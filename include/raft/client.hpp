#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include <asio/awaitable.hpp>
#include <tl/expected.hpp>

#include "raft/errors.hpp"

namespace raft
{
    /// The default timeout for requests in milliseconds.
    constexpr uint64_t DEFAULT_TIMEOUT_MS = 1000;

    namespace data
    {
        /// LogEntry represents a single log entry in the Raft log.
        struct LogEntry
        {
            int64_t term;  ///< The term of the log entry.
            std::vector<std::byte> data;  ///< The data contained in the log entry.
        };

        /// The request message for AppendEntries.
        struct AppendEntriesRequest
        {
            int64_t term;  ///< The current term.
            std::string leaderID;  ///< The leader's ID.
            int64_t
                prevLogIndex;  ///< The index of the log entry immediately preceding the new ones.
            int64_t prevLogTerm;  ///< The term of the log entry at prev_log_index.
            std::vector<LogEntry>
                entries;  ///< The log entries to store. This may be empty for a heartbeat.
            int64_t leaderCommit;  ///< The leader's commit index.
        };

        /// The reply message for AppendEntries.
        struct AppendEntriesResponse
        {
            int64_t term;  ///< The current term.
            bool success;  ///< True if the follower contained the entry matching prevLogIndex and
                           ///< prevLogTerm.
        };

        /// The request message for RequestVote.
        struct RequestVoteRequest
        {
            int64_t term;  ///< The current term.
            std::string candidateID;  ///< The candidate's ID.
            int64_t lastLogIndex;  ///< The index of the candidate's last log entry.
            int64_t lastLogTerm;  ///< The term of the candidate's last log entry.
        };

        /// The reply message for RequestVote.
        struct RequestVoteResponse
        {
            int64_t term;  ///< The current term.
            bool voteGranted;  ///< True if the candidate received a vote.
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
        /// @return The AppendEntries response or an error.
        virtual tl::expected<data::AppendEntriesResponse, Error> appendEntries(
            const data::AppendEntriesRequest& request, const RequestConfig& config) = 0;

        /// Send a RequestVote request to the server.
        /// @param request The RequestVote request to send.
        /// @param config The configuration for the request.
        /// @return The RequestVote response or an error.
        virtual tl::expected<data::RequestVoteResponse, Error> requestVote(
            const data::RequestVoteRequest& request, const RequestConfig& config) = 0;
    };

    /// Creates a new Raft client that connects to the specified address.
    /// This uses gRPC to connect to the server.
    /// @param address The address to connect to in "host:port" format.
    /// @return A unique pointer to the client or an error.
    tl::expected<std::unique_ptr<Client>, Error> createClient(const std::string& address);

    class ClientFactory
    {
      public:
        virtual ~ClientFactory() = default;

        virtual tl::expected<std::unique_ptr<Client>, Error> createClient(
            const std::string& address) = 0;
    };

    std::unique_ptr<ClientFactory> createClientFactory();
}  // namespace raft
