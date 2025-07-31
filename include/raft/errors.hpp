#pragma once

#include <string>
#include <variant>

namespace raft {
    namespace errors {
        /// A timeout has occurred.
        struct Timeout {
        };

        /// An unimplemented error.
        struct Unimplemented {
        };

        /// An invalid argument error.
        struct InvalidArgument {
            std::string message; ///< The error message.
        };

        /// The replica is not the leader.
        struct NotLeader {
        };

        /// The network interface is already running.
        struct AlreadyRunning {
        };

        /// The network interface is not running.
        struct NotRunning {
        };

        /// The server failed to start.
        struct FailedToStart {
        };
    } // namespace errors

    using Error = std::variant<
        errors::Timeout,
        errors::Unimplemented,
        errors::InvalidArgument,
        errors::NotLeader,
        errors::AlreadyRunning,
        errors::NotRunning,
        errors::FailedToStart
    >;
} // namespace raft
