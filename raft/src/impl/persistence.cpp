#include "persistence.hpp"

#include <spdlog/spdlog.h>

namespace raft::impl
{
    PersistenceHandler::PersistenceHandler(std::shared_ptr<Persister> persister,
                                           std::chrono::microseconds interval,
                                           uint64_t maxEntries)
        : persister_(std::move(persister))
        , interval_(interval)
        , maxEntries_(maxEntries)
    {
        resetTimer();
        thread_ = std::thread {&PersistenceHandler::run, this};
    }

    PersistenceHandler::~PersistenceHandler()
    {
        {
            std::lock_guard lock {mutex_};
            running_ = false;
        }
        condition_.notify_all();
        thread_.join();
    }

    void PersistenceHandler::addRequest(PersistenceRequest request)
    {
        std::lock_guard lock {mutex_};
        callbacks_.push(std::move(request.callback));
        data_ = std::move(request.data);
        condition_.notify_all();
    }

    void PersistenceHandler::resetTimer()
    {
        lastPersisted_ = std::chrono::steady_clock::now();
    }

    void PersistenceHandler::run()
    {
        spdlog::trace("[PersistenceHandler {}] background thread started",
                      static_cast<void*>(this));
        while (true)
        {
            std::unique_lock lock {mutex_};
            condition_.wait_until(lock,
                                  nextPersistTime(),
                                  [this]
                                  {
                                      auto now = std::chrono::steady_clock::now();
                                      bool timeout = now >= nextPersistTime();
                                      return timeout || callbacks_.size() >= maxEntries_
                                          || !running_;
                                  });
            if (!running_)
            {
                spdlog::trace("[PersistenceHandler {}] background thread stopped",
                              static_cast<void*>(this));
                return;
            }
            if (callbacks_.empty())
            {
                resetTimer();
                continue;
            }

            resetTimer();
            persist(lock);
        }
    }

    void PersistenceHandler::persist(std::unique_lock<std::mutex>& lock)
    {
        std::queue<std::function<void()> > requests;
        requests.swap(callbacks_);

        std::vector<std::byte> data;
        data.swap(data_);

        lock.unlock();

        persister_->saveState(data);
        int counter = 0;
        spdlog::trace("[PersistenceHandler {}] begin persist, request size {}",
                      static_cast<void*>(this),
                      requests.size());
        while (!requests.empty())
        {
            auto& callback = requests.front();
            callback();
            requests.pop();
            counter++;
        }
    }

    std::chrono::time_point<std::chrono::steady_clock> PersistenceHandler::nextPersistTime() const
    {
        return lastPersisted_ + interval_;
    }
}  // namespace raft::impl