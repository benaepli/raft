#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

class Environment : public ::testing::Environment
{
  public:
    void SetUp() override { spdlog::set_level(spdlog::level::debug); }
};

int main(int argc, char** argv)
{
    ::testing::AddGlobalTestEnvironment(new Environment());
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

TEST(RaftTest, Empty)
{
    EXPECT_TRUE(true);
}