#pragma once

#include "raft/network.hpp"

namespace raft::inmemory
{
    /// Configuration structure for creating an inmemory network.
    struct NetworkCreateConfig
    {
        std::shared_ptr<ServiceHandler>
            handler;  ///< The service handler for processing Raft requests
    };

    /// Manager provides an in-memory implementation for Raft networking and client creation.
    ///
    /// The Manager coordinates the creation of both networks and clients that can communicate
    /// with each other through shared memory rather than network protocols.
    class Manager : public raft::ClientFactory
    {
      public:
        /// Creates a new in-memory network instance.
        ///
        /// The created network will handle Raft protocol messages (AppendEntries, RequestVote)
        /// through direct method calls rather than network communication.
        ///
        /// @param config Configuration containing the service handler for processing requests
        /// @return A shared pointer to the network instance or an error if creation fails
        virtual tl::expected<std::shared_ptr<raft::Network>, raft::Error> createNetwork(
            const NetworkCreateConfig& config) = 0;
    };

    /// Creates a new Manager instance for in-memory Raft operations.
    /// @return A shared pointer to the Manager instance.
    std::shared_ptr<Manager> createManager();
}  // namespace raft::inmemory