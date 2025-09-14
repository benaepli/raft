#pragma once

#include "../errors.hpp"
#include "cli_protos/kv_store.grpc.pb.h"

namespace raft_cli::errors
{
    template<class... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };
    template<class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    inline Error fromGrpcStatus(const grpc::Status& status)
    {
        cli_protos::ErrorDetails details;
        if (details.ParseFromString(status.error_details()))
        {
            switch (details.error_case())
            {
                case cli_protos::ErrorDetails::kNotLeaderError:
                {
                    auto notLeader = details.not_leader_error();
                    auto result = NotLeader {};
                    if (notLeader.has_leader_address())
                    {
                        result.leaderAddress = notLeader.leader_address();
                    }
                    return result;
                }
                default:
                    return Unknown {.message = status.error_message()};
            }
        }

        switch (status.error_code())
        {
            default:
                return Unknown {.message = status.error_message()};
        }
    }

    inline grpc::Status toGrpcStatus(const Error& error)
    {
        return std::visit(
            overloaded {[](ConfigError const& e)
                        { return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.message); },
                        [](NotLeader const& e)
                        {
                            cli_protos::ErrorDetails details;
                            auto* notLeaderError = details.mutable_not_leader_error();
                            if (e.leaderAddress.has_value())
                            {
                                notLeaderError->set_leader_address(*e.leaderAddress);
                            }

                            std::string serializedDetails;
                            details.SerializeToString(&serializedDetails);

                            grpc::Status status(grpc::StatusCode::FAILED_PRECONDITION,
                                                "not leader");
                            return grpc::Status(
                                status.error_code(), status.error_message(), serializedDetails);
                        },
                        [](Unknown const& e)
                        { return grpc::Status(grpc::StatusCode::UNKNOWN, e.message); }},
            error);
    }
}  // namespace raft_cli::errors