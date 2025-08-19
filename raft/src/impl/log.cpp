#include "log.hpp"

#include <spdlog/spdlog.h>

namespace raft::impl
{
    data::LogEntry* Log::get(uint64_t index)
    {
        if (index < baseIndex)
        {
            return nullptr;
        }
        size_t offset = index - baseIndex;
        if (offset >= entries.size())
        {
            return nullptr;
        }
        return &entries[offset];
    }

    data::LogEntry const* Log::get(uint64_t index) const
    {
        if (index < baseIndex)
        {
            return nullptr;
        }
        size_t offset = index - baseIndex;
        if (offset >= entries.size())
        {
            return nullptr;
        }
        return &entries[offset];
    }

    uint64_t Log::lastIndex() const
    {
        return baseIndex + entries.size() - 1;
    }

    uint64_t Log::lastTerm() const
    {
        const auto* entry = get(lastIndex());
        if (entry == nullptr)
        {
            return 0;
        }
        return entry->term;
    }

    bool Log::candidateIsEligible(uint64_t candidateLastIndex, uint64_t candidateLastTerm) const
    {
        auto term = lastTerm();
        auto index = lastIndex();
        // If the candidate's log is empty, then we can only elect it if our log is also
        // empty.
        if (candidateLastIndex == 0)
        {
            return index == 0;
        }
        if (term != candidateLastTerm)
        {
            return term < candidateLastTerm;
        }
        return index <= candidateLastIndex;
    }

    bool Log::isConsistentWith(uint64_t leaderPriorIndex, uint64_t leaderPriorTerm) const
    {
        if (leaderPriorIndex == 0 && leaderPriorTerm == 0)
        {
            return true;
        }
        if (leaderPriorIndex > lastIndex())
        {
            return false;
        }
        auto* entry = get(leaderPriorIndex);
        if (entry == nullptr)
        {
            return false;
        }
        return entry->term == leaderPriorTerm;
    }

    bool Log::isConflicting(size_t offset, const std::vector<data::LogEntry>& newEntries) const
    {
        for (size_t i = 0; i < newEntries.size(); i++)
        {
            if (entries[offset + i].term != newEntries[i].term)
            {
                return true;
            }
        }
        return false;
    }

    void Log::store(uint64_t startIndex, const std::vector<data::LogEntry>& newEntries)
    {
        if (startIndex < baseIndex)
        {
            spdlog::error("[Log] start index {} is less than base index {}", startIndex, baseIndex);
            return;
        }
        size_t offset = startIndex - baseIndex;
        if (offset > entries.size())
        {
            spdlog::error(
                "[Log] start index {} is greater than log size {}", startIndex, entries.size());
            return;
        }
        auto minSize = offset + newEntries.size();
        if (minSize > entries.size() || isConflicting(offset, newEntries))
        {
            // If the new entries conflict with the existing ones, we have to erase the old ones.
            entries.resize(minSize);
        }

        for (size_t i = 0; i < newEntries.size(); i++)
        {
            entries[offset + i] = newEntries[i];
        }
    }

    EntryInfo Log::appendOne(uint64_t term, std::vector<std::byte> data)
    {
        auto index = lastIndex() + 1;
        auto entry = data::LogEntry {
            .term = term,
            .entry = std::move(data),
        };
        entries.push_back(entry);
        return {.index = index, .term = term};
    }

    void Log::appendNoOp(uint64_t term)
    {
        auto entry = data::LogEntry {
            .term = term,
            .entry = data::NoOp {},
        };
        entries.push_back(entry);
    }

}  // namespace raft::impl