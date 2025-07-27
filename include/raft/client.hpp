#pragma once

#include <cstdint>
#include <string>
#include <variant>
#include <tl/expected.hpp>

namespace raft {
    namespace errors {
        struct Timeout {
        };

        struct Unimplemented {
        };

        struct InvalidArgument {
            std::string message;
        };
    } // namespace errors

    using Error = std::variant<
        errors::Timeout,
        errors::Unimplemented,
        errors::InvalidArgument
    >;

    namespace data {
        // The request message for AppendEntries.
        struct AppendEntriesRequest {
            // The current term.
            int64_t term;
            // The candidate's ID.
            std::string candidateID;
            // The index of the candidate's last log entry.
            int64_t lastLogIndex;
            // The term of the candidate's last log entry.
            int64_t lastLogTerm;
        };

        // The reply message for AppendEntries.
        struct AppendEntriesResponse {
            // The current term.
            int64_t term;
            // True if the follower contained the entry matching prevLogIndex and prevLogTerm.
            bool success;
        };
    } // namespace data

    // RequestConfig defines the configuration for a
    struct RequestConfig {
        // The timeout in milliseconds for the request.
        uint64_t timeout = 1000;
    };

    class Client {
    public:
        virtual ~Client() = default;

        virtual tl::expected<data::AppendEntriesResponse, Error> appendEntries(
            const data::AppendEntriesRequest &request,
            const RequestConfig &config = RequestConfig()
        ) = 0;
    };
} // namespace raft
