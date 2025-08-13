#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mocks/persister.hpp"
#include "raft/fmt/errors.hpp"
#include "raft/inmemory/manager.hpp"
#include "raft/server.hpp"

using raft::testing::MockPersister;

namespace
{
    constexpr std::chrono::duration ELECTION_WAIT_PERIOD = std::chrono::seconds(1);
    constexpr auto THREAD_COUNT = 4;

    std::vector<raft::Peer> getPeers(std::vector<std::string> ids, std::string current)
    {
        auto peers = std::vector<raft::Peer>();
        for (const auto& id : ids)
        {
            if (id != current)
            {
                peers.emplace_back(id, id);
            }
        }
        return peers;
    }

    struct ServerAndNetwork
    {
        std::string id;
        std::shared_ptr<raft::Server> server;
        std::shared_ptr<raft::Network> network;
    };

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

    struct ElectionTester
    {
        std::shared_ptr<raft::inmemory::Manager> manager;
        std::vector<ServerAndNetwork> servers;

        // ElectionTester's constructor creates servers and networks for the given IDs
        // and starts them.
        explicit ElectionTester(std::vector<std::string> ids)
        {
            manager = raft::inmemory::createManager();
            for (const auto& id : ids)
            {
                auto config = raft::ServerCreateConfig {
                    .id = id,
                    .clientFactory = manager,
                    .peers = getPeers(ids, id),
                    .persister = std::make_shared<NoOpPersister>(),
                    .threadCount = THREAD_COUNT,
                };
                auto serverResult = raft::createServer(config);
                EXPECT_TRUE(serverResult.has_value());
                auto server = std::move(serverResult.value());
                auto networkResult =
                    manager->createNetwork(raft::inmemory::NetworkCreateConfig {.handler = server});
                EXPECT_TRUE(networkResult.has_value());
                auto network = networkResult.value();
                mustStartNetwork(*network, id);
                servers.emplace_back(
                    ServerAndNetwork {.id = id, .server = server, .network = network});
            }

            for (const auto& server : servers)
            {
                mustStartServer(*server.server);
            }
        }

        // Verifies that there is exactly one active leader and returns that leader.
        tl::expected<std::string, std::string> checkOneLeader()
        {
            // While our timeout hasn't elapsed, we do the following:
            // 1. Check each server's status. If it's a leader, record its term. If there are
            // multiple leaders at the same term, return an error.
            // 2. Find the leader with the highest term.
            // 3. Check if that leader still has a majority.
            // Note that we read the status twice per iteration. This is to ensure that
            // we don't have issues if a Raft leader immediately loses leadership before
            // its first heartbeat.
            std::chrono::steady_clock::time_point const start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() < start + ELECTION_WAIT_PERIOD)
            {
                std::map<uint64_t, std::string> termToLeader;
                for (const auto& server : servers)
                {
                    auto status = server.server->getStatus();
                    if (!status.has_value())
                    {
                        return tl::make_unexpected(fmt::format("{}", status.error()));
                    }
                    auto term = status->term;
                    auto isLeader = status->isLeader;
                    if (!isLeader)
                    {
                        continue;
                    }
                    auto it = termToLeader.find(term);
                    if (it != termToLeader.end() && it->second != server.id)
                    {
                        return tl::make_unexpected("more than one leader found");
                    }
                    termToLeader.emplace(term, server.id);
                }

                // Find highest term with a leader, which is the last element in the map.
                auto it = termToLeader.rbegin();
                if (it == termToLeader.rend())
                {
                    continue;
                }
                uint64_t leaderTerm = it->first;
                std::string leaderID = it->second;
                uint64_t termAgreementCount = 0;
                for (const auto& server : servers)
                {
                    auto status = server.server->getStatus();
                    if (!status.has_value())
                    {
                        return tl::make_unexpected(fmt::format("{}", status.error()));
                    }
                    if (status->term == leaderTerm)
                    {
                        termAgreementCount++;
                    }
                }
                uint64_t majority = servers.size() / 2 + 1;
                if (termAgreementCount >= majority)
                {
                    return leaderID;
                }
            }

            return tl::make_unexpected("no leader found");
        }
    };
}  // namespace

TEST(ElectionTest, SimpleLeaderElection)
{
    ElectionTester tester({"A", "B", "C"});

    auto result = tester.checkOneLeader();
    EXPECT_TRUE(result.has_value()) << "Expected one leader to be elected, but got error: "
                                    << (result.has_value() ? "" : result.error());
}