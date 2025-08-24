#include <unordered_map>
#include <unordered_set>
#include <variant>

#include "raft/enhanced/server.hpp"

#include <asio/bind_executor.hpp>
#include <asio/io_context.hpp>
#include <asio/steady_timer.hpp>
#include <asio/strand.hpp>
#include <asio/thread_pool.hpp>
#include <spdlog/spdlog.h>

#include "raft/network.hpp"
#include "serialize.hpp"

namespace raft::enhanced
{
    using LocalHandlerResult = std::variant<errors::NotLeader, errors::Timeout, LocalCommitInfo>;
    struct LocalHandler
    {
        std::weak_ptr<ServerImpl> server;
        EntryInfo info;
        std::unique_ptr<asio::steady_timer> timer;
        std::atomic<bool> done = false;
        LocalCommitCallback callback;

        void operator()(LocalHandlerResult result);
    };

    class ServerImpl : public std::enable_shared_from_this<ServerImpl>
    {
      public:
        explicit ServerImpl(ServerCreateConfig config)
            : threadPool_(config.threadCount)
            , callbackStrand_(threadPool_.get_executor())
            , server_(std::move(config.server))
            , network_(std::move(config.network))
            , commitTimeout_(config.commitTimeout)
            , globalCommitCallback_(config.commitCallback)
        {
            if (server_)
            {
                server_->setCommitCallback(
                    [this](EntryInfo info, std::vector<std::byte> data)
                    { asio::post(callbackStrand_, [this, info, data] { onCommit(info, data); }); });
            }
        }

        ~ServerImpl() { threadPool_.join(); }

        void commit(RequestInfo const& info,
                    const std::vector<std::byte>& value,
                    LocalCommitCallback callback)
        {
            auto entry = Entry {
                .clientID = info.clientID,
                .requestID = info.requestID,
                .data = value,
            };
            auto appendResult = server_->append(serialize(entry));
            if (!appendResult)
            {
                callback(tl::make_unexpected(appendResult.error()));
                return;
            }
            // appendResult is EntryInfo, which contains entry and index. It is guaranteed to be
            // unique for each invocation.
            asio::post(callbackStrand_,
                       [this, appendResult, callback = std::move(callback)]
                       {
                           auto timer = std::make_unique<asio::steady_timer>(threadPool_);
                           auto localHandler = std::make_shared<LocalHandler>(shared_from_this(),
                                                                              *appendResult,
                                                                              std::move(timer),
                                                                              false,
                                                                              std::move(callback));
                           localHandler->timer->expires_from_now(commitTimeout_);
                           localHandler->timer->async_wait(asio::bind_executor(
                               callbackStrand_,  // LocalHandler must execute on the strand, since
                                                 // it accesses the handlers map.
                               [localHandler](asio::error_code ec) mutable
                               {
                                   if (ec)
                                   {
                                       return;  // Timer was cancelled
                                   }
                                   // This code now runs on the strand
                                   localHandler->operator()(errors::Timeout());
                               }));

                           handlers_[*appendResult] = localHandler;
                           indexToInfo_[appendResult->index].emplace(*appendResult);
                       });
        }

        void onCommit(EntryInfo info, std::vector<std::byte> data)
        {
            auto deserialized = deserialize(data);
            if (!deserialized)
            {
                dispatchCommit(info, std::move(data), /*duplicate=*/false);
                spdlog::info(
                    "enhanced server [{}] received entry that does not have deduplication info",
                    server_->getId());
                return;
            }
            auto lastRequest = lastRequest_.find(deserialized->clientID);
            if (lastRequest != lastRequest_.end())
            {
                if (lastRequest->second >= deserialized->requestID)
                {
                    dispatchCommit(info, std::move(data), /*duplicate=*/true);
                    return;
                }
            }
            lastRequest_[deserialized->clientID] = deserialized->requestID;
            dispatchCommit(info, std::move(data), /*duplicate=*/false);
        }

        void clearClient(std::string const& clientID)
        {
            asio::post(callbackStrand_, [this, clientID] { lastRequest_.erase(clientID); });
        }

        void setCommitCallback(GlobalCommitCallback callback)
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            globalCommitCallback_ = std::move(callback);
        }

        void clearCommitCallback()
        {
            std::lock_guard<std::mutex> lock(callbackMutex_);
            globalCommitCallback_.reset();
        }

      private:
        void dispatchCommit(EntryInfo const& info, std::vector<std::byte> data, bool duplicate)
        {
            auto it = handlers_.find(info);
            bool found = it != handlers_.end();
            if (found)
            {
                auto localHandler = it->second;
                localHandler->operator()(LocalCommitInfo {
                    .data = data,
                    .duplicate = duplicate,
                });
            }

            auto infos = indexToInfo_.find(info.index);
            if (infos != indexToInfo_.end())
            {
                for (auto& entry : infos->second)
                {
                    if (info.term == entry.term)
                    {
                        continue;
                    }
                    auto handler = handlers_.find(entry);
                    if (handler != handlers_.end())
                    {
                        handler->second->operator()(errors::NotLeader());
                    }
                }
            }

            std::lock_guard lock(callbackMutex_);
            if (globalCommitCallback_)
            {
                globalCommitCallback_->operator()(data, found, duplicate);
            }
        }

        asio::thread_pool threadPool_;
        asio::strand<asio::thread_pool::executor_type> callbackStrand_;
        std::vector<std::thread> threads_;
        std::shared_ptr<raft::Server> server_;
        std::shared_ptr<raft::Network> network_;
        std::chrono::nanoseconds commitTimeout_;

        std::mutex callbackMutex_;
        std::optional<GlobalCommitCallback> globalCommitCallback_;

        std::unordered_map<std::string, uint64_t> lastRequest_;  // clientID -> requestID
        std::unordered_map<EntryInfo, std::shared_ptr<LocalHandler>> handlers_;
        std::unordered_map<uint64_t, std::unordered_set<EntryInfo>> indexToInfo_;

        friend struct LocalHandler;
    };

    void LocalHandler::operator()(LocalHandlerResult result)
    {
        if (done.exchange(true))
        {
            return;
        }
        timer->cancel();
        if (auto* commit = std::get_if<LocalCommitInfo>(&result))
        {
            callback(*commit);
        }
        else if (auto* notLeader = std::get_if<errors::NotLeader>(&result))
        {
            callback(tl::make_unexpected(*notLeader));
        }
        else if (auto* timeout = std::get_if<errors::Timeout>(&result))
        {
            callback(tl::make_unexpected(*timeout));
        }
        if (auto s = server.lock())
        {
            s->handlers_.erase(info);
            auto infos = s->indexToInfo_.find(info.index);
            if (infos != s->indexToInfo_.end())
            {
                infos->second.erase(info);
            }
        }
    }

    Server::Server(ServerCreateConfig config)
        : pImpl_(std::make_shared<ServerImpl>(std::move(config)))
    {
    }

    Server::~Server() = default;

    Server::Server(Server&&) noexcept = default;
    Server& Server::operator=(Server&&) noexcept = default;

    void Server::commit(RequestInfo const& info,
                        const std::vector<std::byte>& value,
                        LocalCommitCallback callback)
    {
        pImpl_->commit(info, value, std::move(callback));
    }

    void Server::clearClient(std::string const& clientID)
    {
        pImpl_->clearClient(clientID);
    }

    void Server::setCommitCallback(GlobalCommitCallback callback)
    {
        pImpl_->setCommitCallback(std::move(callback));
    }

    void Server::clearCommitCallback()
    {
        pImpl_->clearCommitCallback();
    }

}  // namespace raft::enhanced