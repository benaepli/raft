#include <cstring>
#include <filesystem>
#include <vector>

#include "raft/fs/sqlite.hpp"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "raft/errors.hpp"
#include "raft/persister.hpp"

class SQLitePersisterTest : public ::testing::Test
{
  protected:
    void SetUp() override
    {
        testDir_ = std::filesystem::temp_directory_path() / "raft_sqlite_test";
        std::filesystem::create_directories(testDir_);
        dbPath_ = testDir_ / "test.db";
    }

    void TearDown() override
    {
        if (std::filesystem::exists(testDir_))
        {
            std::filesystem::remove_all(testDir_);
        }
    }

    std::filesystem::path testDir_;
    std::filesystem::path dbPath_;
};

TEST_F(SQLitePersisterTest, CreatePersisterSuccess)
{
    auto result = raft::fs::createSQLitePersister(dbPath_.string());
    ASSERT_TRUE(result.has_value());
    EXPECT_NE(result.value(), nullptr);
}

TEST_F(SQLitePersisterTest, CreatePersisterInvalidPath)
{
    auto result = raft::fs::createSQLitePersister("/invalid/path/that/does/not/exist/test.db");
    ASSERT_FALSE(result.has_value());
    EXPECT_TRUE(std::holds_alternative<raft::errors::FailedToStart>(result.error()));
}

TEST_F(SQLitePersisterTest, LoadStateNoPersistedState)
{
    auto result = raft::fs::createSQLitePersister(dbPath_.string());
    ASSERT_TRUE(result.has_value());

    auto persister = result.value();
    auto loadResult = persister->loadState();

    ASSERT_FALSE(loadResult.has_value());
    EXPECT_TRUE(std::holds_alternative<raft::errors::NoPersistedState>(loadResult.error()));
}

TEST_F(SQLitePersisterTest, SaveAndLoadState)
{
    auto result = raft::fs::createSQLitePersister(dbPath_.string());
    ASSERT_TRUE(result.has_value());

    auto persister = result.value();

    // Create test data
    std::vector<std::byte> testData = {std::byte {0x01},
                                       std::byte {0x02},
                                       std::byte {0x03},
                                       std::byte {0x04},
                                       std::byte {0x05},
                                       std::byte {0x06},
                                       std::byte {0x07},
                                       std::byte {0x08}};

    // Save the data
    auto saveResult = persister->saveState(testData);
    ASSERT_TRUE(saveResult.has_value()) << "Failed to save state";

    // Load the data
    auto loadResult = persister->loadState();
    ASSERT_TRUE(loadResult.has_value()) << "Failed to load state";

    // Verify the data matches
    EXPECT_EQ(loadResult.value(), testData);
}

TEST_F(SQLitePersisterTest, SaveAndLoadEmptyState)
{
    auto result = raft::fs::createSQLitePersister(dbPath_.string());
    ASSERT_TRUE(result.has_value());

    auto persister = result.value();

    // Create empty test data
    std::vector<std::byte> emptyData;

    // Save the empty data
    auto saveResult = persister->saveState(emptyData);
    ASSERT_TRUE(saveResult.has_value()) << "Failed to save empty state";

    // Load the data
    auto loadResult = persister->loadState();
    ASSERT_TRUE(loadResult.has_value()) << "Failed to load empty state";

    // Verify the data matches (should be empty)
    EXPECT_EQ(loadResult.value(), emptyData);
    EXPECT_TRUE(loadResult.value().empty());
}

TEST_F(SQLitePersisterTest, OverwriteState)
{
    auto result = raft::fs::createSQLitePersister(dbPath_.string());
    ASSERT_TRUE(result.has_value());

    auto persister = result.value();

    // Create first test data
    std::vector<std::byte> firstData = {
        std::byte {0x01}, std::byte {0x02}, std::byte {0x03}, std::byte {0x04}};

    // Save first data
    auto saveResult1 = persister->saveState(firstData);
    ASSERT_TRUE(saveResult1.has_value());

    // Create second test data
    std::vector<std::byte> secondData = {std::byte {0x05},
                                         std::byte {0x06},
                                         std::byte {0x07},
                                         std::byte {0x08},
                                         std::byte {0x09},
                                         std::byte {0x0A}};

    // Save second data (should overwrite)
    auto saveResult2 = persister->saveState(secondData);
    ASSERT_TRUE(saveResult2.has_value());

    // Load the data
    auto loadResult = persister->loadState();
    ASSERT_TRUE(loadResult.has_value());

    // Verify we get the second data, not the first
    EXPECT_EQ(loadResult.value(), secondData);
    EXPECT_NE(loadResult.value(), firstData);
}

