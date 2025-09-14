#pragma once

#include <string>
#include <variant>

#include <fmt/core.h>
#include <fmt/std.h>

#include "raft/errors.hpp"
#include "raft/fmt/errors.hpp"

namespace raft_cli
{
    namespace errors
    {
        struct ConfigError
        {
            std::string message;
        };

        struct NotLeader
        {
            std::optional<std::string> leaderAddress;
        };

        using raft::errors::Unknown;

    }  // namespace errors

    using Error = std::variant<errors::ConfigError, errors::NotLeader, errors::Unknown>;
}  // namespace raft_cli

template<>
struct fmt::formatter<raft_cli::errors::ConfigError>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(raft_cli::errors::ConfigError const& err, FormatContext& ctx) const
    {
        return fmt::format_to(ctx.out(), "config error: {}", err.message);
    }
};

template<>
struct fmt::formatter<raft_cli::errors::NotLeader>
{
    constexpr auto parse(format_parse_context& ctx) { return ctx.begin(); }

    template<typename FormatContext>
    auto format(raft_cli::errors::NotLeader const& err, FormatContext& ctx) const
    {
        return fmt::format_to(
            ctx.out(), "not leader, leader address: {}", err.leaderAddress.value_or("unknown"));
    }
};