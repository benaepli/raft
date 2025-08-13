find_package(GTest CONFIG REQUIRED)

add_executable(raft_tests
        src/basic.cpp
        src/network.cpp
        src/impl/persistence.cpp
        src/inmemory/manager.cpp
        src/election.cpp
        src/mocks/persister.hpp
)
target_link_libraries(raft_tests PRIVATE GTest::gtest GTest::gtest_main GTest::gmock GTest::gmock_main raft)
target_include_directories(raft_tests PRIVATE ${CMAKE_SOURCE_DIR}/raft/src src)