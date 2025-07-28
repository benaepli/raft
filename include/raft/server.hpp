#pragma once

#include <optional>
#include <random>

#include "raft/client.hpp"

namespace raft {
    /// The default timeout interval in milliseconds.
    constexpr std::pair<uint64_t, uint64_t> DEFAULT_TIMEOUT_INTERVAL_RANGE = {100, 200};
    /// The default heartbeat interval in milliseconds.
    constexpr uint64_t DEFAULT_HEARTBEAT_INTERVAL = 10;

    class ServiceHandler;

    struct TimeoutInterval {
        uint64_t min = DEFAULT_TIMEOUT_INTERVAL_RANGE.first;
        uint64_t max = DEFAULT_TIMEOUT_INTERVAL_RANGE.second;

        /// Returns a random timeout interval between min and max.
        /// @param rng The random number generator to use.
        [[nodiscard]] uint64_t sample(std::mt19937 &rng) const {
            std::uniform_int_distribution dist(min, max);
            return dist(rng);
        }
    };

    /// The interface for persisting the Raft server's state.
    struct Persister {
        virtual ~Persister() = default;

        /// Saves the server's persistent state to storage.
        /// This method is called whenever the server's state changes and needs to be persisted.
        /// @param state The serialized state data to persist.
        virtual void saveState(std::vector<std::byte> state) = 0;

        /// Loads the server's persistent state from storage.
        /// This method is called during server initialization to restore previous state.
        /// @return The serialized state data if available, or std::nullopt if no state exists.
        virtual std::optional<std::vector<std::byte> > loadState() = 0;
    };

    /// Information about a log entry.
    struct EntryInfo {
        uint64_t index; ///< The index of the log entry.
        uint64_t term; ///< The term of the log entry.
    };

    /// The callback for when a log entry is committed. It accepts the committed data.
    /// @param info Information about the committed entry.
    /// @param data The committed data.
    using CommitCallback = std::function<void(EntryInfo, std::vector<std::byte>)>;

    /**
     * The callback when the server's known leader changes.
     * @param leaderID The ID of the new leader, or std::nullopt if there is no leader.
     * @param isLeader Whether this server is the new leader.
     * @param lostLeadership If this server was the previous leader, this will be true.
     */
    using LeaderChangedCallback = std::function<void(std::optional<std::string>, bool, bool)>;

    /// A peer in the Raft cluster.
    struct Peer {
        std::string id;
        std::string address;
    };

    /// Configuration for creating a Raft server.
    struct ServerCreateConfig {
        std::string id; ///< The ID of the server.
        std::unique_ptr<ClientFactory> clientFactory; ///< The client factory to use.
        std::vector<Peer> peers; ///< The list of other addresses of Raft servers in the cluster.
        std::shared_ptr<Persister> persister; ///< The persister to use.
        std::optional<CommitCallback> commitCallback; ///< The commit callback to use.
        std::optional<LeaderChangedCallback> leaderChangedCallback; ///< The leader changed callback to use.
        TimeoutInterval timeoutInterval;
        uint64_t heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL;
    };

    struct NetworkCreateConfig {
        std::shared_ptr<ServiceHandler> handler;
        std::optional<uint16_t> port; ///< The port number for this server.
    };

    /// A service handler for the Raft server.
    class ServiceHandler {
    public:
        virtual ~ServiceHandler() = default;

        /// Handles an AppendEntries request.
        /// @param request The AppendEntries request to handle.
        /// @return The AppendEntries response or an error.
        virtual tl::expected<data::AppendEntriesResponse, Error> handleAppendEntries(
            const data::AppendEntriesRequest &request
        ) = 0;

        /// Handles a RequestVote request.
        /// @param request The RequestVote request to handle.
        /// @return The RequestVote response or an error.
        virtual tl::expected<data::RequestVoteResponse, Error> handleRequestVote(
            const data::RequestVoteRequest &request
        ) = 0;
    };

    /// The Raft server interface. This is the main interface for the Raft server. ALl functions are thread-safe.
    class Server : public ServiceHandler {
    public:
        /// Starts Raft consensus.
        virtual void start() = 0;

        /// Returns the ID of the last-known leader, or std::nullopt if either no leader exists or
        /// the leader is unknown.
        /// @return The leader's address or std::nullopt.
        [[nodiscard]] virtual std::optional<std::string> getLeaderID() const = 0;

        /// If the server is the leader, appends an entry to the log. Otherwise, returns a NotLeader error.
        /// Note that this will not wait for the entry to be committed.
        /// An appended entry is guaranteed to eventually be committed if leadership is not lost.
        /// @param data The data to append to the log.
        /// @return Information about the appended entry or an error.
        virtual tl::expected<EntryInfo, Error> append(std::vector<std::byte> data) = 0;

        /// Sets the commit callback, which runs when a log entry is committed.
        /// The callback may be called on a different thread.
        /// @param callback The callback function to set.
        virtual void setCommitCallback(CommitCallback callback) = 0;

        /// Sets the leader changed callback, which runs when the leader changes.
        /// The callback may be called on a different thread.
        /// @param callback The callback function to set.
        virtual void setLeaderChangedCallback(LeaderChangedCallback callback) = 0;

        /// Returns the current term.
        /// @return The current term.
        [[nodiscard]] virtual uint64_t getTerm() const = 0;

        /// Returns the current commit index.
        /// @return The current commit index.
        [[nodiscard]] virtual uint64_t getCommitIndex() const = 0;

        /// Returns the total size of the log in bytes, which may be useful for snapshot strategy.
        /// @return The total byte count of the log.
        [[nodiscard]] virtual uint64_t getLogByteCount() const = 0;

        // Returns the ID of the server.
        [[nodiscard]] virtual std::string getId() const = 0;
    };

    /// The network interface for the Raft server.
    class Network {
    public:
        virtual ~Network() = default;

        /// Starts the Raft server.
        /// @return Success or an error.
        virtual tl::expected<void, Error> start() = 0;

        /// Stops the Raft server.
        /// @return Success or an error.
        virtual tl::expected<void, Error> stop() = 0;
    };

    /// Creates a new Raft server with the given configuration.
    /// Note that on creation, the server will attempt to read its state from the persister.
    /// For any
    /// @param config The configuration for the server.
    /// @return A shared pointer to the server or an error.
    tl::expected<std::shared_ptr<Server>, Error> createServer(const ServerCreateConfig &config);

    /// Creates a new Raft network with the given configuration.
    /// Internally, this uses gRPC.
    /// @param config The configuration for the network.
    /// @return A shared pointer to the network or an error.
    tl::expected<std::shared_ptr<Network>, Error> createNetwork(const NetworkCreateConfig &config);
} // namespace raft
