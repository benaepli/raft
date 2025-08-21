#include "raft/enhanced/server.hpp"

namespace raft::enhanced
{
    Server::Server(ServerCreateConfig config)
        : server_(std::move(config.server))
        , network_(std::move(config.network))
        , commitCallback_(std::move(config.commitCallback))
    {
        if (server_)
        {
            server_->setCommitCallback([this](EntryInfo info, std::vector<std::byte> data)
                                       { onCommit(info, std::move(data)); });
        }
    }

    void Server::setCommitCallback(CommitCallback callback)
    {
        commitCallback_ = std::move(callback);
    }

    void Server::clearCommitCallback()
    {
        commitCallback_.reset();
    }

    tl::expected<void, Error> Server::commit(RequestInfo const& info,
                                             const std::vector<std::byte>& value)
    {
    }

    void Server::onCommit(EntryInfo info, std::vector<std::byte> data) {}

    void Server::clearClient(std::string const& clientID)
    {
        lastRequest_.erase(std::string(clientID));
    }

}  // namespace raft::enhanced