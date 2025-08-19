#include <thread>

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mocks/persister.hpp"
#include "raft/fmt/errors.hpp"
#include "raft/inmemory/manager.hpp"
#include "raft/server.hpp"
#include "server_tester.hpp"

using raft::testing::MockPersister;
using raft::testing::ServerTester;

namespace
{
    void mustStartServer(raft::Server& server)
    {
        auto startResult = server.start();
        EXPECT_TRUE(startResult.has_value());
    }

    void mustStartNetwork(raft::Network& network, std::string id)
    {
        auto startResult = network.start(id);
        EXPECT_TRUE(startResult.has_value());
    }

    class NoOpPersister : public raft::Persister
    {
      public:
        void saveState(std::vector<std::byte> state) override {}
        std::optional<std::vector<std::byte>> loadState() override { return std::nullopt; }
    };
}  // namespace

TEST(ElectionTest, SimpleLeaderElection)
{
    ServerTester tester({"A", "B", "C"}, std::make_shared<NoOpPersister>());

    auto result = tester.checkOneLeader();
    EXPECT_TRUE(result.has_value()) << "Expected one leader to be elected, but got error: "
                                    << (result.has_value() ? "" : result.error());
}

TEST(ElectionTest, LeaderReelectionAfterDisconnect)
{
    ServerTester tester({"A", "B", "C"}, std::make_shared<NoOpPersister>());

    // First, get an election with all three networks connected
    auto firstLeaderResult = tester.checkOneLeader();
    ASSERT_TRUE(firstLeaderResult.has_value())
        << "Expected one leader to be elected, but got error: " << firstLeaderResult.error();
    std::string firstLeader = firstLeaderResult.value();
    // Disconnect the leader
    auto disconnectResult = tester.manager->detachNetwork(firstLeader);
    EXPECT_TRUE(disconnectResult.has_value())
        << fmt::format("Failed to disconnect leader: {}", disconnectResult.error());

    // Wait the max election timeout for a new election
    std::this_thread::sleep_for(std::chrono::milliseconds(ServerTester::MAX_ELECTION_TIMEOUT));

    // Check for a new leader (should be different from the first leader)
    auto secondLeaderResult = tester.checkOneLeader();
    ASSERT_TRUE(secondLeaderResult.has_value())
        << "Expected new leader to be elected, but got error: " << secondLeaderResult.error();
    std::string secondLeader = secondLeaderResult.value();
    EXPECT_NE(firstLeader, secondLeader) << "Expected a different leader after disconnection";

    // Reconnect the previous leader
    auto reconnectResult = tester.manager->attachNetwork(firstLeader);
    EXPECT_TRUE(reconnectResult.has_value())
        << fmt::format("Failed to reconnect leader: {}", reconnectResult.error());

    // Wait the election timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(ServerTester::MAX_ELECTION_TIMEOUT));

    // Check that the second leader is still the leader
    auto finalLeaderResult = tester.checkOneLeader();
    ASSERT_TRUE(finalLeaderResult.has_value())
        << "Expected leader to remain, but got error: " << finalLeaderResult.error();
    std::string finalLeader = finalLeaderResult.value();
    EXPECT_EQ(secondLeader, finalLeader)
        << "Expected second leader to remain leader after reconnection";
}