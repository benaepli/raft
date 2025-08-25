#include <thread>

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mocks/persister.hpp"
#include "raft/fmt/errors.hpp"
#include "raft/inmemory/manager.hpp"
#include "raft/server.hpp"
#include "server_tester.hpp"

using raft::testing::NoOpPersister;
using raft::testing::ServerTester;

TEST(ElectionTest, SimpleLeaderElection)
{
    ServerTester tester({"A", "B", "C"}, std::make_shared<NoOpPersister>());

    std::string leader;
    tester.checkOneLeader(leader);
}

TEST(ElectionTest, LeaderReelectionAfterDisconnect)
{
    ServerTester tester({"A", "B", "C"}, std::make_shared<NoOpPersister>());

    // First, get an election with all three networks connected
    std::string firstLeader;
    tester.checkOneLeader(firstLeader);
    // Disconnect the leader
    auto disconnectResult = tester.manager->detachNetwork(firstLeader);
    EXPECT_TRUE(disconnectResult.has_value())
        << fmt::format("Failed to disconnect leader: {}", disconnectResult.error());

    // Wait the max election timeout for a new election
    std::this_thread::sleep_for(std::chrono::milliseconds(ServerTester::MAX_ELECTION_TIMEOUT));

    // Check for a new leader (should be different from the first leader)
    std::string secondLeader;
    tester.checkOneLeader(secondLeader);
    EXPECT_NE(firstLeader, secondLeader) << "Expected a different leader after disconnection";

    // Reconnect the previous leader
    auto reconnectResult = tester.manager->attachNetwork(firstLeader);
    EXPECT_TRUE(reconnectResult.has_value())
        << fmt::format("Failed to reconnect leader: {}", reconnectResult.error());

    // Wait the election timeout
    std::this_thread::sleep_for(std::chrono::milliseconds(ServerTester::MAX_ELECTION_TIMEOUT));

    // Check that the second leader is still the leader
    std::string finalLeader;
    tester.checkOneLeader(finalLeader);
    EXPECT_EQ(secondLeader, finalLeader)
        << "Expected second leader to remain leader after reconnection";
}

TEST(ElectionTest, NoMajorityEven)
{
    ServerTester tester({"A", "B", "C", "D"}, std::make_shared<NoOpPersister>());

    tester.manager->detachNetwork("A");
    tester.manager->detachNetwork("B");

    std::this_thread::sleep_for(std::chrono::milliseconds(ServerTester::MAX_ELECTION_TIMEOUT));

    tester.checkNoLeader();

    tester.manager->attachNetwork("A");
    std::string leader;
    tester.checkOneLeader(leader);
}