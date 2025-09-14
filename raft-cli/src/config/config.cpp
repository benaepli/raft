#include <filesystem>

#include "config.hpp"

#include <toml++/toml.hpp>

namespace raft_cli::config
{
    namespace
    {
        tl::expected<Settings, Error> parseSettings(const toml::table& config)
        {
            Settings settings;

            // Use defaults
            settings.electionTimeoutInterval = DEFAULT_ELECTION_TIMEOUT_INTERVAL;
            settings.heartbeatInterval = DEFAULT_HEARTBEAT_INTERVAL;

            auto settingsTable = config["settings"].as_table();
            if (!settingsTable)
            {
                return tl::unexpected(
                    errors::ConfigError {"missing [settings] section in config file"});
            }

            // Parse min_election_timeout_ms (optional, use default if not specified)
            if (auto minTimeoutNode = (*settingsTable)["min_election_timeout_ms"])
            {
                if (!minTimeoutNode.as_integer())
                {
                    return tl::unexpected(errors::ConfigError {
                        "invalid min_election_timeout_ms in [settings] - must be an integer"});
                }
                settings.electionTimeoutInterval.min = minTimeoutNode.as_integer()->get();
            }

            // Parse max_election_timeout_ms (optional, use default if not specified)
            if (auto maxTimeoutNode = (*settingsTable)["max_election_timeout_ms"])
            {
                if (!maxTimeoutNode.as_integer())
                {
                    return tl::unexpected(errors::ConfigError {
                        "invalid max_election_timeout_ms in [settings] - must be an integer"});
                }
                settings.electionTimeoutInterval.max = maxTimeoutNode.as_integer()->get();
            }

            // Parse heartbeat_interval_ms (optional, use default if not specified)
            if (auto heartbeatIntervalNode = (*settingsTable)["heartbeat_interval_ms"])
            {
                if (!heartbeatIntervalNode.as_integer())
                {
                    return tl::unexpected(errors::ConfigError {
                        "invalid heartbeat_interval_ms in [settings] - must be an integer"});
                }
                settings.heartbeatInterval =
                    std::chrono::milliseconds(heartbeatIntervalNode.as_integer()->get());
            }

            auto dataDirNode = (*settingsTable)["data_directory"];
            if (!dataDirNode)
            {
                return tl::unexpected(errors::ConfigError {"missing data_directory in [settings]"});
            }
            if (!dataDirNode.as_string())
            {
                return tl::unexpected(errors::ConfigError {
                    "invalid data_directory in [settings] - must be a string"});
            }
            settings.dataDirectory = dataDirNode.as_string()->get();

            return settings;
        }

        tl::expected<Node, Error> parseNode(const toml::table& nodeTable)
        {
            Node node;

            // Parse node id (required)
            auto id = nodeTable["id"].as_string();
            if (!id)
            {
                return tl::unexpected(
                    errors::ConfigError {"missing or invalid 'id' in cluster node"});
            }
            node.id = id->get();

            // Parse node address (required)
            auto address = nodeTable["address"].as_string();
            if (!address)
            {
                return tl::unexpected(
                    errors::ConfigError {"missing or invalid 'address' in cluster node"});
            }
            node.address = address->get();

            // Parse kv_port (required)
            auto kvPort = nodeTable["kv_port"].as_integer();
            if (!kvPort)
            {
                return tl::unexpected(
                    errors::ConfigError {"missing or invalid 'kv_port' in cluster node"});
            }
            node.kvPort = static_cast<uint16_t>(kvPort->get());

            // Parse raft_port (required)
            auto raftPort = nodeTable["raft_port"].as_integer();
            if (!raftPort)
            {
                return tl::unexpected(
                    errors::ConfigError {"missing or invalid 'raft_port' in cluster node"});
            }
            node.raftPort = static_cast<uint16_t>(raftPort->get());

            return node;
        }

        tl::expected<std::vector<Node>, Error> parseNodes(const toml::table& config)
        {
            auto cluster = config["cluster"].as_table();
            if (!cluster)
            {
                return tl::unexpected(
                    errors::ConfigError {"missing [cluster] section in config file"});
            }

            auto nodesArray = (*cluster)["nodes"].as_array();
            if (!nodesArray)
            {
                return tl::unexpected(
                    errors::ConfigError {"missing or invalid cluster.nodes array"});
            }

            std::vector<Node> nodes;
            nodes.reserve(nodesArray->size());

            for (auto& nodeToml : *nodesArray)
            {
                auto nodeTable = nodeToml.as_table();
                if (!nodeTable)
                {
                    return tl::unexpected(
                        errors::ConfigError {"invalid node entry in cluster.nodes array"});
                }

                auto node = parseNode(*nodeTable);
                if (!node)
                {
                    return tl::unexpected(node.error());
                }

                nodes.push_back(std::move(*node));
            }

            if (nodes.empty())
            {
                return tl::unexpected(
                    errors::ConfigError {"no nodes defined in cluster configuration"});
            }

            return nodes;
        }
    }  // namespace

    tl::expected<Config, Error> loadConfig(std::string_view path)
    {
        if (!std::filesystem::exists(path))
        {
            return tl::unexpected(
                errors::ConfigError {"config file not found: " + std::string(path)});
        }

        toml::table config;
        try
        {
            config = toml::parse_file(path);
        }
        catch (const toml::parse_error& err)
        {
            return tl::unexpected(
                errors::ConfigError {"failed to parse TOML file: " + std::string(err.what())});
        }

        auto settings = parseSettings(config);
        if (!settings)
        {
            return tl::unexpected(settings.error());
        }

        auto nodes = parseNodes(config);
        if (!nodes)
        {
            return tl::unexpected(nodes.error());
        }

        return Config {.settings = std::move(*settings), .nodes = std::move(*nodes)};
    }
}  // namespace raft_cli::config