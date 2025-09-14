#include <cstring>
#include <filesystem>
#include <vector>

#include "raft/fs/sqlite.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "raft/errors.hpp"
#include "raft/persister.hpp"

// PersistedLog interface tests
class SQLitePersistedLogTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        testDir_ = std::filesystem::temp_directory_path() / "raft_sqlite_log_test";
        std::filesystem::create_directories(testDir_);
        dbPath_ = testDir_ / "test_log.db";

        auto result = raft::fs::createSQLitePersister(dbPath_.string());
        ASSERT_TRUE(result.has_value());
        persister_ = result.value();
    }

    void TearDown() override
    {
        persister_.reset();
        if (std::filesystem::exists(testDir_))
        {
            std::filesystem::remove_all(testDir_);
        }
    }

    std::filesystem::path testDir_;
    std::filesystem::path dbPath_;
    std::shared_ptr<raft::Persister> persister_;
};

TEST_F(SQLitePersistedLogTest, InitialState)
{
    // Test initial state after creation
    auto baseIndex = persister_->getBaseIndex();
    ASSERT_TRUE(baseIndex.has_value());
    EXPECT_EQ(*baseIndex, 1);

    // No entries should exist initially
    auto entry = persister_->getEntry(1);
    EXPECT_FALSE(entry.has_value());

    // No last term initially
    auto lastTerm = persister_->getLastTerm();
    EXPECT_FALSE(lastTerm.has_value());
}

TEST_F(SQLitePersistedLogTest, ApplyTransactionUpdateTermOnly)
{
    raft::PersistedTransaction transaction;
    transaction.setTerm(42);
    transaction.startIndex = 1;  // Required field

    auto result = persister_->apply(transaction);
    ASSERT_TRUE(result.has_value()) << "Failed to apply term update transaction";

    // No log entries should be added
    auto entry = persister_->getEntry(1);
    EXPECT_FALSE(entry.has_value());

    auto lastTerm = persister_->getLastTerm();
    EXPECT_FALSE(lastTerm.has_value());
}

TEST_F(SQLitePersistedLogTest, ApplyTransactionUpdateVotedForOnly)
{
    raft::PersistedTransaction transaction;
    transaction.setVotedFor("server-123");
    transaction.startIndex = 1;  // Required field

    auto result = persister_->apply(transaction);
    ASSERT_TRUE(result.has_value()) << "Failed to apply votedFor update transaction";

    // No log entries should be added
    auto entry = persister_->getEntry(1);
    EXPECT_FALSE(entry.has_value());
}

TEST_F(SQLitePersistedLogTest, ApplyTransactionUpdateLogOnly)
{
    // Create test entries
    std::vector<raft::data::LogEntry> entries;
    entries.push_back(
        {.term = 1, .entry = std::vector<std::byte> {std::byte {0x01}, std::byte {0x02}}});
    entries.push_back(
        {.term = 1, .entry = std::vector<std::byte> {std::byte {0x03}, std::byte {0x04}}});

    raft::PersistedTransaction transaction;
    transaction.store(1, entries);

    auto result = persister_->apply(transaction);
    ASSERT_TRUE(result.has_value()) << "Failed to apply log update transaction";

    // Verify entries were stored
    auto entry1 = persister_->getEntry(1);
    ASSERT_TRUE(entry1.has_value());
    EXPECT_EQ(entry1->term, 1);
    auto data1 = std::get<std::vector<std::byte>>(entry1->entry);
    EXPECT_EQ(data1, (std::vector<std::byte> {std::byte {0x01}, std::byte {0x02}}));

    auto entry2 = persister_->getEntry(2);
    ASSERT_TRUE(entry2.has_value());
    EXPECT_EQ(entry2->term, 1);
    auto data2 = std::get<std::vector<std::byte>>(entry2->entry);
    EXPECT_EQ(data2, (std::vector<std::byte> {std::byte {0x03}, std::byte {0x04}}));

    // Verify last term
    auto lastTerm = persister_->getLastTerm();
    ASSERT_TRUE(lastTerm.has_value());
    EXPECT_EQ(*lastTerm, 1);
}

