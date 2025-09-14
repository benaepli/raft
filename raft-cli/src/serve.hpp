#pragma once
#include <string>

namespace raft_cli
{
    void serve(std::string configPath, std::string nodeID)
    {
        auto config = config::loadConfig(configPath);
        if (!config)
        {
            std::cerr << fmt::format("{}\n", config.error());
            return;
        }

        auto found = std::find_if(config->nodes.begin(),
                                  config->nodes.end(),
                                  [nodeID](const auto& node) { return node.id == nodeID; });
        if (found == config->nodes.end())
        {
            std::cerr << fmt::format("node {} not found in config\n", nodeID);
            return;
        }
    }
}  // namespace raft_cli