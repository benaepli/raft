#include "server.hpp"

#include "raft/fs/sqlite.hpp"

namespace raft_cli::store
{
    KVStore::KVStore(KVStoreConfig const& config)
        : kvAddress_("127.0.0.1:" + std::to_string(config.kvPort))
        , raftAddress_("127.0.0.1:" + std::to_string(config.raftPort))
    {
    }

    KVStore::~KVStore()
    {
        stop();
    }

    tl::expected<void, Error> KVStore::start()
    {
        return {};
    }

    void KVStore::stop() {}

    tl::expected<KVStore, Error> KVStore::create(KVStoreConfig const& config)
    {
        auto persister = raft::fs::createSQLitePersister(config.sqliteDBPath);
        if (!persister)
        {
            return tl::unexpected(errors::Unknown {
                .message = fmt::format("failed to create persister: {}", persister.error())});
        }

        auto clientFactory = raft::createClientFactory();

        auto serverConfig = raft::ServerCreateConfig {
            .id = config.id,
            .clientFactory = clientFactory,
            .peers = config.peers,
            .persister = *persister,
            .electionTimeout = config.electionTimeout,
            .heartbeatInterval = config.heartbeatInterval,
        };
        auto underlyingServer = raft::createServer(serverConfig);
        if (!underlyingServer)
        {
            return tl::unexpected(errors::Unknown {
                .message = fmt::format("failed to create server: {}", underlyingServer.error())});
        }

        raft::NetworkCreateConfig const networkConfig {*underlyingServer};
        auto network = raft::createNetwork(networkConfig);
        if (!network)
        {
            return tl::unexpected(errors::Unknown {
                .message = fmt::format("failed to create network: {}", network.error())});
        }

        auto enhancedConfig =
            raft::enhanced::typed::ServerCreateConfig<data::Command, std::string> {
                .server = *underlyingServer,
                .commitTimeout = config.commitTimeout,
                .threadCount = config.threadCount,
            };
        auto server = std::make_unique<raft::enhanced::typed::Server<data::Command, std::string>>(
            enhancedConfig);

        auto store = KVStore(config);
        store.network_ = *network;
        store.underlyingServer_ = *underlyingServer;
        store.server_ = std::move(server);
        return store;
    }

}  // namespace raft_cli::store