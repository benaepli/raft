#include "raft/fs/sqlite.hpp"

#include <spdlog/spdlog.h>

#include "SQLiteCpp/Database.h"
#include "SQLiteCpp/Transaction.h"

namespace raft::fs
{
    namespace
    {
        class SQLitePersister final : public raft::Persister
        {
          public:
            explicit SQLitePersister(const std::string& path)
                : path_(path)
            {
            }
            tl::expected<void, Error> init()
            {
                try
                {
                    db_ = SQLite::Database(path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
                    // We use a simple key-value structure where the key (id=1) is always the same.
                    db_->exec(
                        "CREATE TABLE IF NOT EXISTS raft_state ("
                        "id INTEGER PRIMARY KEY, "
                        "state BLOB NOT NULL);");
                }
                catch (const SQLite::Exception& e)
                {
                    spdlog::error("[{}] {}", path_, e.what());
                    return tl::make_unexpected(errors::FailedToStart {});
                }
                return {};
            }

            tl::expected<void, Error> saveState(std::vector<std::byte> state) noexcept override
            {
                try
                {
                    // A transaction ensures that the entire operation succeeds or fails.
                    // If the application crashes mid-operation, the database will automatically
                    // roll back, leaving the old state intact.
                    SQLite::Transaction transaction(*db_);

                    SQLite::Statement query(
                        *db_, "INSERT OR REPLACE INTO raft_state (id, state) VALUES (1, ?)");

                    if (state.empty())
                    {
                        query.bind(1, "", 0);
                    }
                    else
                    {
                        query.bind(1, state.data(), static_cast<int>(state.size()));
                    }
                    query.exec();

                    transaction.commit();
                }
                catch (const std::exception& e)
                {
                    return tl::make_unexpected(errors::PersistenceFailed {.message = e.what()});
                }
                return {};
            }

            tl::expected<std::vector<std::byte>, Error> loadState() noexcept override
            {
                try
                {
                    SQLite::Statement query(*db_, "SELECT state FROM raft_state WHERE id = 1");

                    if (query.executeStep())  // true if a row was found
                    {
                        // Get the blob data from the first column (index 0).
                        SQLite::Column blobColumn = query.getColumn(0);

                        // Get a pointer to the data and its size.
                        const void* data = blobColumn.getBlob();
                        const int size = blobColumn.getBytes();

                        // Create a std::vector<std::byte> from the raw data.
                        const auto* begin = static_cast<const std::byte*>(data);
                        return std::vector<std::byte>(begin, begin + size);
                    }
                }
                catch (const std::exception& e)
                {
                    return tl::make_unexpected(errors::PersistenceFailed {.message = e.what()});
                }

                return tl::make_unexpected(errors::NoPersistedState {});
            }

          private:
            std::string path_;
            std::optional<SQLite::Database> db_;
        };
    }  // namespace

    tl::expected<std::shared_ptr<Persister>, Error> createSQLitePersister(const std::string& path)
    {
        auto sqlitePersister = std::make_shared<SQLitePersister>(path);
        auto initResult = sqlitePersister->init();
        if (!initResult.has_value())
        {
            return tl::make_unexpected(initResult.error());
        }
        return sqlitePersister;
    }
}  // namespace raft::fs
