#pragma once
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include <tl/expected.hpp>

#include "../errors.hpp"

namespace raft_cli::config
{
    constexpr std::chrono::nanoseconds DEFAULT_ELECTION_TIMEOUT = std::chrono::milliseconds(1000);
    constexpr std::chrono::nanoseconds DEFAULT_HEARTBEAT_INTERVAL = std::chrono::milliseconds(100);

    struct Node
    {
        std::string id;
        std::string address;
        uint16_t kvPort;
        uint16_t raftPort;
    };

    struct Settings
    {
        std::chrono::nanoseconds electionTimeout;
        std::chrono::nanoseconds heartbeatInterval;
        std::string dataDirectory;
    };

    struct Config
    {
        Settings settings;
        std::vector<Node> nodes;
    };

    tl::expected<Config, Error> loadConfig(std::string_view path);

}  // namespace raft_cli::config