TEST_F(SQLitePersistedLogTest, ApplyTransactionCombinedUpdates)
{
    // Create test entries
    std::vector<raft::data::LogEntry> entries;
    entries.push_back({.term = 5, .entry = std::vector<std::byte> {std::byte {0xAA}}});

    raft::PersistedTransaction transaction;
    transaction.setTerm(5);
    transaction.setVotedFor("server-456");
    transaction.store(10, entries);

    auto result = persister_->apply(transaction);
    ASSERT_TRUE(result.has_value()) << "Failed to apply combined update transaction";

    // Verify entry was stored at correct index
    auto entry = persister_->getEntry(10);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->term, 5);
    auto data = std::get<std::vector<std::byte>>(entry->entry);
    EXPECT_EQ(data, (std::vector<std::byte> {std::byte {0xAA}}));

    // Verify last term
    auto lastTerm = persister_->getLastTerm();
    ASSERT_TRUE(lastTerm.has_value());
    EXPECT_EQ(*lastTerm, 5);
}

TEST_F(SQLitePersistedLogTest, ApplyTransactionWithNoOpEntries)
{
    // Create entries with NoOp
    std::vector<raft::data::LogEntry> entries;
    entries.push_back({.term = 3, .entry = raft::data::NoOp {}});
    entries.push_back({.term = 3, .entry = std::vector<std::byte> {std::byte {0xFF}}});

    raft::PersistedTransaction transaction;
    transaction.store(1, entries);

    auto result = persister_->apply(transaction);
    ASSERT_TRUE(result.has_value()) << "Failed to apply transaction with NoOp entries";

    // Verify NoOp entry
    auto entry1 = persister_->getEntry(1);
    ASSERT_TRUE(entry1.has_value());
    EXPECT_EQ(entry1->term, 3);
    EXPECT_TRUE(std::holds_alternative<raft::data::NoOp>(entry1->entry));

    // Verify regular entry
    auto entry2 = persister_->getEntry(2);
    ASSERT_TRUE(entry2.has_value());
    EXPECT_EQ(entry2->term, 3);
    auto data = std::get<std::vector<std::byte>>(entry2->entry);
    EXPECT_EQ(data, (std::vector<std::byte> {std::byte {0xFF}}));

    // Verify last term
    auto lastTerm = persister_->getLastTerm();
    ASSERT_TRUE(lastTerm.has_value());
    EXPECT_EQ(*lastTerm, 3);
}

TEST_F(SQLitePersistedLogTest, ApplyTransactionOverwriteEntries)
{
    // First, store some initial entries
    std::vector<raft::data::LogEntry> initialEntries;
    initialEntries.push_back({.term = 1, .entry = std::vector<std::byte> {std::byte {0x01}}});
    initialEntries.push_back({.term = 1, .entry = std::vector<std::byte> {std::byte {0x02}}});
    initialEntries.push_back({.term = 1, .entry = std::vector<std::byte> {std::byte {0x03}}});

    raft::PersistedTransaction transaction1;
    transaction1.store(1, initialEntries);
    auto result1 = persister_->apply(transaction1);
    ASSERT_TRUE(result1.has_value());

    // Verify initial entries exist
    EXPECT_TRUE(persister_->getEntry(1).has_value());
    EXPECT_TRUE(persister_->getEntry(2).has_value());
    EXPECT_TRUE(persister_->getEntry(3).has_value());

    // Now overwrite from index 2 onward
    std::vector<raft::data::LogEntry> newEntries;
    newEntries.push_back({.term = 2, .entry = std::vector<std::byte> {std::byte {0x04}}});

    raft::PersistedTransaction transaction2;
    transaction2.store(2, newEntries);
    auto result2 = persister_->apply(transaction2);
    ASSERT_TRUE(result2.has_value());

    // Verify entry 1 still exists
    auto entry1 = persister_->getEntry(1);
    ASSERT_TRUE(entry1.has_value());
    auto data1 = std::get<std::vector<std::byte>>(entry1->entry);
    EXPECT_EQ(data1, (std::vector<std::byte> {std::byte {0x01}}));

    // Verify entry 2 was overwritten
    auto entry2 = persister_->getEntry(2);
    ASSERT_TRUE(entry2.has_value());
    EXPECT_EQ(entry2->term, 2);
    auto data2 = std::get<std::vector<std::byte>>(entry2->entry);
    EXPECT_EQ(data2, (std::vector<std::byte> {std::byte {0x04}}));

    // Verify entry 3 was deleted
    auto entry3 = persister_->getEntry(3);
    EXPECT_FALSE(entry3.has_value());

    // Verify last term updated
    auto lastTerm = persister_->getLastTerm();
    ASSERT_TRUE(lastTerm.has_value());
    EXPECT_EQ(*lastTerm, 2);
}

