#include "serialize.hpp"

#include <spdlog/spdlog.h>

#include "raft_protos/raft.pb.h"
#include "utils/grpc_data.hpp"

namespace raft::data
{
    std::vector<std::byte> serialize(PersistedState const& state)
    {
        raft_protos::PersistedState protoState = toProto(state);
        std::string serializedState;
        bool success = protoState.SerializeToString(&serializedState);
        if (!success)
        {
            // Should never happen.
            spdlog::error("Failed to serialize state");
        }
        return toBytes(serializedState);
    }

    tl::expected<PersistedState, Error> deserialize(std::vector<std::byte> data)
    {
        raft_protos::PersistedState protoState;
        bool success = protoState.ParseFromArray(data.data(), static_cast<int>(data.size()));
        if (!success)
        {
            return tl::make_unexpected(errors::Deserialization {});
        }
        return fromProto(protoState);
    }
}  // namespace raft::data