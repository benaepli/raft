#pragma once

#include <grpcpp/grpcpp.h>

#include "raft/client.hpp"

namespace raft::errors
{
    // Helper for std::visit with multiple lambdas
    template<class... Ts>
    struct overloaded : Ts...
    {
        using Ts::operator()...;
    };
    template<class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

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
                return Unknown {.message = status.error_message()};
        }
    }

    // This attempts to match an error to a gRPC status code.
    inline grpc::Status toGrpcStatus(const Error& error)
    {
        return std::visit(
            overloaded {
                [](const Unknown& e) { return grpc::Status(grpc::StatusCode::UNKNOWN, e.message); },
                [](const Timeout&)
                { return grpc::Status(grpc::StatusCode::DEADLINE_EXCEEDED, "Timeout occurred"); },
                [](const Unimplemented&)
                { return grpc::Status(grpc::StatusCode::UNIMPLEMENTED, "Not implemented"); },
                [](const InvalidArgument& e)
                { return grpc::Status(grpc::StatusCode::INVALID_ARGUMENT, e.message); },
                [](const NotLeader&)
                { return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Not leader"); },
                [](const AlreadyRunning&)
                { return grpc::Status(grpc::StatusCode::ALREADY_EXISTS, "Already running"); },
                [](const NotRunning&)
                { return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Not running"); },
                [](const FailedToStart&)
                { return grpc::Status(grpc::StatusCode::INTERNAL, "Failed to start"); },
                [](const Deserialization&)
                { return grpc::Status(grpc::StatusCode::DATA_LOSS, "Deserialization failed"); },
                [](const UnknownLeader&)
                { return grpc::Status(grpc::StatusCode::FAILED_PRECONDITION, "Unknown leader"); },
                [](const NonexistentNetwork&)
                { return grpc::Status(grpc::StatusCode::NOT_FOUND, "Network does not exist"); }},
            error);
    }
}  // namespace raft::errors