find_package(GTest REQUIRED)
find_package(spdlog REQUIRED)

add_executable(raft_tests
        src/main.cpp
        src/network.cpp
        src/impl/persistence.cpp
        src/inmemory/manager.cpp
        src/election.cpp
        src/enhanced/server.cpp
        src/mocks/persister.hpp
        src/server_tester.hpp
        src/server.cpp
        src/fs/sqlite.cpp
)
target_link_libraries(raft_tests PRIVATE GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main raft spdlog::spdlog)
target_include_directories(raft_tests PRIVATE ${CMAKE_SOURCE_DIR}/raft/src src)