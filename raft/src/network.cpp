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
        class GrpcServiceImpl final : public raft_protos::Raft::Service
        {
          public:
            explicit GrpcServiceImpl(ServiceHandler& handler)
                : handler_(handler)
            {
            }

            grpc::Status AppendEntries(grpc::ServerContext* context,
                                       const raft_protos::AppendEntriesRequest* request,
                                       raft_protos::AppendEntriesReply* response) override
            {
                auto internalRequest = data::fromProto(*request);
                auto result = handler_.get().handleAppendEntries(internalRequest);
                if (!result)
                {
                    return errors::toGrpcStatus(result.error());
                }
                *response = data::toProto(*result);
                return grpc::Status::OK;
            }

            grpc::Status RequestVote(grpc::ServerContext* context,
                                     const raft_protos::RequestVoteRequest* request,
                                     raft_protos::RequestVoteReply* response) override
            {
                auto internalRequest = data::fromProto(*request);
                auto result = handler_.get().handleRequestVote(internalRequest);
                if (!result)
                {
                    return errors::toGrpcStatus(result.error());
                }
                *response = data::toProto(*result);
                return grpc::Status::OK;
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
                if (isRunning())
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

                return address.find(":0") != std::string::npos
                    ? address.substr(0, address.find(":0")) + ":" + std::to_string(port)
                    : address;
            }

            tl::expected<void, Error> stop() override
            {
                if (!isRunning())
                {
                    return tl::make_unexpected(errors::NotRunning {});
                }

                server_->Shutdown();
                server_.reset();

                return {};
            }

          private:
            friend class GrpcServiceImpl;

            bool isRunning() const { return server_ != nullptr; }

            GrpcServiceImpl service_;
            std::unique_ptr<grpc::Server> server_;
        };
    }  // namespace

    tl::expected<std::shared_ptr<Network>, Error> createNetwork(const NetworkCreateConfig& config)
    {
        return std::make_shared<GrpcNetwork>(*config.handler);
    }
}  // namespace raft