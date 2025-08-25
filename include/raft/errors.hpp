#pragma once

#include <ostream>
#include <string>
#include <variant>

namespace raft
{
    namespace errors
    {
        /// An unknown error.
        struct Unknown
        {
            std::string message;  ///< The error message.
        };

        /// A timeout has occurred.
        struct Timeout
        {
        };

        /// An unimplemented error.
        struct Unimplemented
        {
        };

        /// An invalid argument error.
        struct InvalidArgument
        {
            std::string message;  ///< The error message.
        };

        /// The replica is not the leader.
        struct NotLeader
        {
        };

        /// The network interface or server is already running.
        struct AlreadyRunning
        {
        };

        /// The network interface is not running.
        struct NotRunning
        {
        };

        /// The server failed to start.
        struct FailedToStart
        {
        };

        /// Deserialization error.
        struct Deserialization
        {
        };

        /// The leader is unknown or does not exist.
        struct UnknownLeader
        {
        };

        /// The network does not exist.
        struct NonexistentNetwork
        {
        };

        /// No persisted state is available.
        struct NoPersistedState
        {
        };

        /// Persistence operation failed.
        struct PersistenceFailed
        {
            std::string message;  ///< The error message.
        };
    }  // namespace errors

    using Error = std::variant<errors::Unknown,
                               errors::Timeout,
                               errors::Unimplemented,
                               errors::InvalidArgument,
                               errors::NotLeader,
                               errors::AlreadyRunning,
                               errors::NotRunning,
                               errors::FailedToStart,
                               errors::Deserialization,
                               errors::UnknownLeader,
                               errors::NonexistentNetwork,
                               errors::NoPersistedState,
                               errors::PersistenceFailed>;

}  // namespace raft
