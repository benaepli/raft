
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

    raft::impl::PersistenceHandler handler{persister, std::chrono::milliseconds(1), 2};
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data, .callback = [] {
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

TEST(PersistenceHandler, MaxEntriesIndividual) {
    std::array state1{0, 1, 2};
    std::array state2{3, 4, 5};

    auto persister = std::make_shared<MockPersister>();
    auto data1 = serialize(std::span(state1));
    auto data2 = serialize(std::span(state2));
    EXPECT_CALL(*persister, saveState(data1)).Times(1);
    EXPECT_CALL(*persister, saveState(data2)).Times(1);

    raft::impl::PersistenceHandler handler{persister, std::chrono::seconds(1), 1};
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data1, .callback = [] {
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data2, .callback = [] {
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
}

TEST(PersistenceHandler, CallbackExecution) {
    std::array state{0, 1, 2};
    auto persister = std::make_shared<MockPersister>();
    auto data = serialize(std::span(state));
    EXPECT_CALL(*persister, saveState(data)).Times(1);

    std::atomic<int> callbackCount{0};
    raft::impl::PersistenceHandler handler{persister, std::chrono::milliseconds(1), 2};
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data, .callback = [&callbackCount] {
            ++callbackCount;
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_EQ(callbackCount, 1);
}

TEST(PersistenceHandler, MultipleCallbacksExecution) {
    std::array state{0, 1, 2};
    auto persister = std::make_shared<MockPersister>();
    auto data = serialize(std::span(state));
    EXPECT_CALL(*persister, saveState(data)).Times(1);

    std::atomic<int> callbackCount{0};
    raft::impl::PersistenceHandler handler{persister, std::chrono::milliseconds(10), 3};

    for (int i = 0; i < 3; ++i) {
        handler.addRequest(raft::impl::PersistenceRequest{
            .data = data, .callback = [&callbackCount] {
                ++callbackCount;
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_EQ(callbackCount, 3);
}

TEST(PersistenceHandler, BatchPersistence) {
    std::array state1{0, 1, 2};
    std::array state2{3, 4, 5};
    std::array state3{6, 7, 8};

    auto persister = std::make_shared<MockPersister>();
    auto data1 = serialize(std::span(state1));
    auto data2 = serialize(std::span(state2));
    auto data3 = serialize(std::span(state3));

    EXPECT_CALL(*persister, saveState(data3)).Times(1);

    raft::impl::PersistenceHandler handler{persister, std::chrono::seconds(1), 3};
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data1, .callback = [] {
        }
    });
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data2, .callback = [] {
        }
    });
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data3, .callback = [] {
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(5));
}

TEST(PersistenceHandler, ConcurrentRequests) {
    std::array state{0, 1, 2};
    auto persister = std::make_shared<MockPersister>();
    auto data = serialize(std::span(state));
    EXPECT_CALL(*persister, saveState(testing::_)).Times(testing::AtLeast(1));

    std::atomic<int> totalCallbacks{0};
    raft::impl::PersistenceHandler handler{persister, std::chrono::milliseconds(5), 10};

    std::vector<std::thread> threads;
    const int numThreads = 5;
    const int requestsPerThread = 10;

    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&handler, &data, &totalCallbacks, requestsPerThread]() {
            for (int i = 0; i < requestsPerThread; ++i) {
                handler.addRequest(raft::impl::PersistenceRequest{
                    .data = data, .callback = [&totalCallbacks] {
                        totalCallbacks++;
                    }
                });
            }
        });
    }

    for (auto &thread: threads) {
        thread.join();
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_EQ(totalCallbacks, numThreads * requestsPerThread);
}

TEST(PersistenceHandler, MultipleTimeoutWithMaxEntries) {
    std::vector state1{1, 2, 3};
    std::vector state2{4, 5, 6};
    std::vector state3{7, 8, 9};
    std::vector state4{10, 11, 12};
    std::vector state5{13, 14, 15};

    auto persister = std::make_shared<MockPersister>();
    auto data1 = serialize(std::span(state1));
    auto data2 = serialize(std::span(state2));
    auto data3 = serialize(std::span(state3));
    auto data4 = serialize(std::span(state4));
    auto data5 = serialize(std::span(state5));

    EXPECT_CALL(*persister, saveState(data2)).Times(1);
    EXPECT_CALL(*persister, saveState(data5)).Times(1);

    // First two: timeout, last three: max entries
    raft::impl::PersistenceHandler handler{persister, std::chrono::milliseconds(20), 3};
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data1, .callback = [] {
        }
    });
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data2, .callback = [] {
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data3, .callback = [] {
        }
    });
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data4, .callback = [] {
        }
    });
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data5, .callback = [] {
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

TEST(PersistenceHandler, LargeDataHandling) {
    std::vector<int> largeState(1000, 42);
    auto persister = std::make_shared<MockPersister>();
    auto data = serialize(std::span(largeState));
    EXPECT_CALL(*persister, saveState(data)).Times(1);

    std::atomic<bool> callbackExecuted{false};
    raft::impl::PersistenceHandler handler{persister, std::chrono::milliseconds(1), 2};
    handler.addRequest(raft::impl::PersistenceRequest{
        .data = data, .callback = [&callbackExecuted] {
            callbackExecuted = true;
        }
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    EXPECT_TRUE(callbackExecuted);
}
