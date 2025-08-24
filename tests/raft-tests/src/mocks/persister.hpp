#pragma once

#include <gmock/gmock.h>

#include "raft/persister.hpp"

namespace raft::testing
{
    class MockPersister : public Persister
    {
      public:
        MOCK_METHOD(void, saveState, (std::vector<std::byte> state), (override));
        MOCK_METHOD(std::optional<std::vector<std::byte>>, loadState, (), (override));
    };

    class NoOpPersister : public raft::Persister
    {
      public:
        void saveState(std::vector<std::byte> state) override {}
        std::optional<std::vector<std::byte>> loadState() override { return std::nullopt; }
    };
}  // namespace raft::testing