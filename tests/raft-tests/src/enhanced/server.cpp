#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <utility>

#include "raft/enhanced/server.hpp"

#include <fmt/format.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../mocks/persister.hpp"
#include "../server_tester.hpp"
#include "raft/fmt/errors.hpp"
#include "raft/inmemory/manager.hpp"

using raft::testing::ServerTester;

namespace
{
    namespace
    {
        template<typename T>
        std::vector<std::byte> serialize(T data)
        {
            auto byteSpan = std::as_writable_bytes(data);
            return std::vector<std::byte>(byteSpan.begin(), byteSpan.end());
        }
    }  // namespace

    using raft::testing::NoOpPersister;

    /// Enhanced server wrapper that creates enhanced servers for all servers
    class EnhancedServerTester
    {
      public:
        explicit EnhancedServerTester(std::vector<std::string> ids)
            : tester_(std::move(ids), std::make_shared<NoOpPersister>())
        {
            // Create enhanced servers for all servers
            for (const auto& serverAndNetwork : tester_.servers)
            {
                auto config = raft::enhanced::ServerCreateConfig {
                    .network = serverAndNetwork.network,
                    .server = serverAndNetwork.server,
                    .commitTimeout = std::chrono::seconds(5),
                    .threadCount = 1,
                    .commitCallback = std::nullopt,
                };

                enhancedServers_[serverAndNetwork.id] =
                    std::make_unique<raft::enhanced::Server>(std::move(config));
            }
        }

        /// Finds the current leader and returns the enhanced server for that leader
        raft::enhanced::Server* checkOneLeader()
        {
            auto leaderResult = tester_.checkOneLeader();
            EXPECT_TRUE(leaderResult.has_value())
                << "Failed to find leader: " << leaderResult.error();

            if (!leaderResult.has_value())
            {
                return nullptr;
            }

            std::string leaderID = *leaderResult;
            auto it = enhancedServers_.find(leaderID);
            if (it != enhancedServers_.end())
            {
                return it->second.get();
            }

            EXPECT_TRUE(false) << "Leader server not found in enhanced servers";
            return nullptr;
        }

      private:
        ServerTester tester_;
        std::unordered_map<std::string, std::unique_ptr<raft::enhanced::Server>> enhancedServers_;
    };
}  // namespace

TEST(EnhancedServerTest, SimpleCommitOnLeader)
{
    EnhancedServerTester tester({"A", "B", "C"});

    auto* enhancedServer = tester.checkOneLeader();
    ASSERT_NE(enhancedServer, nullptr);

    // Test data to commit
    std::array state {0, 1, 2};
    std::vector<std::byte> dataBytes = serialize(std::span(state));

    raft::enhanced::RequestInfo requestInfo {
        .clientID = "test-client-1",
        .requestID = 1,
    };

    // Variables for synchronization
    std::mutex callbackMutex;
    std::condition_variable callbackCV;
    bool callbackCalled = false;
    tl::expected<raft::enhanced::LocalCommitInfo, raft::Error> commitResult;

    // Perform the commit with a callback
    enhancedServer->commit(requestInfo,
                           dataBytes,
                           [&](tl::expected<raft::enhanced::LocalCommitInfo, raft::Error> result)
                           {
                               std::lock_guard lock(callbackMutex);
                               commitResult = std::move(result);
                               callbackCalled = true;
                               callbackCV.notify_all();
                           });

    // Wait for callback to be called
    {
        std::unique_lock<std::mutex> lock(callbackMutex);
        callbackCV.wait(lock, [&] { return callbackCalled; });
    }

    // Verify the commit succeeded
    EXPECT_TRUE(commitResult.has_value())
        << "Commit failed with error: "
        << (!commitResult.has_value() ? fmt::format("{}", commitResult.error()) : "");

    EXPECT_EQ(commitResult->data, dataBytes) << "Committed data does not match";
    EXPECT_FALSE(commitResult->duplicate) << "First commit should not be marked as duplicate";
}