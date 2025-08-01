#pragma once

#include <memory>
#include <optional>

#include "raft/client.hpp"
#include "raft/server.hpp"

namespace raft
{
    /// The network interface for the Raft server.
    class Network
    {
      public:
        virtual ~Network() = default;

        /// Starts the Raft server.
        /// @param address The address to listen on. For a TCP server, this would be
        /// "127.0.0.1:8080" for localhost on port 8080 or "127.0.0.1:0" to listen on any available
        /// port.
        /// @return The address of the server
        virtual tl::expected<std::string, Error> start(const std::string& address) = 0;

        /// Stops the Raft server.
        /// @return Success or an error.
        virtual tl::expected<void, Error> stop() = 0;
    };

    struct NetworkCreateConfig
    {
        std::shared_ptr<ServiceHandler> handler;
    };

    /// Creates a new Raft network with the given configuration.
    /// Internally, this uses gRPC.
    /// @param config The configuration for the network.
    /// @return A shared pointer to the network or an error.
    tl::expected<std::shared_ptr<Network>, Error> createNetwork(const NetworkCreateConfig& config);
}  // namespace raft