#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <tl/expected.hpp>

#include "raft/errors.hpp"

namespace raft::enhanced
{
    /// Entry represents a log entry's data with deduplication information.
    struct Entry
    {
        std::string clientID;  ///< The client ID for this log entry.
        uint64_t requestID;  ///< The request ID for this log entry.
        std::vector<std::byte> data;  ///< The data contained in the log entry.
    };

    /// Serializes an Entry to bytes using protobuf format.
    /// @param entry The Entry to serialize.
    /// @return The serialized bytes.
    std::vector<std::byte> serialize(Entry const& entry);

    /// Deserializes bytes to an Entry using protobuf format.
    /// @param data The serialized bytes.
    /// @return The deserialized Entry or an error.
    tl::expected<Entry, Error> deserialize(std::vector<std::byte> const& data);
}  // namespace raft::enhanced