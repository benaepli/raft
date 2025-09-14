#pragma once
#include <algorithm>
#include <iostream>
#include <string>

#include <fmt/format.h>

#include "config/config.hpp"
#include "raft/server.hpp"
#include "store/data.hpp"
#include "store/server.hpp"

namespace raft_cli
{
    inline void serve(std::string configPath, std::string nodeID)
    {
        auto config = config::loadConfig(configPath);
        if (!config)
        {
            std::cerr << fmt::format("{}\n", config.error());
            return;
        }

        auto found = std::find_if(config->nodes.begin(),
                                  config->nodes.end(),
                                  [nodeID](auto const& node) { return node.id == nodeID; });
        if (found == config->nodes.end())
        {
            std::cerr << fmt::format("node {} not found in config\n", nodeID);
            return;
        }

        std::vector<raft::Peer> peers;
        for (auto const& node : config->nodes)
        {
            if (node.id == nodeID)
            {
                continue;
            }

            peers.emplace_back(node.id, fmt::format("{}:{}", node.address, node.raftPort));
        }

        // Create KVStore configuration using TimeoutInterval from config
        store::KVStoreConfig kvConfig = {
            .kvPort = found->kvPort,
            .id = nodeID,
            .raftPort = found->raftPort,
            .sqliteDBPath = fmt::format("{}/{}.db", config->settings.dataDirectory, nodeID),
            .peers = std::move(peers),
            .threadCount = 1,  // Default thread count
            .electionTimeout = config->settings.electionTimeoutInterval,
            .heartbeatInterval = config->settings.heartbeatInterval,
            .commitTimeout = std::chrono::seconds(5)  // Default commit timeout
        };

        auto kvStore = store::KVStore::create(kvConfig);
        if (!kvStore)
        {
            std::cerr << fmt::format("Failed to create KV store: {}\n", kvStore.error());
            return;
        }

        auto startResult = kvStore->start();
        if (!startResult)
        {
            std::cerr << fmt::format("Failed to start KV store: {}\n", startResult.error());
            return;
        }

        std::cout << fmt::format(
            "KV store started on port {}, Raft on port {}\n", found->kvPort, found->raftPort);

        // Keep the server running until Ctrl+C or the exit command is received
        std::cout << "Press Ctrl+C or type 'exit' to stop the server\n";
        while (true)
        {
            std::string line;
            std::getline(std::cin, line);
            if (line == "exit")
            {
                break;
            }
        }

        kvStore->stop();
    }
}  // namespace raft_cli