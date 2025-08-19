#pragma once

#include <vector>

#include "raft/client.hpp"
#include "raft/server.hpp"

namespace raft::impl
{
    struct Log
    {
        std::vector<data::LogEntry> entries;
        // The index of the first entry in the log.
        // For instance, if the log contains entries with indices 10, 11, and 12, then baseIndex
        // will be 10.
        // This should be at least 1.
        uint64_t baseIndex = 1;

        [[nodiscard]] data::LogEntry* get(uint64_t index);

        [[nodiscard]] data::LogEntry const* get(uint64_t index) const;

        // The index of the last entry in the log, or baseIndex - 1 if the log is empty.
        [[nodiscard]] uint64_t lastIndex() const;

        // The term of the last entry in the log, or 0 if the log is empty.
        [[nodiscard]] uint64_t lastTerm() const;

        [[nodiscard]] bool candidateIsEligible(uint64_t candidateLastIndex,
                                               uint64_t candidateLastTerm) const;

        [[nodiscard]] bool isConsistentWith(uint64_t leaderPriorIndex,
                                            uint64_t leaderPriorTerm) const;

        // Stores entries into the log, discarding any existing entries past the startIndex.
        // Note that this does not validate startIndex.
        void store(uint64_t startIndex, const std::vector<data::LogEntry>& newEntries);

        EntryInfo appendOne(uint64_t term, std::vector<std::byte> data);

      private:
        // Returns true if the new entries are conflicting with the existing entries.
        [[nodiscard]] bool isConflicting(size_t offset,
                                         const std::vector<data::LogEntry>& newEntries) const;
    };
}  // namespace raft::impl