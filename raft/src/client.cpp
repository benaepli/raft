

#include "raft/client.hpp"
#include "utils/grpc_data.hpp"
#include "utils/grpc_errors.hpp"

#include <grpcpp/grpcpp.h>
#include <chrono>
#include "raft_protos/raft.grpc.pb.h"

namespace raft {
    using data::AppendEntriesRequest;
    using data::AppendEntriesResponse;
    using data::RequestVoteRequest;
    using data::RequestVoteResponse;

    namespace {
        class GrpcClient final : public Client {
        public:
            explicit GrpcClient(const std::shared_ptr<grpc::Channel> &channel)
                : stub_(raft_protos::Raft::NewStub(channel)) {
            }

            tl::expected<AppendEntriesResponse, Error> appendEntries(
                const AppendEntriesRequest &request,
                const RequestConfig &config
            ) override {
                grpc::ClientContext context;
                configureContext(context, config);
                raft_protos::AppendEntriesReply response;
                grpc::Status status = stub_->AppendEntries(&context, data::toProto(request), &response);

                if (!status.ok()) {
                    return tl::unexpected(errors::fromGrpcStatus(status));
                }

                return data::fromProto(response);
            }

            tl::expected<RequestVoteResponse, Error> requestVote(
                const RequestVoteRequest &request,
                const RequestConfig &config
            ) override {
                grpc::ClientContext context;
                configureContext(context, config);
                raft_protos::RequestVoteReply response;
                grpc::Status status = stub_->RequestVote(&context, data::toProto(request), &response);

                if (!status.ok()) {
                    return tl::unexpected(errors::fromGrpcStatus(status));
                }

                return data::fromProto(response);
            }

        private:
            static void configureContext(grpc::ClientContext &context, const RequestConfig &config) {
                auto deadline = std::chrono::system_clock::now() +
                                std::chrono::milliseconds(config.timeout);
                context.set_deadline(deadline);
            }

            std::unique_ptr<raft_protos::Raft::Stub> stub_;
        };
    } // namespace

    tl::expected<std::unique_ptr<Client>, Error> createClient(const std::string &address) {
        return std::make_unique<GrpcClient>(grpc::CreateChannel(address, grpc::InsecureChannelCredentials()));
    }
} // namespace raft
