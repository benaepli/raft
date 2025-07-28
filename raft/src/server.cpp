#include "raft/server.hpp"
#include "utils/grpc_data.hpp"
#include "utils/grpc_errors.hpp"

#include <grpcpp/grpcpp.h>
#include "raft_protos/raft.grpc.pb.h"

namespace raft {
    namespace {
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
            std::string address;
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

        // ServerImpl is the implementation of the Raft server.
        class ServerImpl final : public Server {
        public:
            ServerImpl(std::unique_ptr<ClientFactory> clientFactory,
                       std::shared_ptr<Persister> persister,
                       std::optional<CommitCallback> commitCallback,
                       std::optional<LeaderChangedCallback> leaderChangedCallback) : clientFactory_(
                    std::move(clientFactory)),
                persister_(std::move(persister)),
                commitCallback_(std::move(commitCallback)),
                leaderChangedCallback_(std::move(leaderChangedCallback)) {
            }

            ~ServerImpl() override = default;

            tl::expected<void, Error> init(std::vector<std::string> addresses);

            tl::expected<data::AppendEntriesResponse, Error> handleAppendEntries(
                const data::AppendEntriesRequest &request
            ) override;

            tl::expected<data::RequestVoteResponse, Error> handleRequestVote(
                const data::RequestVoteRequest &request
            ) override;

            [[nodiscard]] std::optional<std::string> getLeaderAddress() const override;

            tl::expected<EntryInfo, Error> append(std::vector<std::byte> data) override;

            void setCommitCallback(CommitCallback callback) override;

            void setLeaderChangedCallback(LeaderChangedCallback callback) override;

            [[nodiscard]] uint64_t getTerm() const override;

            [[nodiscard]] uint64_t getCommitIndex() const override;

            [[nodiscard]] uint64_t getLogByteCount() const override;

        private:
            // The global lock for the server.
            std::mutex mutex_;
            std::unique_ptr<ClientFactory> clientFactory_;
            // Clients for the other replicas.
            std::vector<ClientInfo> clients_;
            std::shared_ptr<Persister> persister_;
            std::optional<CommitCallback> commitCallback_;
            std::optional<LeaderChangedCallback> leaderChangedCallback_;
            std::optional<std::string> leaderAddress_;
            uint64_t term_ = 0;
            uint64_t commitIndex_ = 0;
            Log log_{};
        };
    } // namespace

    tl::expected<std::shared_ptr<Server>, Error> createServer(const ServerCreateConfig &config) {
        auto server = std::make_shared<ServerImpl>(config.clientFactory, config.persister,
                                                   config.commitCallback, config.leaderChangedCallback);
        if (auto result = server->init(config.addresses); !result) {
            return tl::make_unexpected(result.error());
        }
        return server;
    }

    tl::expected<std::shared_ptr<Network>, Error> createNetwork(const NetworkCreateConfig &config) {
        return std::make_shared<GrpcNetwork>(*config.handler, config.port);
    }
} // namespace raft
