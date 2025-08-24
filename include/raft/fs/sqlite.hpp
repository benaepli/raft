#pragma once
#include <memory>
#include <string>

#include <tl/expected.hpp>

#include "raft/errors.hpp"
#include "raft/persister.hpp"

namespace raft::fs
{
    tl::expected<std::shared_ptr<Persister>, Error> createSQLitePersister(const std::string& path);
}  // namespace raft::fs