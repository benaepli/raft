#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <tl/expected.hpp>

namespace raft_cli::store::data
{
    /// RequestInfo contains client identification for request deduplication.
    struct RequestInfo
    {
        std::string
            clientID;  ///< The client ID for this request. Must be unique across all clients.
        uint64_t requestID;  ///< The request ID for this request. Must be monotonically increasing
                             ///< for each client.

        bool operator==(RequestInfo const& other) const = default;
    };

    /// The request message for the Get RPC.
    struct GetRequest
    {
        std::string key;  ///< The key to retrieve.

        bool operator==(GetRequest const& other) const = default;
    };

    /// The response message for the Get RPC.
    struct GetResponse
    {
        std::vector<std::byte>
            value;  ///< The value associated with the key, or empty if the key does not exist.
        bool found;  ///< True if the key was found in the store.

        bool operator==(GetResponse const& other) const = default;
    };

    /// The request message for the Put RPC.
    struct PutRequest
    {
        std::string key;  ///< The key to store.
        std::vector<std::byte> value;  ///< The value to associate with the key.
        std::optional<RequestInfo>
            requestInfo;  ///< Optional request information for client deduplication.

        bool operator==(PutRequest const& other) const = default;
    };

    /// The response message for the Put RPC.
    struct PutResponse
    {
        bool duplicate;  ///< True if this was a duplicate request that was ignored.

        bool operator==(PutResponse const& other) const = default;
    };

    /// The request message for the Delete RPC.
    struct DeleteRequest
    {
        std::string key;  ///< The key to delete.
        std::optional<RequestInfo>
            requestInfo;  ///< Optional request information for client deduplication.

        bool operator==(DeleteRequest const& other) const = default;
    };

    /// The response message for the Delete RPC.
    struct DeleteResponse
    {
        bool deleted;  ///< True if the key existed and was deleted.
        bool duplicate;  ///< True if this was a duplicate request that was ignored.

        bool operator==(DeleteResponse const& other) const = default;
    };

    /// The request message for the EndSession RPC.
    struct EndSessionRequest
    {
        std::string
            clientID;  ///< The client ID for this request. Must be unique across all clients.

        bool operator==(EndSessionRequest const& other) const = default;
    };

    /// The response message for the EndSession RPC.
    struct EndSessionResponse
    {
        bool operator==(EndSessionResponse const& other) const = default;
    };

    /// Command for putting a key-value pair.
    struct PutCommand
    {
        std::string key;  ///< The key to store.
        std::vector<std::byte> value;  ///< The value to associate with the key.

        bool operator==(PutCommand const& other) const = default;
    };

    /// Command for deleting a key.
    struct DeleteCommand
    {
        std::string key;  ///< The key to delete.

        bool operator==(DeleteCommand const& other) const = default;
    };

    /// Empty command for no-op operations.
    struct EmptyCommand
    {
        bool operator==(EmptyCommand const& other) const = default;
    };

    /// Command variant that can hold any of the supported command types.
    using Command = std::variant<PutCommand, DeleteCommand, EmptyCommand>;

    std::vector<std::byte> serialize(Command const& command);
    tl::expected<Command, std::string> deserialize(std::vector<std::byte> const& bytes);

}  // namespace raft_cli::store::data