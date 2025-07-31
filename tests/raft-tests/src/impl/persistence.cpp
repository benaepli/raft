
#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "impl/persistence.hpp"
#include "raft/persister.hpp"

class MockPersister : public raft::Persister {
public:
    MOCK_METHOD(void, saveState, (std::vector<std::byte> state), (override));
    MOCK_METHOD(std::optional<std::vector<std::byte>>, loadState, (), (override));
};

namespace {
    template<typename T>
    std::vector<std::byte> serialize(T data) {
        auto byteSpan = std::as_writable_bytes(data);
        return std::vector<std::byte>(byteSpan.begin(), byteSpan.end());
    }
} // namespace

TEST(PersistenceHandler, SingleTimeout) {
    std::array state{0, 1, 2};

    auto persister = std::make_shared<MockPersister>();
    auto data = serialize(std::span(state));
    EXPECT_CALL(*persister, saveState(data)).Times(1);

    raft::impl::PersistenceHandler handler{persister, std::chrono::milliseconds(1), 1};
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data, .callback = [] {
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}
