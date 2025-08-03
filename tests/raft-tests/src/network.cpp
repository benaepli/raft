#include <future>
#include <memory>

#include "raft/network.hpp"

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

namespace
{
    std::pair<std::shared_ptr<raft::Network>, std::string> setupNetwork(
        std::shared_ptr<MockServiceHandler> mockHandler, const std::string& address = "127.0.0.1:0")
    {
        raft::NetworkCreateConfig config;
        config.handler = mockHandler;

        auto networkResult = raft::createNetwork(config);
        EXPECT_TRUE(networkResult.has_value());
        auto network = networkResult.value();

        auto startResult = network->start(address);
        EXPECT_TRUE(startResult.has_value());
        std::string actualAddress = startResult.value();

        return std::make_pair(network, actualAddress);
    }

    std::pair<std::shared_ptr<raft::Network>, std::unique_ptr<raft::Client>> setupNetworkAndClient(
        std::shared_ptr<MockServiceHandler> mockHandler, const std::string& address = "127.0.0.1:0")
    {
        auto [network, actualAddress] = setupNetwork(mockHandler, address);

        auto clientResult = raft::createClient(actualAddress);
        EXPECT_TRUE(clientResult.has_value());
        auto client = std::move(clientResult.value());

        return std::make_pair(network, std::move(client));
    }
}  // namespace

TEST(NetworkTest, ValidAppendEntriesAnyPort)
{
    auto mockHandler = std::make_shared<MockServiceHandler>();
    auto [network, client] = setupNetworkAndClient(mockHandler);

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

TEST(NetworkTest, ValidRequestVoteAnyPort)
{
    auto mockHandler = std::make_shared<MockServiceHandler>();
    auto [network, client] = setupNetworkAndClient(mockHandler);

    raft::data::RequestVoteRequest request {
        .term = 1, .candidateID = "candidate", .lastLogIndex = 1, .lastLogTerm = 1};
    raft::data::RequestVoteResponse response {.term = 1, .voteGranted = true};
    EXPECT_CALL(*mockHandler, handleRequestVote(request, testing::_))
        .Times(1)
        .WillOnce(testing::Invoke([response](const raft::data::RequestVoteRequest&, auto callback)
                                  { callback(response); }));

    std::promise<tl::expected<raft::data::RequestVoteResponse, raft::Error>> promise;
    std::future<tl::expected<raft::data::RequestVoteResponse, raft::Error>> future =
        promise.get_future();
    client->requestVote(
        std::move(request),
        raft::RequestConfig(),
        [&promise](tl::expected<raft::data::RequestVoteResponse, raft::Error> result)
        { promise.set_value(result); });
    ASSERT_TRUE(future.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready);
    auto result = future.get();
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ(result.value(), response);
}

TEST(NetworkTest, ValidAppendEntriesGivenPort)
{
    auto mockHandler = std::make_shared<MockServiceHandler>();
    std::string providedAddress = "127.0.0.1:5678";
    auto [network, actualAddress] = setupNetwork(mockHandler, providedAddress);

    ASSERT_EQ(providedAddress, actualAddress);

    auto clientResult = raft::createClient(actualAddress);
    EXPECT_TRUE(clientResult.has_value());
    auto client = std::move(clientResult.value());

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

TEST(NetworkTest, AppendEntriesInvalidArgumentResponse)
{
    auto mockHandler = std::make_shared<MockServiceHandler>();
    auto [network, client] = setupNetworkAndClient(mockHandler);

    raft::data::AppendEntriesRequest request {.term = 1,
                                              .leaderID = "leader",
                                              .prevLogIndex = 1,
                                              .prevLogTerm = 1,
                                              .entries = {},
                                              .leaderCommit = 1};
    auto error = raft::errors::InvalidArgument {"Invalid request parameter"};
    EXPECT_CALL(*mockHandler, handleAppendEntries(request, testing::_))
        .Times(1)
        .WillOnce(testing::Invoke([error](const raft::data::AppendEntriesRequest&, auto callback)
                                  { callback(tl::unexpected(error)); }));

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
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<raft::errors::InvalidArgument>(result.error()));
    ASSERT_EQ(std::get<raft::errors::InvalidArgument>(result.error()).message, error.message);
}
