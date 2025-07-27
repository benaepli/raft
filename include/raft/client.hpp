#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>
#include <tl/expected.hpp>

namespace raft {
    // The default timeout for requests in milliseconds.
    constexpr uint64_t DEFAULT_TIMEOUT_MS = 1000;

    namespace errors {
        struct Timeout {
        };

        struct Unimplemented {
        };

        struct InvalidArgument {
            std::string message;
        };
    } // namespace errors

    using Error = std::variant<
        errors::Timeout,
        errors::Unimplemented,
        errors::InvalidArgument
    >;

    namespace data {
        // LogEntry represents a single log entry in the Raft log.
        struct LogEntry {
            // The term of the log entry.
            int64_t term;
            // The data contained in the log entry.
            std::string data;
        };

        // The request message for AppendEntries.
        struct AppendEntriesRequest {
            // The current term.
            int64_t term;
            // The leader's ID.
            std::string leaderID;
            // The index of the log entry immediately preceding the new ones.
            int64_t prevLogIndex;
            // The term of the log entry at prev_log_index.
            int64_t prevLogTerm;
            // The log entries to store. This may be empty for a heartbeat.
            std::vector<LogEntry> entries;
            // The leader's commit index.
            int64_t leaderCommit;
        };

        // The reply message for AppendEntries.
        struct AppendEntriesResponse {
            // The current term.
            int64_t term;
            // True if the follower contained the entry matching prevLogIndex and prevLogTerm.
            bool success;
        };

        // The request message for RequestVote.
        struct RequestVoteRequest {
            // The current term.
            int64_t term;
            // The candidate's ID.
            std::string candidateID;
            // The index of the candidate's last log entry.
            int64_t lastLogIndex;
            // The term of the candidate's last log entry.
            int64_t lastLogTerm;
        };

        // The reply message for RequestVote.
        struct RequestVoteResponse {
            // The current term.
            int64_t term;
            // True if the candidate received a vote.
            bool voteGranted;
        };
    } // namespace data

    // RequestConfig defines the configuration for a
    struct RequestConfig {
        // The timeout in milliseconds for the request.
        uint64_t timeout = DEFAULT_TIMEOUT_MS;
    };


    // Client is an interface for a Raft client that can send AppendEntries and RequestVote requests.
    class Client {
    public:
        virtual ~Client() = default;

        virtual tl::expected<data::AppendEntriesResponse, Error> appendEntries(
            const data::AppendEntriesRequest &request,
            const RequestConfig &config
        ) = 0;

        virtual tl::expected<data::RequestVoteResponse, Error> requestVote(
            const data::RequestVoteRequest &request,
            const RequestConfig &config
        ) = 0;
    };

    // createClient creates a new Raft client that connects to the specified address.
    // The address should be in the format "host:port". This uses gRPC to connect to the server.
    tl::expected<std::unique_ptr<Client>, Error> createClient(const std::string &address);
} // namespace raft
