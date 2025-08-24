#pragma once

#include <chrono>
#include <memory>

#include "raft/errors.hpp"
#include "raft/network.hpp"
#include "raft/server.hpp"

namespace raft::enhanced
{
    /// The callback for when a log entry is committed.
    /// @param data The committed data.
    /// @param local Whether the entry was committed through the commit() function.
    /// @param duplicate Whether the entry was a duplicate.
    using GlobalCommitCallback =
        std::function<void(std::vector<std::byte> data, bool local, bool duplicate)>;

    /// Information about a committed entry.
    struct LocalCommitInfo
    {
        std::vector<std::byte> data;  ///< The committed data.
        bool duplicate = false;  ///< Whether the entry was a duplicate.
    };

    /// The callback for when a log entry is committed through commit();
    /// @param result The result of the commit operation.
    using LocalCommitCallback = std::function<void(tl::expected<LocalCommitInfo, Error> result)>;

    /// Configuration for creating an enhanced Raft server.
    struct ServerCreateConfig
    {
        std::shared_ptr<raft::Network> network;  ///< The network interface for Raft communication.
        std::shared_ptr<raft::Server> server;  ///< The underlying Raft server instance.
        std::chrono::nanoseconds commitTimeout = std::chrono::seconds(5);  ///< The commit timeout.
        uint64_t threadCount = 1;  ///< The number of threads to use for timer management.
        std::optional<GlobalCommitCallback> commitCallback;  ///< The commit callback to use.
    };

    /// Information about a client request.
    ///
    /// This is used for deduplication. The client ID must be unique, and the request ID must be
    /// monotonically increasing. For deduplication to function correctly, each request must be
    /// processed sequentially for a given client ID.
    struct RequestInfo
    {
        std::string clientID;  ///< The client ID for this request.
        uint64_t requestID;  ///< The request ID for this request.
    };

    class ServerImpl;

    /// Enhanced Raft server providing high-level functionality on top of the core Raft
    /// implementation.
    ///
    /// This wrapper provides commit management, deduplication, and simplified
    /// callback management. All functions are thread-safe. Callbacks are queued and executed
    /// in a serialized fashion, so it is important to keep callbacks lightweight. Since
    /// this class maintains a separate queue, it may be inconsistent with the underlying
    /// server instance.
    class Server
    {
      public:
        explicit Server(ServerCreateConfig config);
        ~Server();

        Server(Server const&) = delete;
        Server& operator=(Server const&) = delete;
        Server(Server&&) noexcept;
        Server& operator=(Server&&) noexcept;

        /// Commits data to the Raft log and monitors leadership and timeouts.
        /// @param info The client and request ID for this request.
        /// @param value The data to commit to the Raft log.
        /// @param callback The callback to invoke when the commit is completed.
        void commit(RequestInfo const& info,
                    const std::vector<std::byte>& value,
                    LocalCommitCallback callback);

        /// Clears the stored deduplication information for a specific client.
        /// @param clientID The client ID to clear from deduplication tracking.
        void clearClient(std::string const& clientID);

        void setCommitCallback(GlobalCommitCallback callback);
        void clearCommitCallback();

      private:
        std::shared_ptr<ServerImpl> pImpl_;
    };
}  // namespace raft::enhanced