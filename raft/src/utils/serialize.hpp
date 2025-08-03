#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "raft/client.hpp"

namespace raft::data  // namespace raft::data
{
    // The persisted state of the Raft server.
    struct PersistedState
    {
        uint64_t term;
        std::vector<LogEntry> entries;
        uint64_t commitIndex;
    };

    std::vector<std::byte> serialize(const PersistedState& state);
    tl::expected<PersistedState, Error> deserialize(std::vector<std::byte> data);
}  // namespace raft::data