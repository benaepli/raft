#include "data.hpp"

#include "../utils/grpc_data.hpp"

namespace raft_cli::store::data
{
    std::vector<std::byte> serialize(Command const& command)
    {
        return toBytes(toProto(command));
    }
}  // namespace raft_cli::store::data