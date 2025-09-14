#pragma once

#include "raft/persister.hpp"

namespace raft::testing
{
    class NoOpPersister final : public raft::Persister
    {
      public:
        NoOpPersister() = default;
        ~NoOpPersister() override = default;

        [[nodiscard]] std::optional<uint64_t> getBaseIndex() override { return 1; }
        [[nodiscard]] std::optional<data::LogEntry> getEntry(uint64_t index) const override
        {
            return {};
        }
        [[nodiscard]] std::optional<uint64_t> getLastTerm() const override { return {}; }

        tl::expected<void, Error> apply(const PersistedTransaction& transaction) override
        {
            return {};
        }
        [[nodiscard]] std::vector<data::LogEntry> getEntries() const override { return {}; }
    };
}  // namespace raft::testing