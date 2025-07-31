#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

namespace raft::common
{
    template<typename T>
    class MPSCQueue
    {
      public:
        MPSCQueue() = default;

        MPSCQueue(const MPSCQueue&) = delete;

        MPSCQueue& operator=(const MPSCQueue&) = delete;

        void push(T value)
        {
            std::lock_guard lock {mutex_};
            queue_.push(std::move(value));
            cv_.notify_one();
        }

        std::optional<T> pop()
        {
            std::unique_lock lock {mutex_};
            cv_.wait(lock, [this] { return !queue_.empty() || closed_; });
            if (queue_.empty())
            {
                return std::nullopt;
            }
            T value = std::move(queue_.front());
            queue_.pop();
            return value;
        }

        void close()
        {
            std::lock_guard lock {mutex_};
            closed_ = true;
            cv_.notify_one();
        }

      private:
        std::queue<T> queue_;
        std::mutex mutex_;
        std::condition_variable cv_;
        bool closed_ = false;
    };
}  // namespace raft::common
