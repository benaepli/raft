
#include <atomic>
#include <memory>
#include <mutex>
#include <utility>

#include "raft/inmemory/manager.hpp"

namespace raft::inmemory
{
    namespace
    {
        class ManagerImpl;

        // The network registers its handler with the manager and unregisters when destroyed.
        class Network : public raft::Network
        {
          public:
            Network(std::shared_ptr<ServiceHandler> handler, std::shared_ptr<ManagerImpl> manager)
                : handler_(std::move(handler))
                , manager_(std::move(manager))
            {
            }

            ~Network() override { shutdown(); }

            void shutdown();

            tl::expected<std::string, Error> start(const std::string& address) override;

            tl::expected<void, Error> stop() override;

          private:
            std::shared_ptr<ServiceHandler> handler_;
            std::shared_ptr<ManagerImpl> manager_;

            std::mutex mutex_;
            bool running_ = false;
            std::string address_;
        };

        class Client final : public raft::Client
        {
          public:
            Client(std::shared_ptr<ManagerImpl> manager, std::string address)
                : manager_(std::move(manager))
                , address_(std::move(address))
            {
            }

            ~Client() override = default;

            void appendEntries(data::AppendEntriesRequest request,
                               RequestConfig config,
                               std::function<void(tl::expected<data::AppendEntriesResponse, Error>)>
                                   callback) override;

            void requestVote(data::RequestVoteRequest request,
                             RequestConfig config,
                             std::function<void(tl::expected<data::RequestVoteResponse, Error>)>
                                 callback) override;

          private:
            std::shared_ptr<ManagerImpl> manager_;
            std::string address_;
        };

        class ManagerImpl final
            : public Manager
            , public std::enable_shared_from_this<ManagerImpl>
        {
          public:
            ManagerImpl() = default;
            ~ManagerImpl() override = default;

            tl::expected<std::unique_ptr<raft::Client>, Error> createClient(
                const std::string& address) override
            {
                return std::make_unique<Client>(shared_from_this(), address);
            }

            tl::expected<std::shared_ptr<raft::Network>, raft::Error> createNetwork(
                const NetworkCreateConfig& config) override
            {
                return std::make_shared<Network>(config.handler, shared_from_this());
            }

            tl::expected<void, Error> registerNetwork(
                const std::string& address, const std::shared_ptr<ServiceHandler>& handler);
            void unregisterNetwork(const std::string& address);

            void callAppendEntries(
                const std::string& address,
                const data::AppendEntriesRequest& request,
                RequestConfig config,
                std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback);

            void callRequestVote(
                const std::string& address,
                const data::RequestVoteRequest& request,
                RequestConfig config,
                std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback);

          private:
            std::mutex mutex_;
            std::unordered_map<std::string, std::weak_ptr<ServiceHandler>> networks_;
        };

        tl::expected<std::string, Error> Network::start(const std::string& address)
        {
            std::lock_guard lock {mutex_};
            if (running_)
            {
                return tl::make_unexpected(errors::AlreadyRunning {});
            }
            if (auto result = manager_->registerNetwork(address, handler_); !result)
            {
                return tl::make_unexpected(result.error());
            }
            running_ = true;
            address_ = address;
            return address;
        }

        void Network::shutdown()
        {
            std::lock_guard lock {mutex_};
            if (!running_)
            {
                return;
            }
            running_ = false;
            manager_->unregisterNetwork(address_);
        }

        tl::expected<void, Error> Network::stop()
        {
            shutdown();
            return {};
        }

        void Client::appendEntries(
            data::AppendEntriesRequest request,
            RequestConfig config,
            std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback)
        {
            manager_->callAppendEntries(address_, request, config, std::move(callback));
        }

        void Client::requestVote(
            data::RequestVoteRequest request,
            RequestConfig config,
            std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback)
        {
            manager_->callRequestVote(address_, request, config, std::move(callback));
        }

        tl::expected<void, Error> ManagerImpl::registerNetwork(
            const std::string& address, const std::shared_ptr<ServiceHandler>& handler)
        {
            std::lock_guard lock {mutex_};
            if (networks_.contains(address))
            {
                return tl::make_unexpected(errors::FailedToStart {});
            }
            networks_.insert({address, handler});
            return {};
        }

        void ManagerImpl::unregisterNetwork(const std::string& address)
        {
            std::lock_guard lock {mutex_};
            networks_.erase(address);
        }

        void ManagerImpl::callAppendEntries(
            const std::string& address,
            const data::AppendEntriesRequest& request,
            RequestConfig config,
            std::function<void(tl::expected<data::AppendEntriesResponse, Error>)> callback)
        {
            std::unique_lock lock {mutex_};
            auto handler = networks_[address].lock();
            lock.unlock();
            if (!handler)
            {
                callback(tl::make_unexpected(errors::Unknown {.message = "network not found"}));
                return;
            }
            handler->handleAppendEntries(request, std::move(callback));
        }

        void ManagerImpl::callRequestVote(
            const std::string& address,
            const data::RequestVoteRequest& request,
            RequestConfig config,
            std::function<void(tl::expected<data::RequestVoteResponse, Error>)> callback)
        {
            std::unique_lock lock {mutex_};
            auto handler = networks_[address].lock();
            lock.unlock();
            if (!handler)
            {
                callback(tl::make_unexpected(errors::Unknown {.message = "network not found"}));
                return;
            }
            handler->handleRequestVote(request, std::move(callback));
        }
    }  // namespace

    std::shared_ptr<Manager> createManager()
    {
        return std::make_shared<ManagerImpl>();
    }
}  // namespace raft::inmemory