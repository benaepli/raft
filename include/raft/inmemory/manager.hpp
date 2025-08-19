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

    /// @brief Manager provides an in-memory implementation for Raft networking and client
    /// creation.
    ///
    /// The Manager coordinates the creation of both networks and clients that can communicate
    /// with each other through shared memory rather than network protocols.
    class Manager
    {
      public:
        virtual ~Manager() = default;

        /// Creates a new in-memory network instance.
        ///
        /// The created network will handle Raft protocol messages (AppendEntries, RequestVote)
        /// through direct method calls rather than network communication. Each network will be
        /// attached to the fabric upon calling the network's start() method.
        ///
        /// @param config Configuration containing the service handler for processing requests
        /// @return A shared pointer to the network instance or an error if creation fails
        virtual tl::expected<std::shared_ptr<Network>, Error> createNetwork(
            const NetworkCreateConfig& config) = 0;

        virtual tl::expected<std::shared_ptr<ClientFactory>, Error> createClientFactory(
            const std::string& clientAddress) = 0;

        /// Detaches a network from the in-memory fabric, preventing it
        /// from sending or receiving messages.
        /// @param address The address of the node to detach or an error
        virtual tl::expected<void, Error> detachNetwork(const std::string& address) = 0;
        /// Re-attaches a previously detached network node, restoring communication.
        /// @param address The address of the node to attach.
        virtual tl::expected<void, Error> attachNetwork(const std::string& address) = 0;
    };

    /// Creates a new Manager instance for in-memory Raft operations.
    /// @return A shared pointer to the Manager instance.
    std::shared_ptr<Manager> createManager();
}  // namespace raft::inmemory