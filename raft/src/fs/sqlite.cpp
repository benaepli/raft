#include "raft/fs/sqlite.hpp"

namespace raft::fs
{
    namespace
    {
        class SQLitePersister final : public raft::Persister
        {
        };
    }  // namespace

    tl::expected<std::shared_ptr<Persister>, Error> createSQLitePersister(const std::string& path)
    {
    }

}  // namespace raft::fs