#pragma once
#include <memory>

#include <gtest/gtest.h>

#include "raft/fmt/errors.hpp"
#include "raft/inmemory/manager.hpp"
#include "raft/network.hpp"

namespace raft::testing
{
    struct ServerAndNetwork
    {
        std::string id;
        std::shared_ptr<raft::Server> server;
        std::shared_ptr<raft::Network> network;
    };

    inline std::vector<Peer> getPeers(std::vector<std::string> ids, std::string current)
    {
        auto peers = std::vector<raft::Peer>();
        for (auto const& id : ids)
        {
            if (id != current)
            {
                peers.emplace_back(id, id);
            }
        }
        return peers;
    }

    struct ServerTester
    {
        constexpr static std::chrono::duration ELECTION_WAIT_PERIOD = std::chrono::seconds(1);
        constexpr static auto THREAD_COUNT = 4;

        constexpr static uint64_t MAX_ELECTION_TIMEOUT = 300;
        constexpr static uint64_t MIN_ELECTION_TIMEOUT = 100;

        std::shared_ptr<raft::inmemory::Manager> manager;
        std::vector<ServerAndNetwork> servers;

        // ServerTester's constructor
        explicit ServerTester(std::vector<std::string> ids, std::shared_ptr<Persister> persister)
        {
            init(std::move(ids), std::move(persister));
        }

        // Initializes servers and networks for the given IDs and starts them.
        void init(std::vector<std::string> ids, std::shared_ptr<Persister> persister)
        {
            manager = raft::inmemory::createManager();
            for (auto const& id : ids)
            {
                auto clientFactoryResult = manager->createClientFactory(id);
                ASSERT_TRUE(clientFactoryResult.has_value())
                    << "Failed to create client factory for " << id;
                auto config = raft::ServerCreateConfig {
                    .id = id,
                    .clientFactory = *clientFactoryResult,
                    .peers = getPeers(ids, id),
                    .persister = persister,
                    .timeoutInterval = raft::TimeoutInterval {.min = MIN_ELECTION_TIMEOUT,
                                                              .max = MAX_ELECTION_TIMEOUT},
                    .threadCount = THREAD_COUNT,
                };
                auto serverResult = createServer(config);
                ASSERT_TRUE(serverResult.has_value())
                    << "Failed to create server for " << id << ": "
                    << fmt::format("{}", serverResult.error());
                auto server = std::move(serverResult.value());
                auto networkResult =
                    manager->createNetwork(raft::inmemory::NetworkCreateConfig {.handler = server});
                ASSERT_TRUE(networkResult.has_value())
                    << "Failed to create network for " << id << ": "
                    << fmt::format("{}", networkResult.error());
                auto network = networkResult.value();
                auto startResult = network->start(id);
                ASSERT_TRUE(startResult.has_value()) << "Failed to start network for " << id << ": "
                                                     << fmt::format("{}", startResult.error());
                servers.emplace_back(
                    ServerAndNetwork {.id = id, .server = server, .network = network});
            }

            for (auto const& server : servers)
            {
                auto startResult = server.server->start();
                ASSERT_TRUE(startResult.has_value())
                    << "Failed to start server " << server.id << ": "
                    << fmt::format("{}", startResult.error());
            }
        }

        // Verifies that there is exactly one active leader and returns that leader via out
        // parameter.
        void checkOneLeader(std::string& leader)
        {
            // While our timeout hasn't elapsed, we do the following:
            // 1. Check each server's status. If it's a leader, record its term. If there are
            // multiple leaders at the same term, fail with ASSERT.
            // 2. Find the leader with the highest term.
            // 3. Check if that leader still has a majority.
            // Note that we read the status twice per iteration. This is to ensure that
            // we don't have issues if a Raft leader immediately loses leadership before
            // its first heartbeat.
            std::chrono::steady_clock::time_point const start = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() < start + ELECTION_WAIT_PERIOD)
            {
                std::map<uint64_t, std::string> termToLeader;
                for (auto const& server : servers)
                {
                    auto status = server.server->getStatus();
                    ASSERT_TRUE(status.has_value())
                        << "Failed to get server status: " << fmt::format("{}", status.error());
                    auto term = status->term;
                    auto isLeader = status->isLeader;
                    if (!isLeader)
                    {
                        continue;
                    }
                    auto it = termToLeader.find(term);
                    ASSERT_FALSE(it != termToLeader.end() && it->second != server.id)
                        << "More than one leader found";
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
                for (auto const& server : servers)
                {
                    auto status = server.server->getStatus();
                    ASSERT_TRUE(status.has_value())
                        << "Failed to get server status: " << fmt::format("{}", status.error());
                    if (status->term == leaderTerm)
                    {
                        termAgreementCount++;
                    }
                }
                uint64_t majority = servers.size() / 2 + 1;
                if (termAgreementCount >= majority)
                {
                    leader = leaderID;
                    return;
                }
            }

            FAIL() << "Timed out waiting for a leader";
        }

        // Verifies that no servers believe themselves to be leaders.
        void checkNoLeader()
        {
            for (auto const& server : servers)
            {
                auto status = server.server->getStatus();
                ASSERT_TRUE(status.has_value())
                    << "Failed to get server status: " << fmt::format("{}", status.error());
                ASSERT_FALSE(status->isLeader) << "Server " << server.id << " is a leader";
            }
        }
    };
}  // namespace raft::testing