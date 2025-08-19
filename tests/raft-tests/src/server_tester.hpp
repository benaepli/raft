#pragma once
#include <memory>

#include <gtest/gtest.h>

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
        for (const auto& id : ids)
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

        // ServerTester's constructor creates servers and networks for the given IDs
        // and starts them.
        explicit ServerTester(std::vector<std::string> ids, std::shared_ptr<Persister> persister)
        {
            manager = raft::inmemory::createManager();
            for (const auto& id : ids)
            {
                auto clientFactoryResult = manager->createClientFactory(id);
                EXPECT_TRUE(clientFactoryResult.has_value());
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
                EXPECT_TRUE(serverResult.has_value());
                auto server = std::move(serverResult.value());
                auto networkResult =
                    manager->createNetwork(raft::inmemory::NetworkCreateConfig {.handler = server});
                EXPECT_TRUE(networkResult.has_value());
                auto network = networkResult.value();
                auto startResult = network->start(id);
                EXPECT_TRUE(startResult.has_value());
                servers.emplace_back(
                    ServerAndNetwork {.id = id, .server = server, .network = network});
            }

            for (const auto& server : servers)
            {
                auto startResult = server.server->start();
                EXPECT_TRUE(startResult.has_value());
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
}  // namespace raft::testing