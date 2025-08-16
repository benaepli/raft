
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>

#include "raft/client.hpp"
#include "raft/persister.hpp"

namespace raft::impl
{
    struct PersistenceRequest
    {
        std::vector<std::byte> data;
        std::function<void()> callback;
    };

    // Handles persistence requests in batches in a separate thread. This calls the Persister's
    // saveState method whenever either the timer expires and the queue is not empty, or if the size
    // of the queue exceeds the maximum. We only persist the most recent state, but each callback
    // will be run.
    class PersistenceHandler
    {
      public:
        PersistenceHandler(std::shared_ptr<Persister> persister,
                           std::chrono::microseconds interval,
                           uint64_t maxEntries);

        ~PersistenceHandler();

        void addRequest(PersistenceRequest request);

      private:
        void resetTimer();
        void run();
        void persist(std::unique_lock<std::mutex>& lock);
        [[nodiscard]] std::chrono::time_point<std::chrono::steady_clock> nextPersistTime() const;

        std::chrono::time_point<std::chrono::steady_clock> lastPersisted_;
        std::shared_ptr<Persister> persister_;
        std::chrono::microseconds interval_;
        uint64_t maxEntries_;

        std::mutex mutex_;
        std::queue<std::function<void()> > callbacks_;
        std::vector<std::byte> data_;
        std::condition_variable condition_;

        std::atomic<bool> running_ = true;
        std::thread thread_;
    };
}  // namespace raft::impl
