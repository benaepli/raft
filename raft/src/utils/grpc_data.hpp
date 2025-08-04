#pragma once

#include "raft/client.hpp"
#include "raft_protos/raft.pb.h"
#include "serialize.hpp"

namespace raft::data
{
    inline std::vector<std::byte> toBytes(const std::string& str)
    {
        std::vector<std::byte> bytes(str.size());
        std::memcpy(bytes.data(), str.c_str(), str.size());
        return bytes;
    }

    inline std::string toString(const std::vector<std::byte>& bytes)
    {
        return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
    }

    inline raft_protos::LogEntry toProto(const LogEntry& entry)
    {
        raft_protos::LogEntry proto;
        proto.set_term(static_cast<int64_t>(entry.term));
        proto.set_data(toString(entry.data));

        return proto;
    }

    inline LogEntry fromProto(const raft_protos::LogEntry& proto)
    {
        return LogEntry {
            .term = static_cast<uint64_t>(proto.term()),
            .data = toBytes(proto.data()),
        };
    }

    inline raft_protos::AppendEntriesRequest toProto(const AppendEntriesRequest& request)
    {
        raft_protos::AppendEntriesRequest proto;
        proto.set_term(static_cast<int64_t>(request.term));
        proto.set_leader_id(request.leaderID);
        proto.set_prev_log_index(static_cast<int64_t>(request.prevLogIndex));
        proto.set_prev_log_term(static_cast<int64_t>(request.prevLogTerm));
        for (const auto& entry : request.entries)
        {
            *proto.add_entries() = toProto(entry);
        }
        proto.set_leader_commit(static_cast<int64_t>(request.leaderCommit));
        return proto;
    }

    inline AppendEntriesRequest fromProto(const raft_protos::AppendEntriesRequest& proto)
    {
        AppendEntriesRequest request {
            .term = static_cast<uint64_t>(proto.term()),
            .leaderID = proto.leader_id(),
            .prevLogIndex = static_cast<uint64_t>(proto.prev_log_index()),
            .prevLogTerm = static_cast<uint64_t>(proto.prev_log_term()),
            .leaderCommit = static_cast<uint64_t>(proto.leader_commit()),
        };
        for (const auto& entry : proto.entries())
        {
            request.entries.push_back(fromProto(entry));
        }
        return request;
    }

    inline raft_protos::AppendEntriesResponse toProto(const AppendEntriesResponse& response)
    {
        raft_protos::AppendEntriesResponse proto;
        proto.set_term(static_cast<int64_t>(response.term));
        proto.set_success(response.success);
        return proto;
    }

    inline AppendEntriesResponse fromProto(const raft_protos::AppendEntriesResponse& proto)
    {
        return AppendEntriesResponse {
            .term = static_cast<uint64_t>(proto.term()),
            .success = proto.success(),
        };
    }

    inline raft_protos::RequestVoteRequest toProto(const RequestVoteRequest& request)
    {
        raft_protos::RequestVoteRequest proto;
        proto.set_term(static_cast<int64_t>(request.term));
        proto.set_candidate_id(request.candidateID);
        proto.set_last_log_index(static_cast<int64_t>(request.lastLogIndex));
        proto.set_last_log_term(static_cast<int64_t>(request.lastLogTerm));
        return proto;
    }

    inline RequestVoteRequest fromProto(const raft_protos::RequestVoteRequest& proto)
    {
        return RequestVoteRequest {
            .term = static_cast<uint64_t>(proto.term()),
            .candidateID = proto.candidate_id(),
            .lastLogIndex = static_cast<uint64_t>(proto.last_log_index()),
            .lastLogTerm = static_cast<uint64_t>(proto.last_log_term()),
        };
    }

    inline raft_protos::RequestVoteResponse toProto(const RequestVoteResponse& response)
    {
        raft_protos::RequestVoteResponse proto;
        proto.set_term(static_cast<int64_t>(response.term));
        proto.set_vote_granted(response.voteGranted);
        return proto;
    }

    inline RequestVoteResponse fromProto(const raft_protos::RequestVoteResponse& proto)
    {
        return RequestVoteResponse {
            .term = static_cast<uint64_t>(proto.term()),
            .voteGranted = proto.vote_granted(),
        };
    }

    inline raft_protos::PersistedState toProto(const PersistedState& state)
    {
        raft_protos::PersistedState proto;
        proto.set_term(static_cast<int64_t>(state.term));
        for (const auto& entry : state.entries)
        {
            *proto.add_entries() = toProto(entry);
        }
        if (state.votedFor.has_value())
        {
            proto.set_voted_for(*state.votedFor);
        }
        return proto;
    }

    inline PersistedState fromProto(const raft_protos::PersistedState& proto)
    {
        PersistedState state {
            .term = static_cast<uint64_t>(proto.term()),
        };
        for (const auto& entry : proto.entries())
        {
            state.entries.push_back(fromProto(entry));
        }
        if (proto.has_voted_for())
        {
            state.votedFor = proto.voted_for();
        }
        return state;
    }
}  // namespace raft::data
