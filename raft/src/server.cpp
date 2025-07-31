#include "raft/server.hpp"

#include "common/mpsc_queue.hpp"
#include "utils/grpc_data.hpp"
#include "utils/grpc_errors.hpp"
#include "impl/persistence.hpp"

#include <grpcpp/grpcpp.h>
#include "raft_protos/raft.grpc.pb.h"
#include "fmt/core.h"
#include "asio.hpp"

namespace raft {
    namespace {
        using common::MPSCQueue;
        class GrpcNetwork;

        // GrpcServiceImpl forwards requests to the ServiceHandler and converts the data between the
        // internal and external representations.
        class GrpcServiceImpl final : public raft_protos::Raft::Service {
        public:
            explicit GrpcServiceImpl(ServiceHandler &handler) : handler_(handler) {
            }

            grpc::Status AppendEntries(
                grpc::ServerContext *context,
                const raft_protos::AppendEntriesRequest *request,
                raft_protos::AppendEntriesReply *response
            ) override {
                auto internalRequest = data::fromProto(*request);
                auto result = handler_.handleAppendEntries(internalRequest);

                // TODO: convert errors.
                if (!result) {
                    return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to handle AppendEntries");
                }
                *response = data::toProto(*result);
                return grpc::Status::OK;
            }

            grpc::Status RequestVote(
                grpc::ServerContext *context,
                const raft_protos::RequestVoteRequest *request,
                raft_protos::RequestVoteReply *response
            ) override {
                auto internalRequest = data::fromProto(*request);
                auto result = handler_.handleRequestVote(internalRequest);
                // TODO: convert errors.
                if (!result) {
                    return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to handle RequestVote");
                }
                *response = data::toProto(*result);
                return grpc::Status::OK;
            }

        private:
            ServiceHandler &handler_;
        };


        class GrpcNetwork final : public Network {
        public:
            explicit GrpcNetwork(ServiceHandler &handler, std::optional<uint16_t> port = std::nullopt)
                : service_(handler), port_(port) {
            }

            tl::expected<void, Error> start() override {
                if (isRunning()) {
                    return tl::make_unexpected(errors::AlreadyRunning{});
                }

                grpc::ServerBuilder builder;

                std::string server_address = "0.0.0.0:";
                if (port_) {
                    server_address += std::to_string(*port_);
                } else {
                    server_address += "0";
                }

                int port;
                builder.AddListeningPort(server_address, grpc::InsecureServerCredentials(), &port);
                builder.RegisterService(&service_);

                server_ = builder.BuildAndStart();
                if (!server_) {
                    return tl::make_unexpected(errors::FailedToStart{});
                }
                return {};
            }

            tl::expected<void, Error> stop() override {
                if (!isRunning()) {
                    return tl::make_unexpected(errors::NotRunning{});
                }

                server_->Shutdown();
                server_.reset();

                return {};
            }

        private:
            friend class GrpcServiceImpl;

            bool isRunning() const { return server_ != nullptr; }

            GrpcServiceImpl service_;
            std::optional<uint16_t> port_;
            std::unique_ptr<grpc::Server> server_;
        };

        struct ClientInfo {
            std::unique_ptr<Client> client;
            std::string id;
            std::string address;
            // The index of the next log entry to send to the replica.
            uint64_t nextIndex = 0;
            // The index of the highest log entry known to be replicated.
            uint64_t matchIndex = 0;
            std::unique_ptr<asio::steady_timer> heartbeatTimer;
            // The maximum number of log entries to send in a single AppendEntries request.
            // TODO: this will be adjusted dynamically based on the replica's responses.
            uint64_t batchSize = 1;
        };

        struct Log {
            std::vector<data::LogEntry> entries;
            // The index of the first entry in the log.
            // For instance, if the log contains entries with indices 10, 11, and 12, then baseIndex
            // will be 10.
            uint64_t baseIndex;

            data::LogEntry &get(uint64_t index) {
                return entries[index - baseIndex];
            }

            data::LogEntry const &get(uint64_t index) const {
                return entries[index - baseIndex];
            }
        };


        namespace events {
            // Timeout occurs when the election timeout elapses.
            struct Timeout {
            };

            // HeartbeatTimeout occurs when the heartbeat timeout elapses.
            struct HeartbeatTimeout {
                std::string id; // The ID of the replica that timed out.
            };

            // AppendEntriesResponse is the response to an AppendEntries request.
            struct AppendEntriesResponse {
                data::AppendEntriesRequest request; // The request that triggered the response.
                data::AppendEntriesResponse response;
            };

