#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <variant>

#include <asio/steady_timer.hpp>
#include <fmt/core.h>
#include <fmt/ranges.h>
#include <fmt/std.h>

namespace raft
{
    // LeaderClientInfo contains the state needed to manage the replication of log entries to a
    // single follower.
    struct LeaderClientInfo
    {
        // The index of the next log entry to send to the replica.
        uint64_t nextIndex = 0;
        // The index of the highest log entry known to be replicated.
        uint64_t matchIndex = 0;
        asio::steady_timer heartbeatTimer;  // Timer for sending heartbeats to the replica.
        // The maximum number of log entries to send in a single AppendEntries request.
        // TODO: this will be adjusted dynamically based on the replica's responses.
        uint64_t batchSize = 1;
    };

    struct CandidateInfo
    {
        uint64_t voteCount;  // The number of votes received.
    };

    struct LeaderInfo
    {
        std::unordered_map<std::string, LeaderClientInfo>
            clients;  // The state of each replica by ID.
    };

    struct FollowerInfo
    {
        std::optional<std::string> votedFor;
    };

    using State = std::variant<CandidateInfo, LeaderInfo, FollowerInfo>;
}  // namespace raft

template<>
struct fmt::formatter<raft::LeaderClientInfo>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const raft::LeaderClientInfo& info, FormatContext& ctx) const
    {
        return fmt::format_to(
            ctx.out(),
            "{{nextIndex: {}, matchIndex: {}, heartbeatTimer: {}, batchSize: {}}}",
            info.nextIndex,
            info.matchIndex,
            info.heartbeatTimer.expiry().time_since_epoch().count(),
            info.batchSize);
    }
};

template<>
struct fmt::formatter<raft::CandidateInfo>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const raft::CandidateInfo& info, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "{{voteCount: {}}}", info.voteCount);
    }
};

template<>
struct fmt::formatter<raft::LeaderInfo>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const raft::LeaderInfo& info, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "{{clients: {}}}", info.clients);
    }
};

template<>
struct fmt::formatter<raft::FollowerInfo>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(const raft::FollowerInfo& info, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "{{votedFor: {}}}", info.votedFor);
    }
};
