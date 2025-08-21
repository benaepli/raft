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

    /// Information about a client request.
    ///
    /// This is used for deduplication. The client ID must be unique, and the request ID must be
    /// monotonically increasing. For deduplication to function correctly, each request must be
    /// processed sequentially for a given client ID.
    struct RequestInfo
    {
        std::string clientID;  ///< The client ID for this request.
        std::string requestID;  ///< The request ID for this request.
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
        /// @param info The client and request ID for this request.
        /// @param value The data to commit to the Raft log.
        /// @return The unique commit identifier on success, or an Error if the operation failed.
        tl::expected<std::string, Error> commit(const RequestInfo& info,
                                                const std::vector<std::byte>& value);

        /// Sets the commit callback, which runs when a log entry is committed.
        /// The callback may be called on a different thread.
        /// @param callback The callback function to set.
        void setCommitCallback(CommitCallback callback);

        /// Clears the commit callback.
        void clearCommitCallback();

        /// Clears the stored deduplication information for a specific client.
        /// @param clientID The client ID to clear from deduplication tracking.
        void clearClient(std::string_view clientID);

      private:
        std::shared_ptr<raft::Server> server_;
        std::shared_ptr<raft::Network> network_;
        std::optional<CommitCallback> commitCallback_;
    };

    /// Creates a new enhanced Raft server with the given configuration.
    /// @param config The configuration for the server.
    /// @return A unique pointer to the enhanced server instance or an error.
    tl::expected<std::unique_ptr<Server>, Error> createServer(const ServerCreateConfig& config);
}  // namespace raft::enhanced