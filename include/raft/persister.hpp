#pragma once

#include <optional>
#include <vector>

namespace raft {
    /// The interface for persisting the Raft server's state.
    struct Persister {
        virtual ~Persister() = default;

        /// Saves the server's persistent state to storage.
        /// This method is called whenever the server's state changes and needs to be persisted.
        /// @param state The serialized state data to persist.
        virtual void saveState(std::vector<std::byte> state) = 0;

        /// Loads the server's persistent state from storage.
        /// This method is called during server initialization to restore previous state.
        /// @return The serialized state data if available, or std::nullopt if no state exists.
        virtual std::optional<std::vector<std::byte> > loadState() = 0;
    };
} // namespace raft
