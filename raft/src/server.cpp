#include "raft/server.hpp"

#include <grpcpp/grpcpp.h>

#include "asio.hpp"
#include "common/mpsc_queue.hpp"
#include "fmt/core.h"
#include "impl/persistence.hpp"
#include "raft_protos/raft.grpc.pb.h"
#include "utils/grpc_data.hpp"
#include "utils/grpc_errors.hpp"

namespace raft
{
    namespace
    {
        using common::MPSCQueue;

        // LeaderClientInfo contains the state needed to manage the replication of log entries to a
        // single follower.
        struct LeaderClientInfo
        {
            // The index of the next log entry to send to the replica.
            uint64_t nextIndex = 0;
            // The index of the highest log entry known to be replicated.
            uint64_t matchIndex = 0;
            std::unique_ptr<asio::steady_timer> heartbeatTimer;
            // The maximum number of log entries to send in a single AppendEntries request.
            // TODO: this will be adjusted dynamically based on the replica's responses.
            uint64_t batchSize = 1;
        };

        struct ClientInfo
        {
            std::unique_ptr<Client> client;
            std::string id;
            std::string address;
        };

        struct Log
        {
            std::vector<data::LogEntry> entries;
            // The index of the first entry in the log.
            // For instance, if the log contains entries with indices 10, 11, and 12, then baseIndex
            // will be 10.
            uint64_t baseIndex;

            data::LogEntry& get(uint64_t index) { return entries[index - baseIndex]; }

            [[nodiscard]] data::LogEntry const& get(uint64_t index) const
            {
                return entries[index - baseIndex];
            }
        };

        struct CandidateInfo
        {
            uint64_t voteCount;  // The number of votes received.
        };

        struct LeaderInfo
        {
            std::unordered_map<std::string, LeaderClientInfo>
                clients;  // The state of each replica by ID.
        };

        struct FollowerInfo
        {
            std::optional<std::string> votedFor;
        };

        using State = std::variant<CandidateInfo, LeaderInfo, FollowerInfo>;

        enum class Lifecycle : uint8_t
        {
            Initialized,
            Running,
            Stopping,
            Stopped
        };

        // The maximum scheduled interval in microseconds between persistence events.
        constexpr std::chrono::microseconds MAX_PERSISTENCE_INTERVAL(1000);
        // The maximum number of log entries before persistence is triggered.
        constexpr uint64_t MAX_LOG_ENTRIES(1024);

        // ServerImpl is the implementation of the Raft server.
        // Note that this server does not conform to RAII. If not shutdown,
        // the server will run indefinitely and manage its own lifecycle.
        class ServerImpl final
            : public Server
            , std::enable_shared_from_this<ServerImpl>
        {
          public:
            ServerImpl(std::string id,
                       std::unique_ptr<ClientFactory> clientFactory,
                       std::shared_ptr<Persister> persister,
                       std::optional<CommitCallback> commitCallback,
                       std::optional<LeaderChangedCallback> leaderChangedCallback,
                       TimeoutInterval timeoutInterval,
                       uint64_t heartbeatInterval);

            ~ServerImpl() override;

            tl::expected<void, Error> init(const std::vector<Peer>& peers, uint16_t threadCount);

            tl::expected<void, Error> start() override;
            void shutdown() override;

            void handleAppendEntries(
                const data::AppendEntriesRequest& request,
                std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback)
                override;

            void handleRequestVote(
                const data::RequestVoteRequest& request,
                std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback)
                override;

            [[nodiscard]] std::optional<std::string> getLeaderID() const override;

            tl::expected<EntryInfo, Error> append(std::vector<std::byte> data) override;

            void setCommitCallback(CommitCallback callback) override;

            void setLeaderChangedCallback(LeaderChangedCallback callback) override;

            [[nodiscard]] tl::expected<uint64_t, Error> getTerm() const override;

            [[nodiscard]] tl::expected<uint64_t, Error> getCommitIndex() const override;

            [[nodiscard]] tl::expected<uint64_t, Error> getLogByteCount() const override;

            [[nodiscard]] std::string getId() const override;

          private:
            data::PersistedState getPersistedState() const;
            bool shutdownCalled() const
            {
                return lifecycle_ == Lifecycle::Stopped || lifecycle_ == Lifecycle::Stopping;
            }

            // Resets the timer and schedules it to run at the next timeout interval.
            void scheduleTimeout();
            // Resets the heartbeat timer and schedules it to run at the next heartbeat interval.
            void scheduleHeartbeatTimeout();

            void processTimeout();
            void processHeartbeatTimeout();
            void processAppend(const std::vector<std::byte>& data,
                               std::function<void(tl::expected<EntryInfo, Error>)> callback);
            void processInboundAppendEntries(
                const data::AppendEntriesRequest& request,
                std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback);
            void processInboundRequestVote(
                const data::RequestVoteRequest& request,
                std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback);
            void processGetTerm(std::function<void(uint64_t)> callback) const;
            void processGetCommitIndex(std::function<void(uint64_t)> callback) const;
            void processGetLogByteCount(std::function<void(uint64_t)> callback) const;
            void processGetLeaderID(std::function<void(std::optional<std::string>)> callback) const;

            // postPersist serializes the current state and creates a new persist request to the
            // persistence handler.
            void postPersist(std::function<void(tl::expected<void, Error>)> callback) const;

            std::atomic<Lifecycle> lifecycle_ {Lifecycle::Initialized};
            std::once_flag startFlag_;
            // The global lock for the server.
            std::mutex mutex_;
            asio::io_context io_;
            asio::executor_work_guard<asio::io_context::executor_type> work_;

            std::random_device rng_;
            std::mt19937 gen_ {rng_()};

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
            // The log starts at index 0.
            Log log_ {};
            uint64_t timeoutInterval_;
            uint64_t heartbeatInterval_;
            State state_ = CandidateInfo {};

            std::optional<std::string> lastLeaderID_;

            mutable asio::strand<asio::io_context::executor_type> strand_;
            std::vector<std::thread> threads_;

            std::unique_ptr<impl::PersistenceHandler> persistenceHandler_;
            std::unique_ptr<asio::steady_timer> timer_;
        };
    }  // namespace

