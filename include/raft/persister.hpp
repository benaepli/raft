#pragma once

#include <optional>
#include <vector>

#include <tl/expected.hpp>

#include "raft/data.hpp"
#include "raft/errors.hpp"

namespace raft
{
    /// Represents a transaction for persisting Raft state changes.
    /// This structure allows atomic updates to different aspects of the Raft server's
    /// persistent state (term, votedFor, and log entries) while providing fine-grained
    /// control over which fields should be updated.
    struct PersistedTransaction
    {
        /// Flag indicating whether the current term should be updated.
        bool updateTerm = false;
        /// The current term value. Only used if updateTerm is true.
        uint64_t term;

        /// Flag indicating whether the votedFor field should be updated.
        bool updateVotedFor = false;
        /// The server ID that this server voted for in the current term.
        /// std::nullopt indicates no vote has been cast.
        /// Only used if updateVotedFor is true.
        std::optional<std::string> votedFor;

        /// Flag indicating whether log entries should be updated.
        bool updateLog = false;
        /// The index where new log entries should be stored.
        /// All existing entries from this index onward will be replaced.
        uint64_t startIndex;
        /// The new log entries to store starting from startIndex.
        /// Only used if updateLog is true.
        std::vector<data::LogEntry> entries;

        /// Configures the transaction to update log entries.
        /// @param start The index where new entries should be stored
        /// @param newEntries The entries to store starting from the start index
        void store(uint64_t start, const std::vector<data::LogEntry>& newEntries)
        {
            updateLog = true;
            startIndex = start;
            entries = newEntries;
        }

        /// Configures the transaction to update the current term.
        /// @param newTerm The new term value to persist
        void setTerm(uint64_t newTerm)
        {
            updateTerm = true;
            term = newTerm;
        }

        /// Configures the transaction to update the votedFor field.
        /// Sets the updateVotedFor flag and specifies the voted server ID.
        /// @param newVotedFor The server ID voted for, or std::nullopt for no vote
        void setVotedFor(std::optional<std::string> newVotedFor)
        {
            updateVotedFor = true;
            votedFor = std::move(newVotedFor);
        }
    };

    /// The interface for persisting the Raft server's state.
    ///
    /// This interface defines the contract for persistent storage of Raft state including
    /// metadata (current term, voted for) and log entries. Implementations must provide
    /// atomic transactions and consistent read operations to maintain Raft correctness.
    struct Persister
    {
        virtual ~Persister() = default;

        /// Gets the base index of the log.
        /// @return The base index if available, std::nullopt if unset.
        [[nodiscard]] virtual std::optional<uint64_t> getBaseIndex() = 0;

        /// Retrieves a single log entry at the specified index.
        /// @param index The log index to retrieve.
        /// @return The log entry if found, std::nullopt if not found.
        [[nodiscard]] virtual std::optional<data::LogEntry> getEntry(uint64_t index) const = 0;

        /// Retrieves all log entries in index order.
        /// @return Vector of all log entries.
        [[nodiscard]] virtual std::vector<data::LogEntry> getEntries() const = 0;

        /// Gets the term of the last log entry.
        /// @return The last term if available, std::nullopt if no entries exist.
        [[nodiscard]] virtual std::optional<uint64_t> getLastTerm() const = 0;

        /// Applies a set of state changes to persistent storage.
        /// @param transaction The transaction containing state changes to persist.
        /// @return Success or error result of the persistence operation.
        virtual tl::expected<void, Error> apply(PersistedTransaction const& transaction) = 0;
    };
}  // namespace raft
