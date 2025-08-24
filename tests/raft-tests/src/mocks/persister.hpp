#pragma once

#include <gmock/gmock.h>

#include "raft/persister.hpp"

namespace raft::testing
{
    class MockPersister : public Persister
    {
      public:
        MOCK_METHOD(tl::expected<void, Error>, saveState, (std::vector<std::byte> state), (override));
        MOCK_METHOD(std::optional<std::vector<std::byte>>, loadState, (), (noexcept, override));
    };

    class NoOpPersister : public raft::Persister
    {
      public:
        tl::expected<void, Error> saveState(std::vector<std::byte> state) override { return {}; }
        std::optional<std::vector<std::byte>> loadState() noexcept override { return std::nullopt; }
    };
}  // namespace raft::testing