TEST_F(SQLitePersisterTest, LargeStateData)
{
    auto result = raft::fs::createSQLitePersister(dbPath_.string());
    ASSERT_TRUE(result.has_value());

    auto persister = result.value();

    // Create large test data (1MB)
    const size_t largeSize = 1024 * 1024;
    std::vector<std::byte> largeData(largeSize);

    // Fill with pattern
    for (size_t i = 0; i < largeSize; ++i)
    {
        largeData[i] = std::byte(i % 256);
    }

    // Save the large data
    auto saveResult = persister->saveState(largeData);
    ASSERT_TRUE(saveResult.has_value()) << "Failed to save large state";

    // Load the data
    auto loadResult = persister->loadState();
    ASSERT_TRUE(loadResult.has_value()) << "Failed to load large state";

    // Verify the data matches
    EXPECT_EQ(loadResult.value(), largeData);
}

TEST_F(SQLitePersisterTest, PersistenceAcrossInstances)
{
    std::vector<std::byte> testData = {
        std::byte {0xAA}, std::byte {0xBB}, std::byte {0xCC}, std::byte {0xDD}};

    // Create first persister instance and save data
    {
        auto result = raft::fs::createSQLitePersister(dbPath_.string());
        ASSERT_TRUE(result.has_value());

        auto persister = result.value();
        auto saveResult = persister->saveState(testData);
        ASSERT_TRUE(saveResult.has_value());
    }

    // Create second persister instance and load data
    {
        auto result = raft::fs::createSQLitePersister(dbPath_.string());
        ASSERT_TRUE(result.has_value());

        auto persister = result.value();
        auto loadResult = persister->loadState();
        ASSERT_TRUE(loadResult.has_value());

        // Verify the data persisted across instances
        EXPECT_EQ(loadResult.value(), testData);
    }
}

TEST_F(SQLitePersisterTest, MultipleOperations)
{
    auto result = raft::fs::createSQLitePersister(dbPath_.string());
    ASSERT_TRUE(result.has_value());

    auto persister = result.value();

    // Test multiple save/load cycles
    for (int i = 0; i < 10; ++i)
    {
        std::vector<std::byte> testData;
        for (int j = 0; j <= i; ++j)
        {
            testData.push_back(std::byte(j));
        }

        auto saveResult = persister->saveState(testData);
        ASSERT_TRUE(saveResult.has_value()) << "Failed save on iteration " << i;

        auto loadResult = persister->loadState();
        ASSERT_TRUE(loadResult.has_value()) << "Failed load on iteration " << i;

        EXPECT_EQ(loadResult.value(), testData) << "Data mismatch on iteration " << i;
    }
}

TEST_F(SQLitePersisterTest, BinaryDataHandling)
{
    auto result = raft::fs::createSQLitePersister(dbPath_.string());
    ASSERT_TRUE(result.has_value());

    auto persister = result.value();

    // Create binary data with all possible byte values
    std::vector<std::byte> binaryData;
    for (int i = 0; i < 256; ++i)
    {
        binaryData.push_back(std::byte(i));
    }

    // Save binary data
    auto saveResult = persister->saveState(binaryData);
    ASSERT_TRUE(saveResult.has_value());

    // Load binary data
    auto loadResult = persister->loadState();
    ASSERT_TRUE(loadResult.has_value());

    // Verify all bytes are preserved correctly
    EXPECT_EQ(loadResult.value(), binaryData);
    EXPECT_EQ(loadResult.value().size(), 256);
}

TEST_F(SQLitePersisterTest, DatabaseFileCreation)
{
    // Verify file doesn't exist initially
    EXPECT_FALSE(std::filesystem::exists(dbPath_));

    // Create persister
    auto result = raft::fs::createSQLitePersister(dbPath_.string());
    ASSERT_TRUE(result.has_value());

    // Verify database file was created
    EXPECT_TRUE(std::filesystem::exists(dbPath_));

    // Verify we can perform operations
    auto persister = result.value();
    std::vector<std::byte> testData = {std::byte {0x42}};

    auto saveResult = persister->saveState(testData);
    EXPECT_TRUE(saveResult.has_value());
}