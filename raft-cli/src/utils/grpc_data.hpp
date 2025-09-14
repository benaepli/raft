#pragma once

#include <cstring>
#include <string>
#include <vector>

#include "../store/data.hpp"
#include "cli_protos/kv_store.pb.h"

namespace raft_cli::store::data
{
    inline std::vector<std::byte> toBytes(std::string const& str)
    {
        std::vector<std::byte> bytes(str.size());
        std::memcpy(bytes.data(), str.c_str(), str.size());
        return bytes;
    }

    inline std::string toString(std::vector<std::byte> const& bytes)
    {
        return {reinterpret_cast<char const*>(bytes.data()), bytes.size()};
    }

    // RequestInfo conversions
    inline cli_protos::RequestInfo toProto(RequestInfo const& requestInfo)
    {
        cli_protos::RequestInfo proto;
        proto.set_clientid(requestInfo.clientID);
        proto.set_requestid(requestInfo.requestID);
        return proto;
    }

    inline RequestInfo fromProto(cli_protos::RequestInfo const& proto)
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

    inline GetRequest fromProto(cli_protos::GetRequest const& proto)
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

    inline GetResponse fromProto(cli_protos::GetResponse const& proto)
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

    inline PutRequest fromProto(cli_protos::PutRequest const& proto)
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

    inline PutResponse fromProto(cli_protos::PutResponse const& proto)
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

    inline DeleteRequest fromProto(cli_protos::DeleteRequest const& proto)
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

    inline DeleteResponse fromProto(cli_protos::DeleteResponse const& proto)
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

    inline EndSessionRequest fromProto(cli_protos::EndSessionRequest const& proto)
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

    inline EndSessionResponse fromProto(cli_protos::EndSessionResponse const& proto)
    {
        return EndSessionResponse {};
    }

    // PutCommand conversions
    inline cli_protos::PutCommand toProto(PutCommand const& command)
    {
        cli_protos::PutCommand proto;
        proto.set_key(command.key);
        proto.set_value(toString(command.value));
        return proto;
    }

    inline PutCommand fromProto(cli_protos::PutCommand const& proto)
    {
        return PutCommand {
            .key = proto.key(),
            .value = toBytes(proto.value()),
        };
    }

    // DeleteCommand conversions
    inline cli_protos::DeleteCommand toProto(DeleteCommand const& command)
    {
        cli_protos::DeleteCommand proto;
        proto.set_key(command.key);
        return proto;
    }

    inline DeleteCommand fromProto(cli_protos::DeleteCommand const& proto)
    {
        return DeleteCommand {
            .key = proto.key(),
        };
    }

    // EmptyCommand conversions
    inline cli_protos::EmptyCommand toProto(EmptyCommand const& command)
    {
        cli_protos::EmptyCommand proto;
        return proto;
    }

    inline EmptyCommand fromProto(cli_protos::EmptyCommand const& proto)
    {
        return EmptyCommand {};
    }

    // Command conversions
    inline cli_protos::Command toProto(Command const& command)
    {
        cli_protos::Command proto;

        std::visit(
            [&proto](auto const& cmd)
            {
                using T = std::decay_t<decltype(cmd)>;
                if constexpr (std::is_same_v<T, PutCommand>)
                {
                    *proto.mutable_put() = toProto(cmd);
                }
                else if constexpr (std::is_same_v<T, DeleteCommand>)
                {
                    *proto.mutable_delete_() = toProto(cmd);
                }
                else if constexpr (std::is_same_v<T, EmptyCommand>)
                {
                    *proto.mutable_empty() = toProto(cmd);
                }
            },
            command);

        return proto;
    }

    inline Command fromProto(cli_protos::Command const& proto)
    {
        switch (proto.command_case())
        {
            case cli_protos::Command::kPut:
                return fromProto(proto.put());
            case cli_protos::Command::kDelete:
                return fromProto(proto.delete_());
            case cli_protos::Command::kEmpty:
                return fromProto(proto.empty());
            case cli_protos::Command::COMMAND_NOT_SET:
                return EmptyCommand {};
        }
        return EmptyCommand {};
    }

    inline std::vector<std::byte> toBytes(cli_protos::Command const& command)
    {
        auto str = command.SerializeAsString();
        return toBytes(str);
    }

}  // namespace raft_cli::store::data