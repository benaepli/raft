#include "raft/fs/sqlite.hpp"

#include <spdlog/spdlog.h>

#include "SQLiteCpp/Database.h"
#include "SQLiteCpp/Transaction.h"
#include "raft/data.hpp"

namespace raft::fs
{
    namespace
    {
        class SQLitePersister final : public Persister
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
                    db_->exec(
                        "CREATE TABLE IF NOT EXISTS raft_metadata ("
                        "id INTEGER PRIMARY KEY CHECK (id = 1),"
                        "current_term INTEGER NOT NULL,"
                        "voted_for TEXT,"
                        "base_index INTEGER NOT NULL DEFAULT 1);");

                    db_->exec(
                        "CREATE TABLE IF NOT EXISTS log_entries ("
                        "log_index INTEGER PRIMARY KEY,"
                        "term INTEGER NOT NULL,"
                        "entry_type INTEGER NOT NULL DEFAULT 0,"
                        "entry BLOB);");

                    // Initialize metadata if it doesn't exist
                    // This will only insert if no row with id=1 exists
                    db_->exec(
                        "INSERT OR IGNORE INTO raft_metadata (id, current_term, base_index) "
                        "VALUES (1, 0, 1);");
                }
                catch (const SQLite::Exception& e)
                {
                    spdlog::error("[{}] {}", path_, e.what());
                    return tl::make_unexpected(errors::FailedToStart {});
                }
                return {};
            }

            [[nodiscard]] std::optional<uint64_t> getBaseIndex() override
            {
                try
                {
                    SQLite::Statement query(*db_,
                                            "SELECT base_index FROM raft_metadata WHERE id = 1");
                    if (query.executeStep())
                    {
                        return static_cast<uint64_t>(query.getColumn(0).getInt64());
                    }
                }
                catch (const std::exception& e)
                {
                    spdlog::error("[{}] failed to get base index: {}", path_, e.what());
                }
                return std::nullopt;
            }

            std::optional<data::LogEntry> getEntry(uint64_t index) const override
            {
                try
                {
                    SQLite::Statement query(
                        *db_,
                        "SELECT term, entry_type, entry FROM log_entries WHERE log_index = ?");
                    query.bind(1, static_cast<int64_t>(index));

                    if (query.executeStep())
                    {
                        uint64_t term = static_cast<uint64_t>(query.getColumn(0).getInt64());
                        int entryType = query.getColumn(1).getInt();

                        if (entryType == 1)  // NoOp entry
                        {
                            return data::LogEntry {.term = term, .entry = data::NoOp {}};
                        }
                        SQLite::Column blobColumn = query.getColumn(2);
                        const void* data = blobColumn.getBlob();
                        const int size = blobColumn.getBytes();

                        const auto* begin = static_cast<const std::byte*>(data);
                        std::vector<std::byte> entryData(begin, begin + size);
                        return data::LogEntry {.term = term, .entry = std::move(entryData)};
                    }
                }
                catch (const std::exception& e)
                {
                    spdlog::error(
                        "[{}] failed to get entry at index {}: {}", path_, index, e.what());
                }
                return std::nullopt;
            }

            [[nodiscard]] std::optional<uint64_t> getLastTerm() const override
            {
                try
                {
                    SQLite::Statement query(
                        *db_, "SELECT term FROM log_entries ORDER BY log_index DESC LIMIT 1");
                    if (query.executeStep())
                    {
                        return static_cast<uint64_t>(query.getColumn(0).getInt64());
                    }
                }
                catch (const std::exception& e)
                {
                    spdlog::error("[{}] failed to get last term: {}", path_, e.what());
                }
                return std::nullopt;
            }

            tl::expected<void, Error> apply(PersistedTransaction const& transaction) override
            {
                try
                {
                    SQLite::Transaction dbTransaction(*db_);

                    if (transaction.updateTerm && transaction.updateVotedFor)
                    {
                        SQLite::Statement updateMetadata(*db_,
                                                         "UPDATE raft_metadata SET current_term = "
                                                         "?, voted_for = ? WHERE id = 1");
                        updateMetadata.bind(1, static_cast<int64_t>(transaction.term));
                        if (transaction.votedFor.has_value())
                        {
                            updateMetadata.bind(2, *transaction.votedFor);
                        }
                        else
                        {
                            updateMetadata.bind(2);  // NULL
                        }
                        updateMetadata.exec();
                    }
                    else if (transaction.updateTerm)
                    {
                        SQLite::Statement updateTerm(
                            *db_, "UPDATE raft_metadata SET current_term = ? WHERE id = 1");
                        updateTerm.bind(1, static_cast<int64_t>(transaction.term));
                        updateTerm.exec();
                    }
                    else if (transaction.updateVotedFor)
                    {
                        SQLite::Statement updateVotedFor(
                            *db_, "UPDATE raft_metadata SET voted_for = ? WHERE id = 1");
                        if (transaction.votedFor.has_value())
                        {
                            updateVotedFor.bind(1, *transaction.votedFor);
                        }
                        else
                        {
                            updateVotedFor.bind(1);  // NULL
                        }
                        updateVotedFor.exec();
                    }

                    if (transaction.updateLog)
                    {
                        // Remove entries from startIndex onward
                        SQLite::Statement deleteEntries(
                            *db_, "DELETE FROM log_entries WHERE log_index >= ?");
                        deleteEntries.bind(1, static_cast<int64_t>(transaction.startIndex));
                        deleteEntries.exec();

                        // Insert new entries starting from startIndex
                        uint64_t currentIndex = transaction.startIndex;
                        for (const auto& entry : transaction.entries)
                        {
                            SQLite::Statement insertEntry(
                                *db_,
                                "INSERT INTO log_entries (log_index, "
                                "term, entry_type, entry) VALUES (?, ?, ?, ?)");
                            insertEntry.bind(1, static_cast<int64_t>(currentIndex));
                            insertEntry.bind(2, static_cast<int64_t>(entry.term));

                            if (std::holds_alternative<data::NoOp>(entry.entry))
                            {
                                insertEntry.bind(3, 1);  // entry_type = 1 for NoOp
                                insertEntry.bind(4);  // NULL for entry data
                            }
                            else
                            {
                                const auto& entryData =
                                    std::get<std::vector<std::byte>>(entry.entry);
                                insertEntry.bind(3, 0);  // entry_type = 0 for regular entry
                                insertEntry.bind(
                                    4, entryData.data(), static_cast<int>(entryData.size()));
                            }
                            insertEntry.exec();
                            currentIndex++;
                        }
                    }

                    dbTransaction.commit();
                }
                catch (const std::exception& e)
                {
                    return tl::make_unexpected(errors::PersistenceFailed {.message = e.what()});
                }
                return {};
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
