#include "log.hpp"

#include <spdlog/spdlog.h>

#include "raft/fmt/errors.hpp"
#include "utils/grpc_errors.hpp"

namespace raft::impl
{
    size_t calculateSize(const data::LogEntry& entry)
    {
        return sizeof(entry.term)
            + std::visit(errors::overloaded {[](const std::vector<std::byte>& data)
                                             { return data.size(); },
                                             [](const data::NoOp&) { return sizeof(data::NoOp); }},
                         entry.entry);
    }

    Log::Log(std::shared_ptr<Persister> persister)
        : persister_(std::move(persister))
    {
        entries_ = persister_->getEntries();
        baseIndex_ = persister_->getBaseIndex().value_or(1);
    }

    data::LogEntry* Log::get(uint64_t index)
    {
        if (index < baseIndex_)
        {
            return nullptr;
        }
        size_t offset = index - baseIndex_;
        if (offset >= entries_.size())
        {
            return nullptr;
        }
        return &entries_[offset];
    }

    data::LogEntry const* Log::get(uint64_t index) const
    {
        if (index < baseIndex_)
        {
            return nullptr;
        }
        size_t offset = index - baseIndex_;
        if (offset >= entries_.size())
        {
            return nullptr;
        }
        return &entries_[offset];
    }

    uint64_t Log::lastIndex() const
    {
        return baseIndex_ + entries_.size() - 1;
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
            if (entries_[offset + i].term != newEntries[i].term)
            {
                return true;
            }
        }
        return false;
    }

    void Log::store(uint64_t startIndex, const std::vector<data::LogEntry>& newEntries)
    {
        if (startIndex < baseIndex_)
        {
            spdlog::error(
                "[Log] start index {} is less than base index {}", startIndex, baseIndex_);
            return;
        }
        size_t offset = startIndex - baseIndex_;
        if (offset > entries_.size())
        {
            spdlog::error(
                "[Log] start index {} is greater than log size {}", startIndex, entries_.size());
            return;
        }
        PersistedTransaction transaction;
        transaction.store(startIndex, newEntries);
        if (auto error = persister_->apply(transaction); !error.has_value())
        {
            spdlog::error("[Log] failed to apply transaction: {}", error.error());
            return;
        }

        auto minSize = offset + newEntries.size();
        if (minSize > entries_.size() || isConflicting(offset, newEntries))
        {
            // If the new entries_ conflict with the existing ones, we have to erase the old ones.
            entries_.resize(minSize);
        }

        for (size_t i = 0; i < newEntries.size(); i++)
        {
            entries_[offset + i] = newEntries[i];
        }
    }

    EntryInfo Log::appendOne(uint64_t term, std::vector<std::byte> data)
    {
        PersistedTransaction transaction;
        transaction.store(lastIndex() + 1,
                          {data::LogEntry {
                              .term = term,
                              .entry = data,
                          }});
        if (auto error = persister_->apply(transaction); !error.has_value())
        {
            spdlog::error("[Log] failed to apply transaction: {}", error.error());
        }

        auto index = lastIndex() + 1;
        auto entry = data::LogEntry {
            .term = term,
            .entry = std::move(data),
        };
        entries_.push_back(entry);
        return {.index = index, .term = term};
    }

    void Log::appendNoOp(uint64_t term)
    {
        PersistedTransaction transaction;
        transaction.store(lastIndex() + 1,
                          {data::LogEntry {
                              .term = term,
                              .entry = data::NoOp {},
                          }});
        if (auto error = persister_->apply(transaction); !error)
        {
            spdlog::error("[Log] failed to apply transaction: {}", error.error());
        }

        auto entry = data::LogEntry {
            .term = term,
            .entry = data::NoOp {},
        };
        entries_.push_back(entry);
    }

    size_t Log::bytes() const
    {
        size_t size = 0;
        for (const auto& entry : entries_)
        {
            size += calculateSize(entry);
        }
        return size;
    }

}  // namespace raft::impl