    ServerImpl::ServerImpl(std::string id,
                           std::unique_ptr<ClientFactory> clientFactory,
                           std::shared_ptr<Persister> persister,
                           std::optional<CommitCallback> commitCallback,
                           std::optional<LeaderChangedCallback> leaderChangedCallback,
                           TimeoutInterval timeoutInterval,
                           uint64_t heartbeatInterval)
        : work_(io_.get_executor())
        , id_(std::move(id))
        , clientFactory_(std::move(clientFactory))
        , persister_(std::move(persister))
        , commitCallback_(std::move(commitCallback))
        , leaderChangedCallback_(std::move(leaderChangedCallback))
        , timeoutInterval_(timeoutInterval.sample(gen_))
        , heartbeatInterval_(heartbeatInterval)
        , strand_(io_.get_executor())
    {
    }

    ServerImpl::~ServerImpl()
    {
        shutdown();
    }

    tl::expected<void, Error> ServerImpl::init(const std::vector<Peer>& peers, uint16_t threadCount)
    {
        if (threadCount < 1)
        {
            return tl::make_unexpected(errors::InvalidArgument {"threadCount must be >= 1"});
        }
        if (auto result = persister_->loadState(); result.has_value())
        {
            auto state = data::deserialize(*result);
            if (!state)
            {
                return tl::make_unexpected(state.error());
            }
            term_ = state->term;
            commitIndex_ = state->commitIndex;
            log_ = Log {.entries = std::move(state->entries), .baseIndex = 0};
        }

        clients_.clear();
        clients_.reserve(peers.size());
        clientIndices_.clear();
        clientIndices_.reserve(peers.size());

        for (const auto& peer : peers)
        {
            auto client = clientFactory_->createClient(peer.address);
            if (!client)
            {
                return tl::make_unexpected(client.error());
            }
            if (clientIndices_.contains(peer.id))
            {
                return tl::make_unexpected(
                    errors::InvalidArgument {fmt::format("duplicate peer id: {}", peer.id)});
            }

            clientIndices_[peer.id] = clients_.size();
            clients_.push_back(
                ClientInfo {.client = std::move(*client), .id = peer.id, .address = peer.address});
        }
        // Start the asio threads here, even though Raft consensus has not started yet. This is so
        // that we can handle simple requests like GetTerm and GetCommitIndex immediately.
        for (uint16_t i = 0; i < threadCount; i++)
        {
            threads_.emplace_back([this] { io_.run(); });
        }

        return {};
    }

    void ServerImpl::shutdown()
    {
        if (shutdownCalled())
        {
            return;
        }

        work_.reset();
        for (auto& thread : threads_)
        {
            thread.join();
        }
    }

    data::PersistedState ServerImpl::getPersistedState() const
    {
        return {.term = term_, .entries = log_.entries, .commitIndex = commitIndex_};
    }

    void ServerImpl::scheduleTimeout()
    {
        if (!timer_)
        {
            timer_ = std::make_unique<asio::steady_timer>(io_);
        }
        timer_->expires_from_now(asio::chrono::milliseconds(timeoutInterval_));
        timer_->async_wait(
            [this](asio::error_code ec)
            {
                if (ec)
                {
                    return;
                }
                asio::post(strand_, [this] { processTimeout(); });
                scheduleTimeout();
            });
    }

    tl::expected<void, Error> ServerImpl::start() override
    {
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }

