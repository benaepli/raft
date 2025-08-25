#pragma once

#include <fmt/core.h>
#include <fmt/std.h>

#include "raft/errors.hpp"

namespace raft::errors::detail
{
    template<typename T>
    constexpr std::string_view getMessage()
    {
        if constexpr (std::is_same_v<T, Timeout>)
        {
            return "timeout";
        }
        if constexpr (std::is_same_v<T, Unimplemented>)
        {
            return "unimplemented";
        }
        if constexpr (std::is_same_v<T, NotLeader>)
        {
            return "not leader";
        }
        if constexpr (std::is_same_v<T, AlreadyRunning>)
        {
            return "already running";
        }
        if constexpr (std::is_same_v<T, NotRunning>)
        {
            return "not running";
        }
        if constexpr (std::is_same_v<T, FailedToStart>)
        {
            return "failed to start";
        }
        if constexpr (std::is_same_v<T, Deserialization>)
        {
            return "deserialization";
        }
        if constexpr (std::is_same_v<T, UnknownLeader>)
        {
            return "unknown leader";
        }
        if constexpr (std::is_same_v<T, NonexistentNetwork>)
        {
            return "nonexistent network";
        }
        if constexpr (std::is_same_v<T, NoPersistedState>)
        {
            return "no persisted state";
        }
        return "unknown error";
    }

    template<typename T>
    concept SimpleError = std::is_same_v<T, Timeout> || std::is_same_v<T, Unimplemented>
        || std::is_same_v<T, NotLeader> || std::is_same_v<T, AlreadyRunning>
        || std::is_same_v<T, NotRunning> || std::is_same_v<T, FailedToStart>
        || std::is_same_v<T, Deserialization> || std::is_same_v<T, UnknownLeader>
        || std::is_same_v<T, NonexistentNetwork> || std::is_same_v<T, NoPersistedState>;
}  // namespace raft::errors::detail

template<>
struct fmt::formatter<raft::errors::Unknown>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(raft::errors::Unknown const& err, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "unknown error: {}", err.message);
    }
};

template<>
struct fmt::formatter<raft::errors::InvalidArgument>
{
    // No format specifiers needed, so the parse function is simple.
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    // The format function defines the output.
    template<typename FormatContext>
    auto format(raft::errors::InvalidArgument const& err, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "invalid argument: {}", err.message);
    }
};

template<>
struct fmt::formatter<raft::errors::PersistenceFailed>
{
    // No format specifiers needed, so the parse function is simple.
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    // The format function defines the output.
    template<typename FormatContext>
    auto format(raft::errors::PersistenceFailed const& err, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "persistence failed: {}", err.message);
    }
};

template<raft::errors::detail::SimpleError T>
struct fmt::formatter<T>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(T const& err, FormatContext& ctx) const
    {
        (void)err;
        return fmt::format_to(ctx.out(), "{}", raft::errors::detail::getMessage<T>());
    }
};