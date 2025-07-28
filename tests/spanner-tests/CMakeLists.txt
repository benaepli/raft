find_package(GTest CONFIG REQUIRED)

add_executable(spanner_tests src/basic.cpp)
target_link_libraries(spanner_tests PRIVATE spanner raft GTest::gtest GTest::gtest_main)