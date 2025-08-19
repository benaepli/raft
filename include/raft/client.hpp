#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <tl/expected.hpp>

#include "raft/errors.hpp"

namespace raft
{
    /// The default timeout for requests in milliseconds.
    constexpr uint64_t DEFAULT_TIMEOUT_MS = 1000;

    namespace data
    {
        /// NoOp is an empty struct that denotes a no-op log entry.
        struct NoOp
        {
            bool operator==(const NoOp& other) const = default;
        };

        /// LogEntry represents a single log entry in the Raft log.
        struct LogEntry
        {
            uint64_t term;  ///< The term of the log entry.
            std::variant<std::vector<std::byte>, NoOp> entry;  ///< The entry data or no-op.

            bool operator==(const LogEntry& other) const = default;
        };

        /// The request message for AppendEntries.
        struct AppendEntriesRequest
        {
            uint64_t term;  ///< The current term.
            std::string leaderID;  ///< The leader's ID.
            uint64_t
                prevLogIndex;  ///< The index of the log entry immediately preceding the new ones.
            uint64_t prevLogTerm;  ///< The term of the log entry at prev_log_index.
            std::vector<LogEntry>
                entries;  ///< The log entries to store. This may be empty for a heartbeat.
            uint64_t leaderCommit;  ///< The leader's commit index.

            bool operator==(const AppendEntriesRequest& other) const = default;
        };

        /// The reply message for AppendEntries.
        struct AppendEntriesResponse
        {
            uint64_t term;  ///< The current term.
            bool success;  ///< True if the follower contained the entry matching prevLogIndex and
                           ///< prevLogTerm.

            bool operator==(const AppendEntriesResponse& other) const = default;
        };

        /// The request message for RequestVote.
        struct RequestVoteRequest
        {
            uint64_t term;  ///< The current term.
            std::string candidateID;  ///< The candidate's ID.
            uint64_t lastLogIndex;  ///< The index of the candidate's last log entry.
            uint64_t lastLogTerm;  ///< The term of the candidate's last log entry.

            bool operator==(const RequestVoteRequest& other) const = default;
        };

        /// The reply message for RequestVote.
        struct RequestVoteResponse
        {
            uint64_t term;  ///< The current term.
            bool voteGranted;  ///< True if the candidate received a vote.

            bool operator==(const RequestVoteResponse& other) const = default;
        };
    }  // namespace data

    /// RequestConfig defines the configuration for a request.
    struct RequestConfig
    {
        uint64_t timeout = DEFAULT_TIMEOUT_MS;  ///< The timeout in milliseconds for the request.
    };

    /// Client is an interface for a Raft client that can send AppendEntries and RequestVote
    /// requests.
    class Client
    {
      public:
        virtual ~Client() = default;

        /// Send an AppendEntries request to the server.
        /// @param request The AppendEntries request to send.
        /// @param config The configuration for the request.
        /// @param callback The callback to invoke with the response or error.
        virtual void appendEntries(
            data::AppendEntriesRequest request,
            RequestConfig config,
            std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback) = 0;

        /// Send a RequestVote request to the server.
        /// @param request The RequestVote request to send.
        /// @param config The configuration for the request.
        /// @param callback The callback to invoke with the response or error.
        virtual void requestVote(
            data::RequestVoteRequest request,
            RequestConfig config,
            std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback) = 0;
    };

    /// Creates a new Raft client that connects to the specified address.
    /// This uses gRPC to connect to the server.
    /// @param address The address to connect to in "host:port" format.
    /// @return A unique pointer to the client or an error.
    tl::expected<std::unique_ptr<Client>, Error> createClient(const std::string& address);

    /// Abstract factory interface for creating Raft clients.
    /// Implementations of this interface provide a way to create Client instances
    /// for connecting to Raft servers at specific addresses.
    class ClientFactory
    {
      public:
        virtual ~ClientFactory() = default;

        /// Creates a new Raft client that connects to the specified address.
        /// @param address The address to connect to in "host:port" format.
        /// @return A unique pointer to the client or an error if creation fails.
        virtual tl::expected<std::unique_ptr<Client>, Error> createClient(
            const std::string& address) = 0;
    };

    /// Creates a default ClientFactory implementation that uses gRPC for communication.
    /// @return A shared pointer to a ClientFactory instance.
    std::shared_ptr<ClientFactory> createClientFactory();
}  // namespace raft
