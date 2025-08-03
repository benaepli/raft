

#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "raft/client.hpp"

#include <grpcpp/grpcpp.h>

#include "raft_protos/raft.grpc.pb.h"
#include "utils/grpc_data.hpp"
#include "utils/grpc_errors.hpp"

namespace raft
{
    using data::AppendEntriesRequest;
    using data::AppendEntriesResponse;
    using data::RequestVoteRequest;
    using data::RequestVoteResponse;

    namespace
    {
        class GrpcClient final : public Client
        {
          public:
            explicit GrpcClient(const std::shared_ptr<grpc::Channel>& channel)
                : stub_(raft_protos::Raft::NewStub(channel))
            {
            }

            void appendEntries(
                AppendEntriesRequest request,
                RequestConfig config,
                std::function<void(tl::expected<AppendEntriesResponse, Error>)> callback) override
            {
                auto context = std::make_unique<grpc::ClientContext>();
                configureContext(*context, config);

                auto protoRequest = data::toProto(request);

                struct Parameters
                {
                    std::unique_ptr<grpc::ClientContext> context;
                    std::unique_ptr<raft_protos::AppendEntriesRequest> request;
                    std::unique_ptr<raft_protos::AppendEntriesResponse> reply;
                    std::function<void(tl::expected<AppendEntriesResponse, Error>)> callback;
                };

                auto params = std::make_shared<Parameters>();
                params->context = std::move(context);
                params->request =
                    std::make_unique<raft_protos::AppendEntriesRequest>(std::move(protoRequest));
                params->reply = std::make_unique<raft_protos::AppendEntriesResponse>();
                params->callback = std::move(callback);

                stub_->async()->AppendEntries(
                    params->context.get(),
                    params->request.get(),
                    params->reply.get(),
                    [params](grpc::Status status) mutable
                    {
                        if (status.ok())
                        {
                            params->callback(data::fromProto(*params->reply));
                        }
                        else
                        {
                            params->callback(tl::unexpected(errors::fromGrpcStatus(status)));
                        }
                    });
            }

            void requestVote(RequestVoteRequest request,
                             RequestConfig config,
                             std::function<void(tl::expected<RequestVoteResponse, Error>)> callback) override
            {
                auto context = std::make_unique<grpc::ClientContext>();
                configureContext(*context, config);

                auto protoRequest = data::toProto(request);

                struct Parameters
                {
                    std::unique_ptr<grpc::ClientContext> context;
                    std::unique_ptr<raft_protos::RequestVoteRequest> request;
                    std::unique_ptr<raft_protos::RequestVoteResponse> reply;
                    std::function<void(tl::expected<RequestVoteResponse, Error>)> callback;
                };

                auto params = std::make_shared<Parameters>();
                params->context = std::move(context);
                params->request =
                    std::make_unique<raft_protos::RequestVoteRequest>(std::move(protoRequest));
                params->reply = std::make_unique<raft_protos::RequestVoteResponse>();
                params->callback = std::move(callback);

                stub_->async()->RequestVote(
                    params->context.get(),
                    params->request.get(),
                    params->reply.get(),
                    [params](grpc::Status status) mutable
                    {
                        if (status.ok())
                        {
                            params->callback(data::fromProto(*params->reply));
                        }
                        else
                        {
                            params->callback(tl::unexpected(errors::fromGrpcStatus(status)));
                        }
                    });
            }

          private:
            static void configureContext(grpc::ClientContext& context, const RequestConfig& config)
            {
                auto deadline =
                    std::chrono::system_clock::now() + std::chrono::milliseconds(config.timeout);
                context.set_deadline(deadline);
            }

            std::unique_ptr<raft_protos::Raft::Stub> stub_;
        };

        class GrpcClientFactory final : public ClientFactory
        {
          public:
            ~GrpcClientFactory() override = default;

            tl::expected<std::unique_ptr<Client>, Error> createClient(
                const std::string& address) override
            {
                return std::make_unique<GrpcClient>(
                    grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
            }
        };
    }  // namespace

    tl::expected<std::unique_ptr<Client>, Error> createClient(const std::string& address)
    {
        return GrpcClientFactory().createClient(address);
    }
}  // namespace raft
