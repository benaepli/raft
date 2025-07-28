find_package(GTest CONFIG REQUIRED)

add_executable(raft_tests src/basic.cpp)
target_link_libraries(raft_tests PRIVATE raft GTest::gtest GTest::gtest_main)
