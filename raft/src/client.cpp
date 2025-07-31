

#include <chrono>

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

            asio::awaitable<tl::expected<AppendEntriesResponse, Error>> appendEntries(
                const AppendEntriesRequest& request, const RequestConfig& config) override
            {
                // TODO: Implement async version
                co_return tl::unexpected(Error {});
            }

            asio::awaitable<tl::expected<RequestVoteResponse, Error>> requestVote(
                const RequestVoteRequest& request, const RequestConfig& config) override
            {
                // TODO: Implement async version
                co_return tl::unexpected(Error {});
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
