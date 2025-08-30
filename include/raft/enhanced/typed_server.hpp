#pragma once
#include <cstddef>
#include <functional>
#include <vector>

#include "raft/enhanced/server.hpp"

namespace raft::enhanced::typed
{
    /// Serializes an object of type T to bytes.
    /// @param obj The object to serialize.
    /// @return The serialized data as bytes.
    template<typename T>
    std::vector<std::byte> serialize(const T& obj);

    /// Deserializes bytes to an object of type T.
    /// @param bytes The bytes to deserialize.
    /// @return The deserialized object, or an error of type E if deserialization fails.
    template<typename T, typename E>
    tl::expected<T, E> deserialize(const std::vector<std::byte>& bytes);

    /// Concept requiring that a type T can be serialized and deserialized with error type E.
    template<typename T, typename E>
    concept Serializable = requires(T obj, const std::vector<std::byte>& bytes) {
        { serialize(obj) } -> std::same_as<std::vector<std::byte>>;
        { deserialize<T>(bytes) } -> std::same_as<tl::expected<T, E>>;
    };

    /// Information about a committed entry with typed data.
    template<typename T, typename E>
        requires Serializable<T, E>
    struct LocalCommitInfo
    {
        T data;  ///< The committed typed data.
        bool duplicate = false;  ///< Whether the entry was a duplicate.
    };

    /// The callback for when a log entry is committed through commit().
    /// @param result The result of the commit operation, containing typed data or an error.
    template<typename T, typename E>
        requires Serializable<T, E>
    using LocalCommitCallback =
        std::function<void(tl::expected<LocalCommitInfo<T, E>, std::variant<Error, E>> result)>;

    /// The callback for when any log entry is committed (local or remote).
    /// @param data The committed typed data, or an error if deserialization fails.
    /// @param local Whether the entry was committed through the commit() function.
    /// @param duplicate Whether the entry was a duplicate.
    template<typename T, typename E>
    using GlobalCommitCallback =
        std::function<void(tl::expected<T, E> data, bool local, bool duplicate)>;

    /// Type-safe wrapper around enhanced Raft server providing automatic serialization.
    ///
    /// This wrapper provides the same functionality as raft::enhanced::Server but with
    /// type safety and automatic serialization/deserialization. All functions are
    /// thread-safe. Callbacks are queued and executed in a serialized fashion, so it
    /// is important to keep callbacks lightweight. Since this class maintains a separate
    /// queue, it may be inconsistent with the underlying server instance.
    ///
    /// @tparam T The data type to be committed to the Raft log.
    /// @tparam E The error type for deserialization failures.
    template<typename T, typename E>
        requires Serializable<T, E>
    class Server
    {
      public:
        /// Constructs a typed enhanced server.
        /// @param config Configuration for creating the underlying enhanced server.
        explicit Server(ServerCreateConfig config)
            : server_(std::move(config))
        {
        }
        ~Server() = default;

        Server(Server const&) = delete;
        Server& operator=(Server const&) = delete;
        Server(Server&&) = default;
        Server& operator=(Server&&) = default;

        /// Commits typed data to the Raft log and monitors leadership and timeouts.
        ///
        /// The data is automatically serialized before being committed to the underlying
        /// Raft server. The callback receives either the deserialized committed data or
        /// an error (either from Raft operations or deserialization failures).
        ///
        /// @param info The client and request ID for this request (used for deduplication).
        /// @param value The typed data to commit to the Raft log.
        /// @param callback The callback to invoke when the commit is completed.
        void commit(RequestInfo const& info, const T& value, LocalCommitCallback<T, E> callback)
        {
            auto serialized = serialize(value);
            server_.commit(
                info,
                serialized,
                [callback =
                     std::move(callback)](tl::expected<enhanced::LocalCommitInfo, Error> result)
                {
                    if (result.has_value())
                    {
                        auto deserialized = deserialize<T>(result->data);
                        if (deserialized.has_value())
                        {
                            LocalCommitInfo<T, E> typedInfo {.data = std::move(*deserialized),
                                                             .duplicate = result->duplicate};
                            callback(std::move(typedInfo));
                        }
                        else
                        {
                            callback(tl::make_unexpected(deserialized.error()));
                        }
                    }
                    else
                    {
                        callback(tl::make_unexpected(result.error()));
                    }
                });
        }

        /// Commits typed data to the Raft log without deduplication information and monitors
        /// leadership and timeouts.
        ///
        /// The data is automatically serialized before being committed to the underlying
        /// Raft server. The callback receives either the deserialized committed data or
        /// an error (either from Raft operations or deserialization failures).
        ///
        /// @param value The typed data to commit to the Raft log.
        /// @param callback The callback to invoke when the commit is completed.
        void commit(const T& value, LocalCommitCallback<T, E> callback)
        {
            auto serialized = serialize(value);
            server_.commit(
                serialized,
                [callback =
                     std::move(callback)](tl::expected<enhanced::LocalCommitInfo, Error> result)
                {
                    if (result.has_value())
                    {
                        auto deserialized = deserialize<T>(result->data);
                        if (deserialized.has_value())
                        {
                            LocalCommitInfo<T, E> typedInfo {.data = std::move(*deserialized),
                                                             .duplicate = result->duplicate};
                            callback(std::move(typedInfo));
                        }
                        else
                        {
                            callback(tl::make_unexpected(deserialized.error()));
                        }
                    }
                    else
                    {
                        callback(tl::make_unexpected(result.error()));
                    }
                });
        }

        /// Ends the session for a specific client.
        /// @param clientID The client ID to clear from deduplication tracking.
        /// @param callback The callback to invoke when the session is ended.
        void endSession(std::string const& clientID,
                        std::function<void(tl::expected<void, Error>)> callback)
        {
            server_.endSession(clientID, std::move(callback));
        }

        /// Sets a global commit callback for all committed entries.
        ///
        /// The callback receives automatically deserialized data for all entries
        /// committed to the Raft log (both local commits through commit() and
        /// remote commits from other servers).
        ///
        /// @param callback The callback to invoke for all committed entries.
        void setCommitCallback(GlobalCommitCallback<T, E> callback)
        {
            server_.setCommitCallback(
                [callback = std::move(callback)](
                    std::vector<std::byte> data, bool local, bool duplicate)
                {
                    auto deserialized = deserialize<T>(data);
                    callback(std::move(deserialized), local, duplicate);
                });
        }

        /// Clears the global commit callback.
        void clearCommitCallback() { server_.clearCommitCallback(); }

      private:
        raft::enhanced::Server server_;
    };
}  // namespace raft::enhanced::typed