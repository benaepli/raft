#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace raft::data
{
    /// NoOp is an empty struct that denotes a no-op log entry.
    struct NoOp
    {
        bool operator==(NoOp const& other) const = default;
    };

    /// LogEntry represents a single log entry in the Raft log.
    struct LogEntry
    {
        uint64_t term;  ///< The term of the log entry.
        std::variant<std::vector<std::byte>, NoOp> entry;  ///< The entry data or no-op.

        bool operator==(LogEntry const& other) const = default;
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

        bool operator==(AppendEntriesRequest const& other) const = default;
    };

    /// The reply message for AppendEntries.
    struct AppendEntriesResponse
    {
        uint64_t term;  ///< The current term.
        bool success;  ///< True if the follower contained the entry matching prevLogIndex and
                       ///< prevLogTerm.

        bool operator==(AppendEntriesResponse const& other) const = default;
    };

    /// The request message for RequestVote.
    struct RequestVoteRequest
    {
        uint64_t term;  ///< The current term.
        std::string candidateID;  ///< The candidate's ID.
        uint64_t lastLogIndex;  ///< The index of the candidate's last log entry.
        uint64_t lastLogTerm;  ///< The term of the candidate's last log entry.

        bool operator==(RequestVoteRequest const& other) const = default;
    };

    /// The reply message for RequestVote.
    struct RequestVoteResponse
    {
        uint64_t term;  ///< The current term.
        bool voteGranted;  ///< True if the candidate received a vote.

        bool operator==(RequestVoteResponse const& other) const = default;
    };
}  // namespace raft::data