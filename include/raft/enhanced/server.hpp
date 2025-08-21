#pragma once

#include "raft/network.hpp"
#include "raft/server.hpp"

namespace raft::enhanced
{
    /// Configuration for creating an enhanced Raft server.
    struct ServerCreateConfig
    {
        std::shared_ptr<raft::Network> network;  ///< The network interface for Raft communication.
        std::shared_ptr<raft::Server> server;  ///< The underlying Raft server instance.
        std::optional<CommitCallback> commitCallback;  ///< The commit callback to use.
    };

    /// Enhanced Raft server providing high-level functionality on top of the core Raft
    /// implementation. This wrapper provides commit waiting, deduplication, and simplified callback
    /// management. All functions are thread-safe.
    class Server
    {
      public:
        /// Starts the enhanced Raft server.
        /// @return void on success, or an Error if startup failed.
        tl::expected<void, Error> start();

        /// Shuts down the enhanced Raft server gracefully.
        /// @return void on success, or an Error if shutdown encountered issues.
        tl::expected<void, Error> shutdown();

        /// Commits data to the Raft log and waits for it to be applied.
        /// Provides request-response semantics with automatic deduplication.
        /// @param id Unique identifier for this request, used for deduplication.
        /// @param value The data to commit to the Raft log.
        /// @return The unique commit identifier on success, or an Error if the operation failed.
        tl::expected<std::string, Error> commit(const std::string& id,
                                                const std::vector<std::byte>& value);

        /// Sets the commit callback, which runs when a log entry is committed.
        /// The callback may be called on a different thread.
        /// @param callback The callback function to set.
        void setCommitCallback(CommitCallback callback);

        /// Clears the commit callback.
        void clearCommitCallback();
    };

    /// Creates a new enhanced Raft server with the given configuration.
    /// @param config The configuration for the server.
    /// @return A unique pointer to the enhanced server instance or an error.
    tl::expected<std::unique_ptr<Server>, Error> createServer(const ServerCreateConfig& config);
}  // namespace raft::enhanced