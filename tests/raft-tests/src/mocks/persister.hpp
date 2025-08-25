#pragma once

#include <gmock/gmock.h>
#include <tl/expected.hpp>

#include "raft/persister.hpp"

namespace raft::testing
{
    class MockPersister : public Persister
    {
      public:
        MOCK_METHOD((tl::expected<void, Error>),
                    saveState,
                    (std::vector<std::byte> state),
                    (noexcept, override));
        MOCK_METHOD((tl::expected<std::vector<std::byte>, Error>), loadState, (), (noexcept, override));
    };

    class NoOpPersister : public raft::Persister
    {
      public:
        tl::expected<void, Error> saveState(std::vector<std::byte> state) noexcept override
        {
            return {};
        }
        tl::expected<std::vector<std::byte>, Error> loadState() noexcept override 
        { 
            return tl::make_unexpected(errors::NoPersistedState{}); 
        }
    };
}  // namespace raft::testing