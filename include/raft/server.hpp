#pragma once

#include <optional>
#include <random>

#include "impl/hash.h"
#include "raft/client.hpp"
#include "raft/data.hpp"
#include "raft/persister.hpp"

namespace raft
{
    /// The default timeout interval in milliseconds.
    constexpr std::pair<uint64_t, uint64_t> DEFAULT_TIMEOUT_INTERVAL_RANGE = {500, 1000};
    /// The default heartbeat interval in milliseconds.
    constexpr uint64_t DEFAULT_HEARTBEAT_INTERVAL = 10;

    struct TimeoutInterval
    {
        uint64_t min = DEFAULT_TIMEOUT_INTERVAL_RANGE.first;
        uint64_t max = DEFAULT_TIMEOUT_INTERVAL_RANGE.second;

        /// Returns a random timeout interval between min and max.
        /// @param rng The random number generator to use.
        [[nodiscard]] uint64_t sample(std::mt19937& rng) const
        {
            std::uniform_int_distribution dist(min, max);
            return dist(rng);
        }
    };

    /// Information about a log entry.
    struct EntryInfo
    {
        uint64_t index;  ///< The index of the log entry.
        uint64_t term;  ///< The term of the log entry.

        bool operator==(EntryInfo const& other) const = default;
    };

    /// The callback for when a log entry is committed. It accepts the committed data.
    /// @param info Information about the committed entry.
    /// @param data The committed data.
    using CommitCallback = std::function<void(EntryInfo, std::vector<std::byte>)>;

    /// A peer in the Raft cluster.
    struct Peer
    {
        std::string id;  ///< The ID of the peer.
        std::string address;  ///< The address of the peer. If this is the current server, this will
                              ///< be "self".
    };

    /**
     * The callback when the server's known leader changes.
     * @param leader The peer information of the new leader, or std::nullopt if there is no leader.
     * @param isLeader Whether this server is the new leader.
     * @param lostLeadership If this server was the previous leader, this will be true.
     */
    using LeaderChangedCallback =
        std::function<void(std::optional<Peer> leader, bool isLeader, bool lostLeadership)>;

    /// Configuration for creating a Raft server.
    struct ServerCreateConfig
    {
        std::string id;  ///< The ID of the server.
        std::shared_ptr<ClientFactory> clientFactory;  ///< The client factory to use.
        std::vector<Peer> peers;  ///< The list of other addresses of Raft servers in the cluster.
        std::shared_ptr<Persister> persister;  ///< The persister to use.
        std::optional<CommitCallback> commitCallback;  ///< The commit callback to use.
        std::optional<LeaderChangedCallback>
            leaderChangedCallback;  ///< The leader changed callback to use.
        TimeoutInterval timeoutInterval;  ///< The election timeout interval.
        uint64_t heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL;  ///< The heartbeat interval.
        uint16_t threadCount = 1;  ///< The number of threads to use for network I/O and consensus.
    };

    /// A service handler for the Raft server.
    class ServiceHandler
    {
      public:
        virtual ~ServiceHandler() = default;

        /// Handles an AppendEntries request.
        /// @param request The AppendEntries request to handle.
        /// @param callback The callback to invoke with the response or error.
        virtual void handleAppendEntries(
            const data::AppendEntriesRequest& request,
            std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback) = 0;

        /// Handles a RequestVote request.
        /// @param request The RequestVote request to handle.
        /// @param callback The callback to invoke with the response or error.
        virtual void handleRequestVote(
            const data::RequestVoteRequest& request,
            std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback) = 0;
    };

    /// A consistent snapshot of the server's state.
    struct Status
    {
        bool isLeader;  ///< Whether this server is currently the leader.
        std::optional<Peer>
            leader;  ///< The current leader peer information, or std::nullopt if unknown.
        uint64_t term;  ///< The current term of the server.
        uint64_t commitIndex;  ///< The index of the last committed log entry.
        uint64_t logByteCount;  ///< The total size of the log in bytes.
    };

    /// The Raft server interface. This is the main interface for the Raft server.
    ///
    /// All functions are thread-safe. However, callbacks may block the main Raft logic.
    /// Therefore, user callbacks should be lightweight and non-blocking.
    class Server : public ServiceHandler
    {
      public:
        /// Starts Raft consensus.
        virtual tl::expected<void, Error> start() = 0;
        /// Shuts down the Raft server.
        virtual void shutdown() = 0;

        /// Returns the last-known leader.
        /// @return The leader peer information or UnknownLeader if no leader is known.
        [[nodiscard]] virtual tl::expected<Peer, Error> getLeader() const = 0;

        /// If the server is the leader, appends an entry to the log. Otherwise, returns a NotLeader
        /// error. Note that this will not wait for the entry to be committed. An appended entry is
        /// guaranteed to eventually be committed if leadership is not lost.
        /// @param data The data to append to the log.
        /// @return Information about the appended entry or an error.
        virtual tl::expected<EntryInfo, Error> append(std::vector<std::byte> data) = 0;

        /// Sets the commit callback, which runs when a log entry is committed.
        /// The callback may be called on a different thread.
        /// @param callback The callback function to set.
        virtual void setCommitCallback(CommitCallback callback) = 0;

        /// Clears the commit callback.
        virtual void clearCommitCallback() = 0;

        /// Sets the leader changed callback, which runs when the leader changes.
        /// The callback may be called on a different thread.
        /// @param callback The callback function to set.
        virtual void setLeaderChangedCallback(LeaderChangedCallback callback) = 0;

        /// Clears the leader changed callback.
        virtual void clearLeaderChangedCallback() = 0;

        /// Returns the current term.
        /// @return The current term if the server has not been shut down.
        [[nodiscard]] virtual tl::expected<uint64_t, Error> getTerm() const = 0;

        /// Returns the current commit index.
        /// @return The current commit index if the server has not been shut down.
        [[nodiscard]] virtual tl::expected<uint64_t, Error> getCommitIndex() const = 0;

        /// Returns the total size of the log in bytes, which may be useful for snapshot strategy.
        /// @return The total byte count of the log if the server has not been shut down.
        [[nodiscard]] virtual tl::expected<uint64_t, Error> getLogByteCount() const = 0;

        /// Returns the ID of the server.
        /// @return The server's ID.
        [[nodiscard]] virtual std::string getId() const = 0;

        /// Returns a consistent snapshot of the server's current state.
        /// @return The server's status if it has not been shut down.
        [[nodiscard]] virtual tl::expected<Status, Error> getStatus() const = 0;
    };

    /// Creates a new Raft server with the given configuration.
    /// Note that on creation, the server will attempt to read its state from the persister.
    /// The client factory will be shared with the server.
    /// @param config The configuration for the server.
    /// @return A shared pointer to the server or an error.
    tl::expected<std::shared_ptr<Server>, Error> createServer(ServerCreateConfig& config);
}  // namespace raft

namespace std
{
    template<>
    struct hash<raft::EntryInfo>
    {
        std::size_t operator()(const raft::EntryInfo& e) const noexcept
        {
            std::size_t seed = std::hash<uint64_t> {}(e.index);
            raft::impl::hashCombine(seed, e.term);

            return seed;
        }
    };
}  // namespace std