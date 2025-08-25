#include <variant>

#include "serialize.hpp"

#include <spdlog/spdlog.h>

#include "raft_protos/enhanced.pb.h"

namespace raft::enhanced
{
    namespace
    {
        std::vector<std::byte> toBytes(const std::string& str)
        {
            std::vector<std::byte> bytes(str.size());
            std::memcpy(bytes.data(), str.c_str(), str.size());
            return bytes;
        }

        std::string toString(const std::vector<std::byte>& bytes)
        {
            return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
        }

        enhanced_protos::Entry toProto(Entry const& entry)
        {
            enhanced_protos::Entry proto;
            proto.set_client_id(entry.clientID);
            proto.set_request_id(static_cast<int64_t>(entry.requestID));
            proto.set_data(toString(entry.data));
            return proto;
        }

        Entry fromProto(enhanced_protos::Entry const& proto)
        {
            Entry entry;
            entry.clientID = proto.client_id();
            entry.requestID = static_cast<uint64_t>(proto.request_id());
            entry.data = toBytes(proto.data());
            return entry;
        }

        enhanced_protos::EndSession toProto(EndSession const& endSession)
        {
            enhanced_protos::EndSession proto;
            proto.set_client_id(endSession.clientID);
            return proto;
        }

        EndSession fromProto(enhanced_protos::EndSession const& proto)
        {
            EndSession endSession;
            endSession.clientID = proto.client_id();
            return endSession;
        }
    }  // namespace

    std::vector<std::byte> serialize(Entry const& entry)
    {
        enhanced_protos::Entry protoEntry = toProto(entry);
        std::string serializedEntry;
        bool success = protoEntry.SerializeToString(&serializedEntry);
        if (!success)
        {
            spdlog::error("failed to serialize enhanced Entry");
        }
        return toBytes(serializedEntry);
    }

    std::vector<std::byte> serialize(EndSession const& endSession)
    {
        enhanced_protos::EndSession protoEndSession = toProto(endSession);
        std::string serializedEndSession;
        bool success = protoEndSession.SerializeToString(&serializedEndSession);
        if (!success)
        {
            spdlog::error("failed to serialize enhanced EndSession");
        }
        return toBytes(serializedEndSession);
    }

    tl::expected<std::variant<Entry, EndSession>, Error> deserialize(
        std::vector<std::byte> const& data)
    {
        enhanced_protos::Entry protoEntry;
        bool entrySuccess = protoEntry.ParseFromArray(data.data(), static_cast<int>(data.size()));
        if (entrySuccess)
        {
            return fromProto(protoEntry);
        }

        enhanced_protos::EndSession protoEndSession;
        bool endSessionSuccess =
            protoEndSession.ParseFromArray(data.data(), static_cast<int>(data.size()));
        if (endSessionSuccess)
        {
            return fromProto(protoEndSession);
        }

        return tl::make_unexpected(errors::Deserialization {});
    }
}  // namespace raft::enhanced