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
        proto.set_term(entry.term);
        proto.set_data(toString(entry.data));

        return proto;
    }

    inline LogEntry fromProto(const raft_protos::LogEntry& proto)
    {
        return LogEntry {
            .term = proto.term(),
            .data = toBytes(proto.data()),
        };
    }

    inline raft_protos::AppendEntriesRequest toProto(const AppendEntriesRequest& request)
    {
        raft_protos::AppendEntriesRequest proto;
        proto.set_term(request.term);
        proto.set_leader_id(request.leaderID);
        proto.set_prev_log_index(request.prevLogIndex);
        proto.set_prev_log_term(request.prevLogTerm);
        for (const auto& entry : request.entries)
        {
            *proto.add_entries() = toProto(entry);
        }
        proto.set_leader_commit(request.leaderCommit);
        return proto;
    }

    inline AppendEntriesRequest fromProto(const raft_protos::AppendEntriesRequest& proto)
    {
        AppendEntriesRequest request {
            .term = proto.term(),
            .leaderID = proto.leader_id(),
            .prevLogIndex = proto.prev_log_index(),
            .prevLogTerm = proto.prev_log_term(),
            .leaderCommit = proto.leader_commit(),
        };
        for (const auto& entry : proto.entries())
        {
            request.entries.push_back(fromProto(entry));
        }
        return request;
    }

    inline raft_protos::AppendEntriesReply toProto(const AppendEntriesResponse& response)
    {
        raft_protos::AppendEntriesReply proto;
        proto.set_term(response.term);
        proto.set_success(response.success);
        return proto;
    }

    inline AppendEntriesResponse fromProto(const raft_protos::AppendEntriesReply& proto)
    {
        return AppendEntriesResponse {
            .term = proto.term(),
            .success = proto.success(),
        };
    }

    inline raft_protos::RequestVoteRequest toProto(const RequestVoteRequest& request)
    {
        raft_protos::RequestVoteRequest proto;
        proto.set_term(request.term);
        proto.set_candidate_id(request.candidateID);
        proto.set_last_log_index(request.lastLogIndex);
        proto.set_last_log_term(request.lastLogTerm);
        return proto;
    }

    inline RequestVoteRequest fromProto(const raft_protos::RequestVoteRequest& proto)
    {
        return RequestVoteRequest {
            .term = proto.term(),
            .candidateID = proto.candidate_id(),
            .lastLogIndex = proto.last_log_index(),
            .lastLogTerm = proto.last_log_term(),
        };
    }

    inline raft_protos::RequestVoteReply toProto(const RequestVoteResponse& response)
    {
        raft_protos::RequestVoteReply proto;
        proto.set_term(response.term);
        proto.set_vote_granted(response.voteGranted);
        return proto;
    }

    inline RequestVoteResponse fromProto(const raft_protos::RequestVoteReply& proto)
    {
        return RequestVoteResponse {
            .term = proto.term(),
            .voteGranted = proto.vote_granted(),
        };
    }

    inline raft_protos::PersistedState toProto(const PersistedState& state)
    {
        raft_protos::PersistedState proto;
        proto.set_term(state.term);
        for (const auto& entry : state.entries)
        {
            *proto.add_entries() = toProto(entry);
        }
        proto.set_commit_index(state.commitIndex);
        return proto;
    }

    inline PersistedState fromProto(const raft_protos::PersistedState& proto)
    {
        PersistedState state {
            .term = proto.term(),
            .commitIndex = proto.commit_index(),
        };
        for (const auto& entry : proto.entries())
        {
            state.entries.push_back(fromProto(entry));
        }
        return state;
    }
}  // namespace raft::data
