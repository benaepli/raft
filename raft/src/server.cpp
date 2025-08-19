#include "raft/server.hpp"

#include <grpcpp/grpcpp.h>
#include <spdlog/spdlog.h>

#include "asio.hpp"
#include "common/mpsc_queue.hpp"
#include "fmt/core.h"
#include "impl/log.hpp"
#include "impl/persistence.hpp"
#include "impl/state.hpp"
#include "raft/fmt/errors.hpp"
#include "raft_protos/raft.grpc.pb.h"
#include "utils/grpc_data.hpp"
#include "utils/grpc_errors.hpp"

namespace raft
{
    using impl::Log;
    namespace
    {
        struct ClientInfo
        {
            std::unique_ptr<Client> client;
            std::string id;
            std::string address;
        };

        enum class Lifecycle : uint8_t
        {
            Initialized,
            Running,
            Stopping,
            Stopped
        };

        // The maximum scheduled interval in microseconds between persistence events.
        constexpr std::chrono::duration MAX_PERSISTENCE_INTERVAL = std::chrono::microseconds(500);
        // The maximum number of log entries before persistence is triggered.
        constexpr uint64_t MAX_LOG_ENTRIES(1024);

        // ServerImpl is the implementation of the Raft server.
        class ServerImpl final : public Server
        {
          public:
            ServerImpl(std::string id,
                       std::shared_ptr<ClientFactory> clientFactory,
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

            [[nodiscard]] tl::expected<std::string, Error> getLeaderID() const override;

            tl::expected<EntryInfo, Error> append(std::vector<std::byte> data) override;

            void setCommitCallback(CommitCallback callback) override;

            void clearCommitCallback() override;

            void setLeaderChangedCallback(LeaderChangedCallback callback) override;

            void clearLeaderChangedCallback() override;

            [[nodiscard]] tl::expected<uint64_t, Error> getTerm() const override;

            [[nodiscard]] tl::expected<uint64_t, Error> getCommitIndex() const override;

            [[nodiscard]] tl::expected<uint64_t, Error> getLogByteCount() const override;

            [[nodiscard]] std::string getId() const override;

            [[nodiscard]] tl::expected<Status, Error> getStatus() const override;

          private:
            data::PersistedState getPersistedState() const;
            bool shutdownCalled() const
            {
                return lifecycle_ == Lifecycle::Stopped || lifecycle_ == Lifecycle::Stopping;
            }

            // Resets the timer and schedules it to run at the next timeout interval.
            // Note that this function will be run again at the next timeout interval,
            void scheduleTimeout();
            // Resets the heartbeat timer and schedules it to run at the next heartbeat interval.
            void scheduleHeartbeatTimeout(const std::string& id);

            void processTimeout();
            void processHeartbeatTimeout(const std::string& id);
            void processInboundAppendEntries(
                const data::AppendEntriesRequest& request,
                std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback);
            void processInboundRequestVote(
                const data::RequestVoteRequest& request,
                std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback);

            void invokeLeaderChangedCallback(const std::optional<std::string>& leaderID,
                                             bool isLeader,
                                             bool lostLeadership);

            void invokeCommitCallback(uint64_t index);

            // postPersist serializes the current state and creates a new persist request to the
            // persistence handler.
            void postPersist(std::function<void(tl::expected<void, Error>)> callback) const;
            // postRequestVote issues a RequestVote request to the specified replica.
            void postRequestVote(
                const ClientInfo& client,
                const data::RequestVoteRequest& request,
                std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback);
            void postAppendEntries(
                const std::string& id,
                const data::AppendEntriesRequest& request,
                std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback);

            void onRequestVoteResponse(tl::expected<data::RequestVoteResponse, Error> response);
            void onAppendEntriesResponse(const std::string& id,
                                         const data::AppendEntriesRequest& request,
                                         tl::expected<data::AppendEntriesResponse, Error> response);

            // becomeFollower transitions the server to the Follower state.
            void becomeFollower();
            // becomeLeader transitions the server to the Leader state.
            void becomeLeader();

            void commitNewEntries();

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
            std::shared_ptr<ClientFactory> clientFactory_;
            // Clients for the other replicas.
            std::vector<ClientInfo> clients_;
            // A map from server ID to the index of the client in `clients_`.
            std::unordered_map<std::string, size_t> clientIndices_;

            std::shared_ptr<Persister> persister_;
            std::optional<CommitCallback> commitCallback_;
            std::optional<LeaderChangedCallback> leaderChangedCallback_;
            uint64_t term_ = 0;
            uint64_t commitIndex_ = 0;
            // The log starts at index 1.
            Log log_ {};
            uint64_t timeoutInterval_;
            uint64_t heartbeatInterval_;
            State state_ = CandidateInfo {};

            std::optional<std::string> lastLeaderID_;

            mutable asio::strand<asio::io_context::executor_type> strand_;
            std::vector<std::thread> threads_;

            std::unique_ptr<impl::PersistenceHandler> persistenceHandler_ =
                std::make_unique<impl::PersistenceHandler>(
                    persister_, MAX_PERSISTENCE_INTERVAL, MAX_LOG_ENTRIES);
            std::unique_ptr<asio::steady_timer> timer_;

            RequestConfig requestConfig_ = {};
        };
    }  // namespace

