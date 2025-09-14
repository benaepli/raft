#pragma once

#include "../errors.hpp"
#include "data.hpp"
#include "raft/enhanced/typed_server.hpp"

namespace raft_cli::store
{

    struct KVStoreConfig
    {
        uint16_t kvPort;

        std::string id;
        uint16_t raftPort;
        std::string sqliteDBPath;
        std::vector<raft::Peer> peers;
        uint16_t threadCount;
        raft::TimeoutInterval electionTimeout;
        std::chrono::nanoseconds heartbeatInterval;

        std::chrono::nanoseconds commitTimeout;
    };

    class KVStore
    {
      public:
        static tl::expected<KVStore, Error> create(KVStoreConfig const& config);
        ~KVStore();

        KVStore(KVStore const&) = delete;
        KVStore& operator=(KVStore const&) = delete;
        KVStore(KVStore&&) = default;
        KVStore& operator=(KVStore&&) = default;

        tl::expected<void, Error> start();
        void stop();

      private:
        explicit KVStore(KVStoreConfig const& config);

        std::string kvAddress_;
        std::string raftAddress_;

        std::shared_ptr<raft::Network> network_;
        std::shared_ptr<raft::Server> underlyingServer_;
        std::unique_ptr<raft::enhanced::typed::Server<data::Command, std::string>> server_;
    };
}  // namespace raft_cli::store