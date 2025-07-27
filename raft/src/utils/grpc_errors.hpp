#pragma once

#include <grpcpp/grpcpp.h>
#include "raft/client.hpp"

namespace raft::errors {
    inline Error fromGrpcStatus(const grpc::Status &status) {
        switch (status.error_code()) {
            case grpc::StatusCode::DEADLINE_EXCEEDED:
            case grpc::StatusCode::UNAVAILABLE:
            case grpc::StatusCode::CANCELLED:
                return Timeout{};
            
            case grpc::StatusCode::UNIMPLEMENTED:
                return Unimplemented{};
            
            case grpc::StatusCode::INVALID_ARGUMENT:
            case grpc::StatusCode::OUT_OF_RANGE:
            case grpc::StatusCode::FAILED_PRECONDITION:
                return InvalidArgument{.message = status.error_message()};
            
            default:
                return InvalidArgument{.message = status.error_message()};
        }
    }
} // namespace raft::errors