#pragma once

#include <grpcpp/grpcpp.h>

#include "raft/client.hpp"

namespace raft::errors
{
    inline Error fromGrpcStatus(const grpc::Status& status)
    {
        switch (status.error_code())
        {
            case grpc::StatusCode::DEADLINE_EXCEEDED:
            case grpc::StatusCode::UNAVAILABLE:
            case grpc::StatusCode::CANCELLED:
                return Timeout {};

            case grpc::StatusCode::UNIMPLEMENTED:
                return Unimplemented {};

            case grpc::StatusCode::INVALID_ARGUMENT:
            case grpc::StatusCode::OUT_OF_RANGE:
            case grpc::StatusCode::FAILED_PRECONDITION:
                return InvalidArgument {.message = status.error_message()};

            default:
                return InvalidArgument {.message = status.error_message()};
        }
    }

    // This attempts to match an error to a gRPC status code.
    inline grpc::Status toGrpcStatus(const Error& error)
    {
        return std::visit(
            [](const auto& e) -> grpc::Status
            {
                using T = std::decay_t<decltype(e)>;

                if constexpr (std::is_same_v<T, Timeout>)
                {
                    return grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED, "Timeout occurred");
                }
                else if constexpr (std::is_same_v<T, Unimplemented>)
                {
                    return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Not implemented");
                }
                else if constexpr (std::is_same_v<T, InvalidArgument>)
                {
                    return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.message);
                }
                else if constexpr (std::is_same_v<T, NotLeader>)
                {
                    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Not leader");
                }
                else if constexpr (std::is_same_v<T, AlreadyRunning>)
                {
                    return grpc::Status(grpc::StatusCode::ALREADY_EXISTS, "Already running");
                }
                else if constexpr (std::is_same_v<T, NotRunning>)
                {
                    return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Not running");
                }
                else if constexpr (std::is_same_v<T, FailedToStart>)
                {
                    return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start");
                }
                else
                {
                    return grpc::Status(grpc::StatusCode::UNKNOWN, "Unknown error");
                }
            },
            error);
    }
}  // namespace raft::errors