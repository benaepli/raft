#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "../store/data.hpp"
#include "cli_protos/kv_store.pb.h"

namespace raft_cli::store::data
{
    inline std::vector<std::byte> toBytes(const std::string& str)
    {
        std::vector<std::byte> bytes(str.size());
        std::memcpy(bytes.data(), str.c_str(), str.size());
        return bytes;
    }

    inline std::string toString(const std::vector<std::byte>& bytes)
    {
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    // RequestInfo conversions
    inline cli_protos::RequestInfo toProto(RequestInfo const& requestInfo)
    {
        cli_protos::RequestInfo proto;
        proto.set_clientid(requestInfo.clientID);
        proto.set_requestid(requestInfo.requestID);
        return proto;
    }

    inline RequestInfo fromProto(const cli_protos::RequestInfo& proto)
    {
        return RequestInfo {
            .clientID = proto.clientid(),
            .requestID = proto.requestid(),
        };
    }

    // GetRequest conversions
    inline cli_protos::GetRequest toProto(GetRequest const& request)
    {
        cli_protos::GetRequest proto;
        proto.set_key(request.key);
        return proto;
    }

    inline GetRequest fromProto(const cli_protos::GetRequest& proto)
    {
        return GetRequest {
            .key = proto.key(),
        };
    }

    // GetResponse conversions
    inline cli_protos::GetResponse toProto(GetResponse const& response)
    {
        cli_protos::GetResponse proto;
        proto.set_value(toString(response.value));
        proto.set_found(response.found);
        return proto;
    }

    inline GetResponse fromProto(const cli_protos::GetResponse& proto)
    {
        return GetResponse {
            .value = toBytes(proto.value()),
            .found = proto.found(),
        };
    }

    // PutRequest conversions
    inline cli_protos::PutRequest toProto(PutRequest const& request)
    {
        cli_protos::PutRequest proto;
        proto.set_key(request.key);
        proto.set_value(toString(request.value));
        if (request.requestInfo.has_value())
        {
            *proto.mutable_request_info() = toProto(*request.requestInfo);
        }
        return proto;
    }

    inline PutRequest fromProto(const cli_protos::PutRequest& proto)
    {
        PutRequest request {
            .key = proto.key(),
            .value = toBytes(proto.value()),
        };
        if (proto.has_request_info())
        {
            request.requestInfo = fromProto(proto.request_info());
        }
        return request;
    }

    // PutResponse conversions
    inline cli_protos::PutResponse toProto(PutResponse const& response)
    {
        cli_protos::PutResponse proto;
        proto.set_duplicate(response.duplicate);
        return proto;
    }

    inline PutResponse fromProto(const cli_protos::PutResponse& proto)
    {
        return PutResponse {
            .duplicate = proto.duplicate(),
        };
    }

    // DeleteRequest conversions
    inline cli_protos::DeleteRequest toProto(DeleteRequest const& request)
    {
        cli_protos::DeleteRequest proto;
        proto.set_key(request.key);
        if (request.requestInfo.has_value())
        {
            *proto.mutable_request_info() = toProto(*request.requestInfo);
        }
        return proto;
    }

    inline DeleteRequest fromProto(const cli_protos::DeleteRequest& proto)
    {
        DeleteRequest request {
            .key = proto.key(),
        };
        if (proto.has_request_info())
        {
            request.requestInfo = fromProto(proto.request_info());
        }
        return request;
    }

    // DeleteResponse conversions
    inline cli_protos::DeleteResponse toProto(DeleteResponse const& response)
    {
        cli_protos::DeleteResponse proto;
        proto.set_deleted(response.deleted);
        proto.set_duplicate(response.duplicate);
        return proto;
    }

    inline DeleteResponse fromProto(const cli_protos::DeleteResponse& proto)
    {
        return DeleteResponse {
            .deleted = proto.deleted(),
            .duplicate = proto.duplicate(),
        };
    }

    // EndSessionRequest conversions
    inline cli_protos::EndSessionRequest toProto(EndSessionRequest const& request)
    {
        cli_protos::EndSessionRequest proto;
        proto.set_clientid(request.clientID);
        return proto;
    }

    inline EndSessionRequest fromProto(const cli_protos::EndSessionRequest& proto)
    {
        return EndSessionRequest {
            .clientID = proto.clientid(),
        };
    }

    // EndSessionResponse conversions
    inline cli_protos::EndSessionResponse toProto(EndSessionResponse const& response)
    {
        cli_protos::EndSessionResponse proto;
        return proto;
    }

    inline EndSessionResponse fromProto(const cli_protos::EndSessionResponse& proto)
    {
        return EndSessionResponse {};
    }

}  // namespace raft_cli::store::data