#pragma once

#include <vector>

#include "raft/data.hpp"
#include "raft/server.hpp"

namespace raft::impl
{
    struct Log
    {
        explicit Log(std::shared_ptr<Persister> persister);

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
        void appendNoOp(uint64_t term);

        // Returns the total number of bytes used by the log.
        [[nodiscard]] size_t bytes() const;

      private:
        std::shared_ptr<Persister> persister_;

        // An in-memory copy of the log entries.
        std::vector<data::LogEntry> entries_;
        // The index of the first entry in the log.
        uint64_t baseIndex_ = 1;

        // Returns true if the new entries are conflicting with the existing entries.
        [[nodiscard]] bool isConflicting(size_t offset,
                                         const std::vector<data::LogEntry>& newEntries) const;
    };
}  // namespace raft::impl