            // RequestVoteResponse is the response to a RequestVote request.
            struct RequestVoteResponse {
                data::RequestVoteRequest request;
                data::RequestVoteResponse response;
            };

            // An AppendEntries event is created when this replica receives an AppendEntries request.
            struct AppendEntries {
                data::AppendEntriesRequest request;
                std::promise<tl::expected<data::AppendEntriesResponse, Error> > promise;
            };

            // A RequestVote event is created when this replica receives a RequestVote request.
            struct RequestVote {
                data::RequestVoteRequest request;
                std::promise<tl::expected<data::RequestVoteResponse, Error> > promise;
            };

            struct GetTerm {
                std::promise<uint64_t> promise;
            };

            struct GetCommitIndex {
                std::promise<uint64_t> promise;
            };

            struct GetLogByteCount {
                std::promise<uint64_t> promise;
            };

            struct GetLeaderID {
                std::promise<std::optional<std::string> > promise;
            };

            // PersistenceComplete is created by the IO thread when persistence is complete.
            struct PersistenceComplete {
                // The callback that the event loop should run when persistence is complete.
                std::function<void()> callback;
            };

            struct Append {
                std::vector<std::byte> data;
                std::promise<tl::expected<EntryInfo, Error> > promise;
            };
        } // namespace events

        using Event = std::variant<
            events::Timeout,
            events::HeartbeatTimeout,
            events::AppendEntriesResponse,
            events::RequestVoteResponse,
            events::AppendEntries,
            events::RequestVote,
            events::GetTerm,
            events::GetCommitIndex,
            events::GetLogByteCount,
            events::GetLeaderID,
            events::PersistenceComplete,
            events::Append
        >;

        // The maximum scheduled interval in microseconds between persistence events.
        constexpr std::chrono::microseconds MAX_PERSISTENCE_INTERVAL(1000);
        // The maximum number of log entries before persistence is triggered.
        constexpr uint64_t MAX_LOG_ENTRIES(1024);

        // ServerImpl is the implementation of the Raft server.
        class ServerImpl final : public Server {
        public:
            ServerImpl(std::string id,
                       std::unique_ptr<ClientFactory> clientFactory,
                       std::shared_ptr<Persister> persister,
                       std::optional<CommitCallback> commitCallback,
                       std::optional<LeaderChangedCallback> leaderChangedCallback,
                       TimeoutInterval timeoutInterval,
                       uint64_t heartbeatInterval) : id_(std::move(id)),
                                                     clientFactory_(std::move(clientFactory)),
                                                     persister_(std::move(persister)),
                                                     commitCallback_(std::move(commitCallback)),
                                                     leaderChangedCallback_(std::move(leaderChangedCallback)),
                                                     timeoutInterval_(timeoutInterval.sample(gen_)),
                                                     heartbeatInterval_(heartbeatInterval) {
            }

            ~ServerImpl() override = default;

            tl::expected<void, Error> init(const std::vector<Peer> &peers);

            void start() override;

            void eventLoop() {
            }

            tl::expected<data::AppendEntriesResponse, Error> handleAppendEntries(
                const data::AppendEntriesRequest &request
            ) override;

            tl::expected<data::RequestVoteResponse, Error> handleRequestVote(
                const data::RequestVoteRequest &request
            ) override;

            [[nodiscard]] std::optional<std::string> getLeaderID() const override;

            tl::expected<EntryInfo, Error> append(std::vector<std::byte> data) override;

            void setCommitCallback(CommitCallback callback) override;

            void setLeaderChangedCallback(LeaderChangedCallback callback) override;

            [[nodiscard]] uint64_t getTerm() const override;

            [[nodiscard]] uint64_t getCommitIndex() const override;

            [[nodiscard]] uint64_t getLogByteCount() const override;

            [[nodiscard]] std::string getId() const override;

        private:
            // The global lock for the server.
            std::mutex mutex_;

            std::random_device rng_;
            std::mt19937 gen_{rng_()};

            // The ID. This is a constant throughout the lifetime of the server.
            std::string id_;
            std::unique_ptr<ClientFactory> clientFactory_;
            // Clients for the other replicas.
            std::vector<ClientInfo> clients_;
            // A map from server ID to the index of the client in `clients_`.
            std::unordered_map<std::string, size_t> clientIndices_;

            std::shared_ptr<Persister> persister_;
            std::optional<CommitCallback> commitCallback_;
            std::optional<LeaderChangedCallback> leaderChangedCallback_;
            std::optional<std::string> leaderAddress_;
            uint64_t term_ = 0;
            uint64_t commitIndex_ = 0;
            Log log_{};
            uint64_t timeoutInterval_;
            uint64_t heartbeatInterval_;