    ServerImpl::ServerImpl(std::string id,
                           std::shared_ptr<ClientFactory> clientFactory,
                           std::shared_ptr<Persister> persister,
                           std::optional<CommitCallback> commitCallback,
                           std::optional<LeaderChangedCallback> leaderChangedCallback,
                           TimeoutInterval timeoutInterval,
                           uint64_t heartbeatInterval)
        : work_(io_.get_executor())
        , id_(std::move(id))
        , clientFactory_(clientFactory)
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
            state_ = FollowerInfo {
                .votedFor = state->votedFor,
            };
            log_ = Log {.entries = std::move(state->entries), .baseIndex = 1};
        }
        else
        {
            log_ = Log {.baseIndex = 1};
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
            if (clientIndices_.find(peer.id) != clientIndices_.end())
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
        lifecycle_ = Lifecycle::Stopping;
        if (timer_)
        {
            timer_->cancel();
        }
        if (std::holds_alternative<LeaderInfo>(state_))
        {
            auto& leaderInfo = std::get<LeaderInfo>(state_);
            for (auto& [_, leaderClientInfo] : leaderInfo.clients)
            {
                leaderClientInfo.heartbeatTimer.cancel();
            }
        }

        work_.reset();
        for (auto& thread : threads_)
        {
            thread.join();
        }
        lifecycle_ = Lifecycle::Stopped;
    }

    data::PersistedState ServerImpl::getPersistedState() const
    {
        data::PersistedState state {.term = term_, .entries = log_.entries};
        if (std::holds_alternative<FollowerInfo>(state_))
        {
            const auto& followerInfo = std::get<FollowerInfo>(state_);
            state.votedFor = followerInfo.votedFor;
        }
        return state;
    }

