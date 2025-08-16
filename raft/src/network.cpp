#include "raft/network.hpp"

#include <grpcpp/grpcpp.h>

#include "raft_protos/raft.grpc.pb.h"
#include "utils/grpc_data.hpp"
#include "utils/grpc_errors.hpp"

namespace raft
{
    namespace
    {
        class GrpcNetwork;

        // GrpcServiceImpl forwards requests to the ServiceHandler and converts the data between the
        // internal and external representations.
        class GrpcServiceImpl final : public raft_protos::Raft::CallbackService
        {
          public:
            explicit GrpcServiceImpl(ServiceHandler& handler)
                : handler_(handler)
            {
            }

            grpc::ServerUnaryReactor* AppendEntries(
                grpc::CallbackServerContext* context,
                const raft_protos::AppendEntriesRequest* request,
                raft_protos::AppendEntriesResponse* response) override
            {
                auto* reactor = context->DefaultReactor();
                auto internalRequest = data::fromProto(*request);

                auto callback =
                    [reactor, response](tl::expected<data::AppendEntriesResponse, Error> result)
                {
                    if (!result)
                    {
                        reactor->Finish(errors::toGrpcStatus(result.error()));
                        return;
                    }
                    *response = data::toProto(*result);
                    reactor->Finish(grpc::Status::OK);
                };
                handler_.get().handleAppendEntries(internalRequest, callback);
                return reactor;
            }

            grpc::ServerUnaryReactor* RequestVote(
                grpc::CallbackServerContext* context,
                const raft_protos::RequestVoteRequest* request,
                raft_protos::RequestVoteResponse* response) override
            {
                auto* reactor = context->DefaultReactor();
                auto internalRequest = data::fromProto(*request);

                auto callback =
                    [reactor, response](tl::expected<data::RequestVoteResponse, Error> result)
                {
                    if (!result)
                    {
                        reactor->Finish(errors::toGrpcStatus(result.error()));
                        return;
                    }
                    *response = data::toProto(*result);
                    reactor->Finish(grpc::Status::OK);
                };
                handler_.get().handleRequestVote(internalRequest, callback);
                return reactor;
            }

          private:
            std::reference_wrapper<ServiceHandler> handler_;
        };

        class GrpcNetwork final : public Network
        {
          public:
            explicit GrpcNetwork(ServiceHandler& handler)
                : service_(handler)
            {
            }

            ~GrpcNetwork() override { auto _ = stop(); }

            tl::expected<std::string, Error> start(const std::string& address) override
            {
                std::lock_guard lock {mutex_};
                if (running_)
                {
                    return tl::make_unexpected(errors::AlreadyRunning {});
                }

                grpc::ServerBuilder builder;

                int port;
                builder.AddListeningPort(address, grpc::InsecureServerCredentials(), &port);
                builder.RegisterService(&service_);

                server_ = builder.BuildAndStart();
                if (!server_)
                {
                    return tl::make_unexpected(errors::FailedToStart {});
                }

                running_ = true;
                return address.find(":0") != std::string::npos
                    ? address.substr(0, address.find(":0")) + ":" + std::to_string(port)
                    : address;
            }

            tl::expected<void, Error> stop() override
            {
                std::lock_guard lock {mutex_};
                if (!running_)
                {
                    return tl::make_unexpected(errors::NotRunning {});
                }
                running_ = false;

                server_->Shutdown();
                server_->Wait();
                server_.reset();

                return {};
            }

          private:
            friend class GrpcServiceImpl;

            bool isRunning() const { return server_ != nullptr; }

            std::mutex mutex_;
            GrpcServiceImpl service_;
            std::unique_ptr<grpc::Server> server_;
            bool running_ = false;
        };
    }  // namespace

    tl::expected<std::shared_ptr<Network>, Error> createNetwork(const NetworkCreateConfig& config)
    {
        return std::make_shared<GrpcNetwork>(*config.handler);
    }
}  // namespace raft