#include <future>
#include <memory>

#include "raft/inmemory/manager.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "raft/client.hpp"

class MockServiceHandler : public raft::ServiceHandler
{
  public:
    MOCK_METHOD(void,
                handleAppendEntries,
                (const raft::data::AppendEntriesRequest& request,
                 std::function<void(tl::expected<raft::data::AppendEntriesResponse, raft::Error>)>
                     callback),
                (override));

    MOCK_METHOD(
        void,
        handleRequestVote,
        (const raft::data::RequestVoteRequest& request,
         std::function<void(tl::expected<raft::data::RequestVoteResponse, raft::Error>)> callback),
        (override));
};

TEST(InMemoryManagerTest, SingleClientNetworkAppendEntries)
{
    auto mockHandler = std::make_shared<MockServiceHandler>();
    auto manager = raft::inmemory::createManager();
    EXPECT_TRUE(manager != nullptr);
    raft::inmemory::NetworkCreateConfig const config {.handler = mockHandler};
    auto networkResult = manager->createNetwork(config);
    EXPECT_TRUE(networkResult.has_value());
    auto network = networkResult.value();
    auto startResult = network->start("test-address");
    EXPECT_TRUE(startResult.has_value());
    auto clientResult = manager->createClient("test-address");
    EXPECT_TRUE(clientResult.has_value());
    auto client = std::move(clientResult.value());
    EXPECT_TRUE(client != nullptr);

    raft::data::AppendEntriesRequest request {.term = 1,
                                              .leaderID = "leader",
                                              .prevLogIndex = 1,
                                              .prevLogTerm = 1,
                                              .entries = {},
                                              .leaderCommit = 1};
    raft::data::AppendEntriesResponse response {.term = 1, .success = true};

    EXPECT_CALL(*mockHandler, handleAppendEntries(request, testing::_))
        .Times(1)
        .WillOnce(testing::Invoke([response](const raft::data::AppendEntriesRequest&, auto callback)
                                  { callback(response); }));

    std::promise<tl::expected<raft::data::AppendEntriesResponse, raft::Error>> promise;
    std::future<tl::expected<raft::data::AppendEntriesResponse, raft::Error>> future =
        promise.get_future();

    client->appendEntries(
        std::move(request),
        raft::RequestConfig(),
        [&promise](tl::expected<raft::data::AppendEntriesResponse, raft::Error> result)
        { promise.set_value(result); });

    ASSERT_TRUE(future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready);
    auto result = future.get();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), response);
}

TEST(InMemoryManagerTest, TwoServersWithTwoClientsAppendEntries)
{
    // Create two mock handlers for the two servers
    auto mockHandler1 = std::make_shared<MockServiceHandler>();
    auto mockHandler2 = std::make_shared<MockServiceHandler>();

    // Create a single manager that will coordinate both servers and clients
    auto manager = raft::inmemory::createManager();
    EXPECT_TRUE(manager != nullptr);

    // Set up first server
    raft::inmemory::NetworkCreateConfig config1 {.handler = mockHandler1};
    auto networkResult1 = manager->createNetwork(config1);
    EXPECT_TRUE(networkResult1.has_value());
    auto network1 = networkResult1.value();
    auto startResult1 = network1->start("server1-address");
    EXPECT_TRUE(startResult1.has_value());

    // Set up second server
    raft::inmemory::NetworkCreateConfig config2 {.handler = mockHandler2};
    auto networkResult2 = manager->createNetwork(config2);
    EXPECT_TRUE(networkResult2.has_value());
    auto network2 = networkResult2.value();
    auto startResult2 = network2->start("server2-address");
    EXPECT_TRUE(startResult2.has_value());

    // Create clients for cross-communication: client1 -> server2, client2 -> server1
    auto client1Result = manager->createClient("server2-address");
    EXPECT_TRUE(client1Result.has_value());
    auto client1 = std::move(client1Result.value());

    auto client2Result = manager->createClient("server1-address");
    EXPECT_TRUE(client2Result.has_value());
    auto client2 = std::move(client2Result.value());

    raft::data::AppendEntriesRequest request1 {.term = 1,
                                               .leaderID = "leader1",
                                               .prevLogIndex = 1,
                                               .prevLogTerm = 1,
                                               .entries = {},
                                               .leaderCommit = 1};
    raft::data::AppendEntriesResponse response1 {.term = 1, .success = true};

    raft::data::AppendEntriesRequest request2 {.term = 2,
                                               .leaderID = "leader2",
                                               .prevLogIndex = 2,
                                               .prevLogTerm = 2,
                                               .entries = {},
                                               .leaderCommit = 2};
    raft::data::AppendEntriesResponse response2 {.term = 2, .success = false};
    EXPECT_CALL(*mockHandler1, handleAppendEntries(request2, testing::_))
        .Times(1)
        .WillOnce(testing::Invoke([response1](const raft::data::AppendEntriesRequest&,
                                              auto callback) { callback(response1); }));
    EXPECT_CALL(*mockHandler2, handleAppendEntries(request1, testing::_))
        .Times(1)
        .WillOnce(testing::Invoke([response2](const raft::data::AppendEntriesRequest&,
                                              auto callback) { callback(response2); }));

    std::promise<tl::expected<raft::data::AppendEntriesResponse, raft::Error>> promise1;
    std::future<tl::expected<raft::data::AppendEntriesResponse, raft::Error>> future1 =
        promise1.get_future();

    std::promise<tl::expected<raft::data::AppendEntriesResponse, raft::Error>> promise2;
    std::future<tl::expected<raft::data::AppendEntriesResponse, raft::Error>> future2 =
        promise2.get_future();

    // Client1 calls server2
    client1->appendEntries(
        std::move(request1),
        raft::RequestConfig(),
        [&promise1](tl::expected<raft::data::AppendEntriesResponse, raft::Error> result)
        { promise1.set_value(result); });

    // Client2 calls server1
    client2->appendEntries(
        std::move(request2),
        raft::RequestConfig(),
        [&promise2](tl::expected<raft::data::AppendEntriesResponse, raft::Error> result)
        { promise2.set_value(result); });

    ASSERT_TRUE(future1.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready);
    ASSERT_TRUE(future2.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready);

    auto result1 = future1.get();
    ASSERT_TRUE(result1.has_value());
    ASSERT_EQ(result1.value(), response2);  // client1 -> server2 -> response2

    auto result2 = future2.get();
    ASSERT_TRUE(result2.has_value());
    ASSERT_EQ(result2.value(), response1);  // client2 -> server1 -> response1
}