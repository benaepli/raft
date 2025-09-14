#pragma once
#include <memory>
#include <string>

#include <tl/expected.hpp>

#include "raft/errors.hpp"
#include "raft/persister.hpp"

namespace raft::fs
{
    /// Creates a SQLite-based persister for Raft state.
    ///
    /// This function initializes a SQLite database at the specified path and creates
    /// the necessary tables for storing Raft metadata and log entries. The database
    /// will be created if it doesn't exist.
    /// @param path The file path where the SQLite database should be stored.
    /// @return A shared pointer to the persister on success, or an error on failure.
    tl::expected<std::shared_ptr<Persister>, Error> createSQLitePersister(const std::string& path);
}  // namespace raft::fs