    void ServerImpl::scheduleTimeout()
    {
        auto guard = work_;
        if (shutdownCalled())
        {
            return;
        }
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
            });
    }

    void ServerImpl::scheduleHeartbeatTimeout(const std::string& id)
    {
        auto guard = work_;
        if (shutdownCalled())
        {
            return;
        }
        if (!std::holds_alternative<LeaderInfo>(state_))
        {
            return;
        }
        auto& leaderInfo = std::get<LeaderInfo>(state_);
        auto it = leaderInfo.clients.find(id);
        if (it == leaderInfo.clients.end())
        {
            spdlog::error("[{}] unknown client ID: {}", id_, id);
            return;
        }
        auto& leaderClientInfo = it->second;
        auto& timer = leaderClientInfo.heartbeatTimer;
        timer.expires_from_now(asio::chrono::milliseconds(heartbeatInterval_));
        timer.async_wait(
            [this, id](asio::error_code ec)
            {
                if (ec)
                {
                    return;
                }
                asio::post(strand_, [this, id] { processHeartbeatTimeout(id); });
            });
    }

    tl::expected<void, Error> ServerImpl::start()
    {
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }

        std::call_once(startFlag_,
                       [this]
                       {
                           lifecycle_ = Lifecycle::Running;
                           scheduleTimeout();
                       });
        return {};
    }

    void ServerImpl::handleAppendEntries(
        const data::AppendEntriesRequest& request,
        std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback)
    {
        auto guard = work_;
        if (shutdownCalled())
        {
            callback(tl::make_unexpected(errors::NotRunning {}));
            return;
        }
        asio::post(
            strand_,
            [this, request, callback = std::move(callback)]
            {
                processInboundAppendEntries(
                    request,
                    [this, callback](tl::expected<data::AppendEntriesResponse, Error> response)
                    {
                        postPersist(
                            [this, callback, response = std::move(response)](
                                tl::expected<void, Error> result)
                            {
                                if (!result)
                                {
                                    spdlog::error(
                                        "[{}] failed to persist state after AppendEntries: {}",
                                        id_,
                                        result.error());
                                    callback(tl::make_unexpected(result.error()));
                                    return;
                                }
                                callback(response);
                            });
                    });
            });
    }

    void ServerImpl::handleRequestVote(
        const data::RequestVoteRequest& request,
        std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback)
    {
        auto guard = work_;
        if (shutdownCalled())
        {
            callback(tl::make_unexpected(errors::NotRunning {}));
            return;
        }
        asio::post(strand_,
                   [this, request, callback = std::move(callback)]
                   {
                       processInboundRequestVote(
                           request,
                           [this, callback](tl::expected<data::RequestVoteResponse, Error> response)
                           {
                               postPersist(
                                   [this, callback, response = std::move(response)](
                                       tl::expected<void, Error> result)
                                   {
                                       if (!result)
                                       {
                                           spdlog::error(
                                               "[{}] failed to persist state after RequestVote: {}",
                                               id_,
                                               result.error());
                                           callback(tl::make_unexpected(result.error()));
                                           return;
                                       }
                                       callback(response);
                                   });
                           });
                   });
    }

    tl::expected<uint64_t, Error> ServerImpl::getTerm() const
    {
        auto guard = work_;
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }
        std::promise<tl::expected<uint64_t, Error>> promise;
        auto future = promise.get_future();
        asio::post(strand_,
                   [this, &promise]
                   {
                       postPersist(
                           [this, &promise, term = term_](tl::expected<void, Error> result)
                           {
                               if (!result)
                               {
                                   spdlog::error("[{}] failed to persist state before getTerm: {}",
                                                 id_,
                                                 result.error());
                                   promise.set_value(tl::make_unexpected(result.error()));
                                   return;
                               }
                               promise.set_value(term);
                           });
                   });
        return future.get();
    }

    tl::expected<uint64_t, Error> ServerImpl::getCommitIndex() const
    {
        auto guard = work_;
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }
        std::promise<tl::expected<uint64_t, Error>> promise;
        auto future = promise.get_future();
        asio::post(
            strand_,
            [this, &promise]
            {
                postPersist(
                    [this, &promise, commitIndex = commitIndex_](tl::expected<void, Error> result)
                    {
                        if (!result)
                        {
                            spdlog::error("[{}] failed to persist state before getCommitIndex: {}",
                                          id_,
                                          result.error());
                            promise.set_value(tl::make_unexpected(result.error()));
                            return;
                        }
                        promise.set_value(commitIndex);
                    });
            });
        return future.get();
    }

    tl::expected<uint64_t, Error> ServerImpl::getLogByteCount() const
    {
        auto guard = work_;
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }
        std::promise<tl::expected<uint64_t, Error>> promise;
        auto future = promise.get_future();
        asio::post(strand_,
                   [this, &promise]
                   {
                       const uint64_t count = log_.entries.size() * sizeof(data::LogEntry);
                       postPersist(
                           [this, &promise, count](tl::expected<void, Error> result)
                           {
                               if (!result)
                               {
                                   spdlog::error(
                                       "[{}] failed to persist state before getLogByteCount: {}",
                                       id_,
                                       result.error());
                                   promise.set_value(tl::make_unexpected(result.error()));
                                   return;
                               }
                               promise.set_value(count);
                           });
                   });
        return future.get();
    }

    tl::expected<std::string, Error> ServerImpl::getLeaderID() const
    {
        auto guard = work_;
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }
        std::promise<tl::expected<std::optional<std::string>, Error>> promise;
        auto future = promise.get_future();
        asio::post(
            strand_,
            [this, &promise]
            {
                postPersist(
                    [this, &promise, lastLeaderID = lastLeaderID_](tl::expected<void, Error> result)
                    {
                        if (!result)
                        {
                            spdlog::error("[{}] failed to persist state before getLeaderID: {}",
                                          id_,
                                          result.error());
                            promise.set_value(tl::make_unexpected(result.error()));
                            return;
                        }
                        promise.set_value(lastLeaderID);
                    });
            });
        auto result = future.get();
        if (!result)
        {
            return tl::make_unexpected(result.error());
        }
        if (!result.value())
        {
            return tl::make_unexpected(errors::UnknownLeader {});
        }
        return result.value().value();
    }

    tl::expected<EntryInfo, Error> ServerImpl::append(std::vector<std::byte> data)
    {
        auto guard = work_;
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }
        std::promise<tl::expected<EntryInfo, Error>> promise;
        auto future = promise.get_future();

        asio::post(strand_,
                   [this, data = std::move(data), &promise]() mutable
                   {
                       bool isLeader = std::holds_alternative<LeaderInfo>(state_);
                       std::optional<EntryInfo> entryInfo;
                       if (isLeader)
                       {
                           entryInfo = log_.appendOne(term_, std::move(data));
                       }
                       postPersist(
                           [this, &promise, entryInfo, isLeader](tl::expected<void, Error> result)
                           {
                               if (!result)
                               {
                                   spdlog::error("[{}] failed to persist state before append: {}",
                                                 id_,
                                                 result.error());
                                   promise.set_value(tl::make_unexpected(result.error()));
                                   return;
                               }
                               if (!isLeader)
                               {
                                   promise.set_value(tl::make_unexpected(errors::NotLeader {}));
                                   return;
                               }
                               promise.set_value(*entryInfo);
                           });
                   });
        return future.get();
    }

    // This is thread-safe since ID is a constant.
    std::string ServerImpl::getId() const
    {
        return id_;
    }

    tl::expected<Status, Error> ServerImpl::getStatus() const
    {
        auto guard = work_;
        if (shutdownCalled())
        {
            return tl::make_unexpected(errors::NotRunning {});
        }

        std::promise<tl::expected<Status, Error>> promise;
        auto future = promise.get_future();
        asio::post(strand_,
                   [this, &promise]
                   {
                       Status status {.isLeader = std::holds_alternative<LeaderInfo>(state_),
                                      .leaderID = lastLeaderID_,
                                      .term = term_,
                                      .commitIndex = commitIndex_,
                                      .logByteCount = log_.entries.size() * sizeof(data::LogEntry)};
                       postPersist(
                           [this, &promise, status](tl::expected<void, Error> result)
                           {
                               if (!result)
                               {
                                   spdlog::error(
                                       "[{}] failed to persist state before getStatus: {}",
                                       id_,
                                       result.error());
                                   promise.set_value(tl::make_unexpected(result.error()));
                                   return;
                               }
                               promise.set_value(status);
                           });
                   });
        return future.get();
    }

    void ServerImpl::setCommitCallback(CommitCallback callback)
    {
        std::lock_guard lock {mutex_};
        commitCallback_ = callback;
    }

    void ServerImpl::clearCommitCallback()
    {
        std::lock_guard lock {mutex_};
        commitCallback_.reset();
    }

    void ServerImpl::setLeaderChangedCallback(LeaderChangedCallback callback)
    {
        std::lock_guard lock {mutex_};
        leaderChangedCallback_ = callback;
    }

    void ServerImpl::clearLeaderChangedCallback()
    {
        std::lock_guard lock {mutex_};
        leaderChangedCallback_.reset();
    }

    void ServerImpl::processTimeout()
    {
        if (std::holds_alternative<LeaderInfo>(state_))
        {
            return;
        }
        term_++;
        state_ = CandidateInfo {
            .voteCount = 1,  // Vote for self
        };
        scheduleTimeout();

        data::RequestVoteRequest request {
            .term = term_,
            .candidateID = id_,
            .lastLogIndex = log_.lastIndex(),
            .lastLogTerm = log_.lastTerm(),
        };

        postPersist(
            [this, request](tl::expected<void, Error> result)
            {
                if (!result)
                {
                    spdlog::error(
                        "[{}] failed to persist state during timeout: {}", id_, result.error());
                    return;
                }
                for (auto& client : clients_)
                {
                    postRequestVote(client,
                                    request,
                                    [this](tl::expected<data::RequestVoteResponse, Error> response)
                                    { onRequestVoteResponse(std::move(response)); });
                }
            });
    }

    void ServerImpl::processHeartbeatTimeout(const std::string& id)
    {
        if (!std::holds_alternative<LeaderInfo>(state_))
        {
            return;
        }
        auto& leaderInfo = std::get<LeaderInfo>(state_);
        auto it = leaderInfo.clients.find(id);
        if (it == leaderInfo.clients.end())
        {
            spdlog::error("[{}] unknown client ID: {}", id_, id);
            return;
        }
        scheduleHeartbeatTimeout(id);

        auto& leaderClientInfo = it->second;
        uint64_t nextIndex = leaderClientInfo.nextIndex;
        uint64_t lastIndex = std::min(nextIndex + leaderClientInfo.batchSize - 1, log_.lastIndex());
        std::vector<data::LogEntry> entries;
        for (uint64_t i = nextIndex; i <= lastIndex; i++)
        {
            entries.push_back(*log_.get(i));
        }
        auto* prevLogEntry = log_.get(nextIndex - 1);
        auto prevLogTerm = prevLogEntry != nullptr ? prevLogEntry->term : 0;

        data::AppendEntriesRequest appendRequest {
            .term = term_,
            .leaderID = id_,
            .prevLogIndex = nextIndex - 1,
            .prevLogTerm = prevLogTerm,
            .entries = entries,
            .leaderCommit = commitIndex_,
        };
        leaderClientInfo.nextIndex = lastIndex + 1;
        postAppendEntries(
            id,
            appendRequest,
            [this, id, appendRequest](tl::expected<data::AppendEntriesResponse, Error> response)
            { onAppendEntriesResponse(id, appendRequest, std::move(response)); });
    }

    void ServerImpl::processInboundAppendEntries(
        const data::AppendEntriesRequest& request,
        std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback)
    {
        if (request.term < term_)
        {
            callback(data::AppendEntriesResponse {
                .term = term_,
                .success = false,
            });
            return;
        }

        if (request.term > term_ || !std::holds_alternative<FollowerInfo>(state_))
        {
            term_ = request.term;
            bool lostLeadership = std::holds_alternative<LeaderInfo>(state_);
            becomeFollower();
            auto previousLeaderID = lastLeaderID_;
            lastLeaderID_ = request.leaderID;
            if (previousLeaderID != lastLeaderID_)
            {
                invokeLeaderChangedCallback(lastLeaderID_, false, lostLeadership);
            }
        }

        if (!log_.isConsistentWith(request.prevLogIndex, request.prevLogTerm))
        {
            callback(data::AppendEntriesResponse {
                .term = term_,
                .success = false,
            });
            return;
        }
        scheduleTimeout();

        log_.store(request.prevLogIndex + 1, request.entries);

        if (request.leaderCommit > commitIndex_)
        {
            uint64_t const newCommitIndex = std::min(request.leaderCommit, log_.lastIndex());
            for (uint64_t i = commitIndex_ + 1; i <= newCommitIndex; i++)
            {
                invokeCommitCallback(i);
            }
            commitIndex_ = newCommitIndex;
        }

        callback(data::AppendEntriesResponse {
            .term = term_,
            .success = true,
        });
    }

    void ServerImpl::processInboundRequestVote(
        const data::RequestVoteRequest& request,
        std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback)
    {
        if (request.term < term_)
        {
            callback(data::RequestVoteResponse {
                .term = term_,
                .voteGranted = false,
            });
            return;
        }
        if (request.term > term_)
        {
            term_ = request.term;
            becomeFollower();
        }

        if (!std::holds_alternative<FollowerInfo>(state_))
        {
            callback(data::RequestVoteResponse {
                .term = term_,
                .voteGranted = false,
            });
            return;
        }
        auto& followerInfo = std::get<FollowerInfo>(state_);
        if (followerInfo.votedFor.has_value() && *followerInfo.votedFor != request.candidateID)
        {
            callback(data::RequestVoteResponse {
                .term = term_,
                .voteGranted = false,
            });
            return;
        }
        callback(data::RequestVoteResponse {
            .term = term_,
            .voteGranted = log_.candidateIsEligible(request.lastLogIndex, request.lastLogTerm),
        });
        followerInfo.votedFor = request.candidateID;
    }

    void ServerImpl::invokeLeaderChangedCallback(const std::optional<std::string>& leaderID,
                                                 bool isLeader,
                                                 bool lostLeadership)
    {
        postPersist(
            [this, leaderID, isLeader, lostLeadership](tl::expected<void, Error> result)
            {
                if (!result)
                {
                    spdlog::error("[{}] failed to persist state before leader changed callback: {}",
                                  id_,
                                  result.error());
                    return;
                }
                std::lock_guard lock {mutex_};
                if (leaderChangedCallback_)
                {
                    (*leaderChangedCallback_)(leaderID, isLeader, lostLeadership);
                }
            });
    }

    void ServerImpl::invokeCommitCallback(uint64_t index)
    {
        auto* logEntry = log_.get(index);
        if (logEntry == nullptr)
        {
            spdlog::error("[{}] failed to get log entry at index {}", id_, index);
            return;
        }

        std::lock_guard lock {mutex_};
        if (commitCallback_)
        {
            EntryInfo info {.index = index, .term = logEntry->term};
            (*commitCallback_)(info, logEntry->data);
        }
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

    void ServerImpl::postRequestVote(
        const ClientInfo& client,
        const data::RequestVoteRequest& request,
        std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback)
    {
        auto cb = [this, guard = work_, callback = std::move(callback)](
                      tl::expected<data::RequestVoteResponse, Error> response)
        {
            (void)guard;
            asio::post(strand_, [callback, response = std::move(response)] { callback(response); });
        };
        client.client->requestVote(request, requestConfig_, cb);
    }

    void ServerImpl::postAppendEntries(
        const std::string& id,
        const data::AppendEntriesRequest& request,
        std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback)
    {
        auto it = clientIndices_.find(id);
        if (it == clientIndices_.end())
        {
            spdlog::error("[{}] unknown client ID: {}", id_, id);
            return;
        }
        auto& client = clients_[it->second];
        auto cb = [this, guard = work_, callback = std::move(callback)](
                      tl::expected<data::AppendEntriesResponse, Error> response)
        {
            (void)guard;
            asio::post(strand_, [callback, response = std::move(response)] { callback(response); });
        };
        client.client->appendEntries(request, requestConfig_, cb);
    }

    void ServerImpl::onRequestVoteResponse(tl::expected<data::RequestVoteResponse, Error> response)
    {
        if (!response)
        {
            spdlog::debug(
                "[{}] received RequestVote response with error: {}", id_, response.error());
            return;
        }

        const auto& voteResponse = *response;
        if (voteResponse.term < term_)
        {
            // We have already moved on to a newer term, so we ignore this response.
            return;
        }
        if (voteResponse.term > term_)
        {
            term_ = voteResponse.term;
            becomeFollower();
            return;
        }

        if (!std::holds_alternative<CandidateInfo>(state_))
        {
            // This might happen if we receive a RequestVote response after we have already
            // become the leader.
            return;
        }
        auto& candidateInfo = std::get<CandidateInfo>(state_);
        if (!voteResponse.voteGranted)
        {
            return;
        }
        candidateInfo.voteCount++;
        auto neededVotes = (clients_.size() / 2) + 1;
        if (candidateInfo.voteCount < neededVotes)
        {
            return;
        }
        // We have enough votes to become the leader.
        becomeLeader();
    }

    void ServerImpl::onAppendEntriesResponse(
        const std::string& id,
        const data::AppendEntriesRequest& request,
        tl::expected<data::AppendEntriesResponse, Error> response)
    {
        if (!response)
        {
            spdlog::debug(
                "[{}] received AppendEntries response with error: {}", id_, response.error());
            return;
        }

        const auto& voteResponse = *response;
        if (voteResponse.term < term_)
        {
            // We have already moved on to a newer term, so we ignore this response.
            return;
        }
        if (voteResponse.term > term_)
        {
            term_ = voteResponse.term;
            becomeFollower();
            return;
        }

        if (!std::holds_alternative<LeaderInfo>(state_))
        {
            spdlog::error(
                "[{}] received AppendEntries response in correct term while not a leader: {}",
                id_,
                id);
            return;
        }
        auto& leaderInfo = std::get<LeaderInfo>(state_);
        auto it = leaderInfo.clients.find(id);
        if (it == leaderInfo.clients.end())
        {
            spdlog::error("[{}] unknown client ID: {}", id_, id);
            return;
        }
        auto& leaderClientInfo = it->second;
        if (!voteResponse.success)
        {
            if (leaderClientInfo.nextIndex < request.prevLogIndex + 1)
            {
                // If this is the case, then we don't need to lower nextIndex because
                // another server has already done it for us.
                return;
            }
            if (leaderClientInfo.matchIndex >= request.prevLogIndex)
            {
                // This safeguards against out-of-order responses. If we already know the server
                // contains entries past and including prevLogIndex, then decrementing nextIndex
                // would result in us re-sending them.
                return;
            }
            leaderClientInfo.nextIndex = std::min(leaderClientInfo.nextIndex, request.prevLogIndex);
            return;
        }

        const uint64_t newMatchIndex = request.prevLogIndex + request.entries.size();
        leaderClientInfo.matchIndex = std::max(leaderClientInfo.matchIndex, newMatchIndex);
        leaderClientInfo.nextIndex = std::max(leaderClientInfo.nextIndex, newMatchIndex + 1);

        commitNewEntries();
    }

    void ServerImpl::becomeFollower()
    {
        state_ = FollowerInfo {};
        scheduleTimeout();
    }

    void ServerImpl::becomeLeader()
    {
        if (timer_)
        {
            timer_->cancel();
        }
        state_ = LeaderInfo {};
        auto& leaderInfo = std::get<LeaderInfo>(state_);
        leaderInfo.clients.reserve(clients_.size());
        for (const auto& client : clients_)
        {
            leaderInfo.clients.emplace(std::pair {client.id,
                                                  LeaderClientInfo {
                                                      .nextIndex = log_.lastIndex() + 1,
                                                      .matchIndex = 0,
                                                      .heartbeatTimer = asio::steady_timer(io_),
                                                      .batchSize = 1,  // Default batch size.
                                                  }});
            scheduleHeartbeatTimeout(client.id);
        }
        invokeLeaderChangedCallback(id_, true, false);
    }

    void ServerImpl::commitNewEntries()
    {
        auto leaderInfo = std::get_if<LeaderInfo>(&state_);
        if (leaderInfo == nullptr)
        {
            spdlog::error("[{}] commitNewEntries called while not a leader", id_);
            return;
        }
        uint64_t newCommitIndex = commitIndex_;
        for (uint64_t n = log_.lastIndex(); n > commitIndex_; --n)
        {
            if (log_.get(n)->term != term_)
            {
                continue;
            }

            int majorityCount = 1;  // Leader itself.
            for (const auto& [id, clientInfo] : leaderInfo->clients)
            {
                if (clientInfo.matchIndex >= n)
                {
                    majorityCount++;
                }
            }

            if (majorityCount > (clients_.size() + 1) / 2)
            {
                newCommitIndex = n;
                break;  // Found it, no need to check smaller indices.
            }
        }
        for (auto i = commitIndex_ + 1; i <= newCommitIndex; i++)
        {
            invokeCommitCallback(i);
        }
        commitIndex_ = std::max(newCommitIndex, commitIndex_);
    }

    tl::expected<std::shared_ptr<Server>, Error> createServer(ServerCreateConfig& config)
    {
        auto server = std::make_shared<ServerImpl>(config.id,
                                                   config.clientFactory,
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