        std::call_once(startFlag_,
                       [this]
                       {
                           persistenceHandler_ = std::make_unique<impl::PersistenceHandler>(
                               persister_, MAX_PERSISTENCE_INTERVAL, MAX_LOG_ENTRIES);
                           scheduleTimeout();
                       });
        return {};
    }

    void ServerImpl::handleAppendEntries(
        const data::AppendEntriesRequest& request,
        std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback)
    {
        asio::post(strand_,
                   [this, request, callback = std::move(callback)]
                   { processInboundAppendEntries(request, callback); });
    }

    void ServerImpl::handleRequestVote(
        const data::RequestVoteRequest& request,
        std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback)
    {
        asio::post(strand_,
                   [this, request, callback = std::move(callback)]
                   { processInboundRequestVote(request, callback); });
    }

    tl::expected<uint64_t, Error> ServerImpl::getTerm() const
    {
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }
        std::promise<uint64_t> promise;
        auto future = promise.get_future();
        asio::post(strand_,
                   [this, &promise]
                   { processGetTerm([&promise](uint64_t term) { promise.set_value(term); }); });
        return future.get();
    }

    tl::expected<uint64_t, Error> ServerImpl::getCommitIndex() const
    {
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }
        std::promise<uint64_t> promise;
        auto future = promise.get_future();
        asio::post(
            strand_,
            [this, &promise]
            { processGetCommitIndex([&promise](uint64_t index) { promise.set_value(index); }); });
        return future.get();
    }

    tl::expected<uint64_t, Error> ServerImpl::getLogByteCount() const
    {
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }
        std::promise<uint64_t> promise;
        auto future = promise.get_future();
        asio::post(
            strand_,
            [this, &promise]
            { processGetLogByteCount([&promise](uint64_t count) { promise.set_value(count); }); });
        return future.get();
    }

    std::optional<std::string> ServerImpl::getLeaderID() const
    {
        std::promise<std::optional<std::string>> promise;
        auto future = promise.get_future();
        asio::post(strand_,
                   [this, &promise]
                   {
                       processGetLeaderID([&promise](std::optional<std::string> id)
                                          { promise.set_value(id); });
                   });
        return future.get();
    }

    tl::expected<EntryInfo, Error> ServerImpl::append(std::vector<std::byte> data)
    {
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }
        std::promise<tl::expected<EntryInfo, Error>> promise;
        auto future = promise.get_future();
        asio::post(strand_,
                   [this, data = std::move(data), &promise]() mutable
                   {
                       // TODO: Implement append handling
                       promise.set_value(tl::make_unexpected(errors::Unimplemented {}));
                   });
        return future.get();
    }

    // This is thread-safe since ID is a constant.
    std::string ServerImpl::getId() const
    {
        return id_;
    }

    void ServerImpl::setCommitCallback(CommitCallback callback)
    {
        std::lock_guard lock {mutex_};
        commitCallback_ = callback;
    }

    void ServerImpl::setLeaderChangedCallback(LeaderChangedCallback callback)
    {
        std::lock_guard lock {mutex_};
        leaderChangedCallback_ = callback;
    }

    void ServerImpl::processTimeout()
    {
        term_++;
        state_ = CandidateInfo {};
        scheduleTimeout();
    }

    void ServerImpl::processHeartbeatTimeout()
    {
        // TODO: Implement heartbeat timeout processing
    }

    void ServerImpl::processInboundAppendEntries(
        const data::AppendEntriesRequest& request,
        std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback)
    {
        // TODO: Implement AppendEntries request handling
        (void)request;
        (void)callback;
    }

    void ServerImpl::processInboundRequestVote(
        const data::RequestVoteRequest& request,
        std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback)
    {
        // TODO: Implement RequestVote request handling
        (void)request;
        (void)callback;
    }

    void ServerImpl::processGetTerm(std::function<void(uint64_t)> callback) const
    {
        callback(term_);
    }

    void ServerImpl::processGetCommitIndex(std::function<void(uint64_t)> callback) const
    {
        callback(commitIndex_);
    }

    void ServerImpl::processGetLogByteCount(std::function<void(uint64_t)> callback) const
    {
        const uint64_t count = log_.entries.size() * sizeof(data::LogEntry);
        callback(count);
    }

    void ServerImpl::processGetLeaderID(
        std::function<void(std::optional<std::string>)> callback) const
    {
        callback(lastLeaderID_);
    }

    void ServerImpl::postPersist(std::function<void(tl::expected<void, Error>)> callback) const
    {
        auto data = data::serialize(getPersistedState());
        // We need the guard to keep the threads alive until the callback is invoked.
        auto cb = [this, guard = work_, callback = std::move(callback)]
        {
            (void)guard;
            // PersistenceHandler may run the callback on a different thread, so we post it back to
            // the strand.
            asio::post(strand_, [callback] { callback({}); });
        };
        persistenceHandler_->addRequest(impl::PersistenceRequest {.data = data, .callback = cb});
    }

    tl::expected<std::shared_ptr<Server>, Error> createServer(ServerCreateConfig& config)
    {
        auto server = std::make_shared<ServerImpl>(config.id,
                                                   std::move(config.clientFactory),
                                                   config.persister,
                                                   config.commitCallback,
                                                   config.leaderChangedCallback,
                                                   config.timeoutInterval,
                                                   config.heartbeatInterval);
        if (auto result = server->init(config.peers, config.threadCount); !result)
        {
            return tl::make_unexpected(result.error());
        }
        return server;
    }
}  // namespace raft
