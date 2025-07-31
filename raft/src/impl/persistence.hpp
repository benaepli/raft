
#pragma once

#include <chrono>
#include <memory>
#include <queue>
#include <thread>

#include "raft/client.hpp"
#include "raft/persister.hpp"

namespace raft::impl {
    struct PersistenceRequest {
        std::vector<std::byte> data;
        std::function<void()> callback;
    };

    // Handles persistence requests in batches in a separate thread. This calls the Persister's saveState method
    // whenever either the timer expires and the queue is not empty, or if the size of the queue exceeds
    // the maximum. We only persist the most recent state, but each callback will be run.
    class PersistenceHandler {
    public:
        PersistenceHandler(std::shared_ptr<Persister> persister,
                           std::chrono::microseconds interval,
                           uint64_t maxEntries) : persister_(std::move(persister)),
                                                  interval_(interval),
                                                  maxEntries_(maxEntries),
                                                  thread_(&PersistenceHandler::run, this) {
            resetTimer();
        }

        ~PersistenceHandler() {
            running_ = false;
            condition_.notify_all();
            thread_.join();
        }

        void addRequest(PersistenceRequest request) {
            std::lock_guard lock{mutex_};
            callbacks_.push(std::move(request.callback));
            data_ = std::move(request.data);
            condition_.notify_one();
        }

    private:
        void resetTimer() {
            lastPersisted_ = std::chrono::steady_clock::now();
        }


        void run() {
            while (true) {
                std::unique_lock lock{mutex_};

                // First, wait indefinitely if the queue is empty.
                condition_.wait(lock, [this] { return !callbacks_.empty() || !running_; });
                if (!running_) {
                    return;
                }

                // Then, wait until either the timer expires or the queue contains more than MAX_LOG_ENTRIES.
                condition_.wait_until(lock, nextPersistTime(), [this] {
                    return callbacks_.size() >= maxEntries_ || !running_;
                });
                if (!running_) {
                    return;
                }

                persist(lock);
                resetTimer();
            }
        }

        // Persist all requests in the queue. This requires the caller to obtain a lock on the mutex.
        void persist(std::unique_lock<std::mutex> &lock) {
            std::queue<std::function<void()> > requests;
            requests.swap(callbacks_);

            std::vector<std::byte> data;
            data.swap(data_);

            lock.unlock();

            persister_->saveState(data);
            while (!requests.empty()) {
                auto &callback = requests.front();
                callback();
                requests.pop();
            }
        }

        [[nodiscard]] std::chrono::time_point<std::chrono::steady_clock> nextPersistTime() const {
            return lastPersisted_ + interval_;
        }


        std::chrono::time_point<std::chrono::steady_clock> lastPersisted_;
        std::shared_ptr<Persister> persister_;
        std::chrono::microseconds interval_;
        uint64_t maxEntries_;

        std::mutex mutex_;
        std::queue<std::function<void()> > callbacks_;
        std::vector<std::byte> data_;
        std::condition_variable condition_;

        std::thread thread_;
        std::atomic<bool> running_ = true;
    };
} // namespace raft::impl
