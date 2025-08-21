#include <unordered_map>

#include "raft/enhanced/server.hpp"

#include <asio/io_context.hpp>
#include <spdlog/spdlog.h>

#include "raft/network.hpp"
#include "serialize.hpp"

namespace raft::enhanced
{
    class Server::Impl
    {
      public:
        explicit Impl(ServerCreateConfig config)
            : server_(std::move(config.server))
            , network_(std::move(config.network))
            , commitTimeout_(config.commitTimeout)
            , commitCallback_(std::move(config.commitCallback))
        {
            if (server_)
            {
                server_->setCommitCallback([this](EntryInfo info, std::vector<std::byte> data)
                                           { onCommit(info, std::move(data)); });
            }
        }

        void setCommitCallback(CommitCallback callback) { commitCallback_ = std::move(callback); }

        void clearCommitCallback() { commitCallback_.reset(); }

        tl::expected<void, Error> commit(RequestInfo const& info,
                                         const std::vector<std::byte>& value)
        {
            // Implementation to be completed
            return {};
        }

        void onCommit(EntryInfo info, std::vector<std::byte> data)
        {
            auto deserialized = deserialize(data);
            if (!deserialized)
            {
                if (commitCallback_)
                {
                    (*commitCallback_)(info, std::move(data));
                }
                spdlog::warn(
                    "enhanced server [{}] received entry that does not have deduplication info",
                    server_->getId());
                return;
            }
            // TODO: implement deduplication
        }

        void clearClient(std::string const& clientID) { lastRequest_.erase(std::string(clientID)); }

      private:
        asio::io_context io_;
        std::shared_ptr<raft::Server> server_;
        std::shared_ptr<raft::Network> network_;
        std::chrono::nanoseconds commitTimeout_;
        std::optional<CommitCallback> commitCallback_;
        std::unordered_map<std::string, uint64_t> lastRequest_;  // clientID -> requestID

        std::unordered_map<EntryInfo, std::function<void()>> commitBroadcast_;
        std::vector<std::function<void()>> lostLeadershipBroadcast_;
    };

    Server::Server(ServerCreateConfig config)
        : pImpl_(std::make_unique<Impl>(std::move(config)))
    {
    }

    Server::~Server() = default;

    Server::Server(Server&&) noexcept = default;
    Server& Server::operator=(Server&&) noexcept = default;

    void Server::setCommitCallback(CommitCallback callback)
    {
        pImpl_->setCommitCallback(std::move(callback));
    }

    void Server::clearCommitCallback()
    {
        pImpl_->clearCommitCallback();
    }

    tl::expected<void, Error> Server::commit(RequestInfo const& info,
                                             const std::vector<std::byte>& value)
    {
        return pImpl_->commit(info, value);
    }

    void Server::clearClient(std::string const& clientID)
    {
        pImpl_->clearClient(clientID);
    }

}  // namespace raft::enhanced