            std::optional<std::string> lastLeaderID_;

            std::optional<std::thread> eventThread_;
            mutable MPSCQueue<Event> events_;

            std::unique_ptr<impl::PersistenceHandler> persistenceHandler_;

            asio::io_context io_{};
            std::unique_ptr<asio::steady_timer> timer_;
        };
    } // namespace

    tl::expected<void, Error> ServerImpl::init(const std::vector<Peer> &peers) {
        clients_.clear();
        clients_.reserve(peers.size());
        clientIndices_.clear();
        clientIndices_.reserve(peers.size());

        for (const auto &peer: peers) {
            auto client = clientFactory_->createClient(peer.address);
            if (!client) {
                return tl::make_unexpected(client.error());
            }
            if (clientIndices_.contains(peer.id)) {
                return tl::make_unexpected(errors::InvalidArgument{
                    fmt::format("duplicate peer id: {}", peer.id)
                });
            }

            clientIndices_[peer.id] = clients_.size();
            clients_.push_back(ClientInfo{
                .client = std::move(*client),
                .id = peer.id,
                .address = peer.address
            });
        }
        // Start the event loop here, even though Raft consensus has not started yet. This is so that we can handle
        // simple requests like GetTerm and GetCommitIndex immediately.
        eventThread_.emplace([this] { eventLoop(); });
        return {};
    }

    void ServerImpl::start() {
        std::lock_guard lock{mutex_};

        persistenceHandler_ = std::make_unique<impl::PersistenceHandler>(
            persister_, MAX_PERSISTENCE_INTERVAL, MAX_LOG_ENTRIES);
        timer_ = std::make_unique<asio::steady_timer>(io_, asio::chrono::milliseconds(timeoutInterval_));
    }

    // Forwards an AppendEntries request to the event loop.
    tl::expected<data::AppendEntriesResponse, Error> ServerImpl::handleAppendEntries(
        const data::AppendEntriesRequest &request) {
        events::AppendEntries event{request};
        auto future = event.promise.get_future();
        events_.push(std::move(event));
        return future.get();
    }

    // Forwards a RequestVote request to the event loop.
    tl::expected<data::RequestVoteResponse, Error> ServerImpl::handleRequestVote(
        const data::RequestVoteRequest &request) {
        events::RequestVote event{request};
        auto future = event.promise.get_future();
        events_.push(std::move(event));
        return future.get();
    }

    tl::expected<EntryInfo, Error> ServerImpl::append(std::vector<std::byte> data) {
        events::Append event{std::move(data)};
        auto future = event.promise.get_future();
        events_.push(std::move(event));
        return future.get();
    }

    uint64_t ServerImpl::getTerm() const {
        events::GetTerm event{};
        auto future = event.promise.get_future();
        events_.push(std::move(event));
        return future.get();
    }

    uint64_t ServerImpl::getCommitIndex() const {
        events::GetCommitIndex event{};
        auto future = event.promise.get_future();
        events_.push(std::move(event));
        return future.get();
    }

    uint64_t ServerImpl::getLogByteCount() const {
        events::GetLogByteCount event{};
        auto future = event.promise.get_future();
        events_.push(std::move(event));
        return future.get();
    }

    std::optional<std::string> ServerImpl::getLeaderID() const {
        events::GetLeaderID event{};
        auto future = event.promise.get_future();
        events_.push(std::move(event));
        return future.get();
    }

    // This is thread-safe since ID is a constant.
    std::string ServerImpl::getId() const {
        return id_;
    }

    void ServerImpl::setCommitCallback(CommitCallback callback) {
        std::lock_guard lock{mutex_};
        commitCallback_ = callback;
    }

    void ServerImpl::setLeaderChangedCallback(LeaderChangedCallback callback) {
        std::lock_guard lock{mutex_};
        leaderChangedCallback_ = callback;
    }

    tl::expected<std::shared_ptr<Server>, Error> createServer(ServerCreateConfig &config) {
        auto server = std::make_shared<ServerImpl>(config.id,
                                                   std::move(config.clientFactory), config.persister,
                                                   config.commitCallback, config.leaderChangedCallback,
                                                   config.timeoutInterval,
                                                   config.heartbeatInterval);
        if (auto result = server->init(config.peers); !result) {
            return tl::make_unexpected(result.error());
        }
        return server;
    }

    tl::expected<std::shared_ptr<Network>, Error> createNetwork(const NetworkCreateConfig &config) {
        return std::make_shared<GrpcNetwork>(*config.handler, config.port);
    }
} // namespace raft
