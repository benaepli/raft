#pragma once

#include <optional>
#include <vector>

#include <tl/expected.hpp>

#include "raft/errors.hpp"

namespace raft
{
    /// The interface for persisting the Raft server's state.
    struct Persister
    {
        virtual ~Persister() = default;

        /// Saves the server's persistent state to storage.
        /// This method is called whenever the server's state changes and needs to be persisted.
        /// @param state The serialized state data to persist.
        /// @return Success or a persistence error.
        virtual tl::expected<void, Error> saveState(std::vector<std::byte> state) noexcept = 0;

        /// Loads the server's persistent state from storage.
        /// This method is called during server initialization to restore previous state.
        /// @return The serialized state data if available, or NoPersistedState if no state exists.
        virtual tl::expected<std::vector<std::byte>, Error> loadState() noexcept = 0;
    };
}  // namespace raft