TEST_F(SQLitePersistedLogTest, ApplyTransactionEmptyEntries)
{
    // Store empty entries list (should just delete from startIndex)
    std::vector<raft::data::LogEntry> initialEntries;
    initialEntries.push_back({.term = 1, .entry = std::vector<std::byte> {std::byte {0x01}}});
    initialEntries.push_back({.term = 1, .entry = std::vector<std::byte> {std::byte {0x02}}});

    raft::PersistedTransaction transaction1;
    transaction1.store(1, initialEntries);
    auto result1 = persister_->apply(transaction1);
    ASSERT_TRUE(result1.has_value());

    // Apply transaction with empty entries from index 2
    std::vector<raft::data::LogEntry> emptyEntries;
    raft::PersistedTransaction transaction2;
    transaction2.store(2, emptyEntries);
    auto result2 = persister_->apply(transaction2);
    ASSERT_TRUE(result2.has_value());

    // Verify entry 1 still exists
    EXPECT_TRUE(persister_->getEntry(1).has_value());

    // Verify entry 2 was deleted
    EXPECT_FALSE(persister_->getEntry(2).has_value());

    // Verify last term is from remaining entry
    auto lastTerm = persister_->getLastTerm();
    ASSERT_TRUE(lastTerm.has_value());
    EXPECT_EQ(*lastTerm, 1);
}

TEST_F(SQLitePersistedLogTest, ApplyTransactionUpdateVotedForNull)
{
    // Test setting votedFor to null
    raft::PersistedTransaction transaction;
    transaction.setVotedFor(std::nullopt);
    transaction.startIndex = 1;

    auto result = persister_->apply(transaction);
    ASSERT_TRUE(result.has_value()) << "Failed to apply null votedFor transaction";
}

TEST_F(SQLitePersistedLogTest, PersistenceAcrossInstances)
{
    // Store data in first instance
    std::vector<raft::data::LogEntry> entries;
    entries.push_back({.term = 7, .entry = std::vector<std::byte> {std::byte {0xBB}}});

    raft::PersistedTransaction transaction;
    transaction.setTerm(7);
    transaction.setVotedFor("persistent-server");
    transaction.store(5, entries);

    auto result = persister_->apply(transaction);
    ASSERT_TRUE(result.has_value());

    // Destroy first instance
    persister_.reset();

    // Create new instance
    auto newResult = raft::fs::createSQLitePersister(dbPath_.string());
    ASSERT_TRUE(newResult.has_value());
    auto newPersister = newResult.value();

    // Verify data persisted
    auto entry = newPersister->getEntry(5);
    ASSERT_TRUE(entry.has_value());
    EXPECT_EQ(entry->term, 7);

    auto lastTerm = newPersister->getLastTerm();
    ASSERT_TRUE(lastTerm.has_value());
    EXPECT_EQ(*lastTerm, 7);

    auto baseIndex = newPersister->getBaseIndex();
    ASSERT_TRUE(baseIndex.has_value());
    EXPECT_EQ(*baseIndex, 1);
}

TEST_F(SQLitePersistedLogTest, CreatePersisterInvalidPath)
{
    auto result = raft::fs::createSQLitePersister("/invalid/path/that/does/not/exist/test.db");
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<raft::errors::FailedToStart>(result.error()));
}

TEST_F(SQLitePersistedLogTest, DatabaseFileCreation)
{
    std::filesystem::path newDbPath = testDir_ / "new_test.db";

    // Verify file doesn't exist initially
    EXPECT_FALSE(std::filesystem::exists(newDbPath));

    // Create persister
    auto result = raft::fs::createSQLitePersister(newDbPath.string());
    ASSERT_TRUE(result.has_value());

    // Verify database file was created
    EXPECT_TRUE(std::filesystem::exists(newDbPath));

    // Verify we can perform operations
    auto persister = result.value();

    raft::PersistedTransaction transaction;
    transaction.setTerm(1);
    transaction.startIndex = 1;

    auto applyResult = persister->apply(transaction);
    EXPECT_TRUE(applyResult.has_value());
}