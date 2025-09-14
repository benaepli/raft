#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <tl/expected.hpp>

#include "raft/data.hpp"
#include "raft/errors.hpp"

namespace raft
{
    /// The default timeout for requests in milliseconds.
    constexpr uint64_t DEFAULT_TIMEOUT_MS = 1000;

    /// RequestConfig defines the configuration for a request.
    struct RequestConfig
    {
        uint64_t timeout = DEFAULT_TIMEOUT_MS;  ///< The timeout in milliseconds for the request.
    };

    /// Client is an interface for a Raft client that can send AppendEntries and RequestVote
    /// requests.
    class Client
    {
      public:
        virtual ~Client() = default;

        /// Send an AppendEntries request to the server.
        /// @param request The AppendEntries request to send.
        /// @param config The configuration for the request.
        /// @param callback The callback to invoke with the response or error.
        virtual void appendEntries(
            data::AppendEntriesRequest request,
            RequestConfig config,
            std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback) = 0;

        /// Send a RequestVote request to the server.
        /// @param request The RequestVote request to send.
        /// @param config The configuration for the request.
        /// @param callback The callback to invoke with the response or error.
        virtual void requestVote(
            data::RequestVoteRequest request,
            RequestConfig config,
            std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback) = 0;
    };

    /// Creates a new Raft client that connects to the specified address.
    /// This uses gRPC to connect to the server.
    /// @param address The address to connect to in "host:port" format.
    /// @return A unique pointer to the client or an error.
    tl::expected<std::unique_ptr<Client>, Error> createClient(std::string const& address);

    /// Abstract factory interface for creating Raft clients.
    /// Implementations of this interface provide a way to create Client instances
    /// for connecting to Raft servers at specific addresses.
    class ClientFactory
    {
      public:
        virtual ~ClientFactory() = default;

        /// Creates a new Raft client that connects to the specified address.
        /// @param address The address to connect to in "host:port" format.
        /// @return A unique pointer to the client or an error if creation fails.
        virtual tl::expected<std::unique_ptr<Client>, Error> createClient(
            std::string const& address) = 0;
    };

    /// Creates a default ClientFactory implementation that uses gRPC for communication.
    /// @return A shared pointer to a ClientFactory instance.
    std::shared_ptr<ClientFactory> createClientFactory();
}  // namespace raft
