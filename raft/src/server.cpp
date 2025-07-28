#include "raft/server.hpp"
#include "utils/grpc_data.hpp"
#include "utils/grpc_errors.hpp"

#include <grpcpp/grpcpp.h>
#include "raft_protos/raft.grpc.pb.h"

namespace raft {
    using data::AppendEntriesRequest;
    using data::AppendEntriesResponse;
    using data::RequestVoteRequest;
    using data::RequestVoteResponse;

    namespace {
        class GrpcNetwork;

        class GrpcServiceImpl final : public raft_protos::Raft::Service {
        public:
            explicit GrpcServiceImpl(ServiceHandler &handler) : handler_(handler) {
            }

            grpc::Status AppendEntries(
                grpc::ServerContext *context,
                const raft_protos::AppendEntriesRequest *request,
                raft_protos::AppendEntriesReply *response
            ) override;

            grpc::Status RequestVote(
                grpc::ServerContext *context,
                const raft_protos::RequestVoteRequest *request,
                raft_protos::RequestVoteReply *response
            ) override;

        private:
            ServiceHandler &handler_;
        };

        class GrpcNetwork final : public Network {
        public:
            explicit GrpcNetwork(ServiceHandler &handler, std::optional<uint16_t> port = std::nullopt) 
                : service_(handler), port_(port) {
            }

            tl::expected<void, Error> start() override {
                return tl::make_unexpected(Error::Unimplemented);
            }

            tl::expected<void, Error> stop() override {
                return tl::make_unexpected(Error::Unimplemented);
            }

        private:
            friend class GrpcServiceImpl;

            GrpcServiceImpl service_;
            std::optional<uint16_t> port_;
            std::unique_ptr<grpc::Server> server_;
        };
    } // namespace

    tl::expected<std::shared_ptr<Server>, Error> createServer(const ServerCreateConfig &config) {
        return std::make_shared<GrpcServer>(config);
    }

    tl::expected<std::shared_ptr<Network>, Error> createNetwork(const NetworkCreateConfig &config) {
        return tl::make_unexpected(Error::Unimplemented);
    }
} // namespace raft
