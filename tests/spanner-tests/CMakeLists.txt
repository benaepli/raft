find_package(doctest CONFIG REQUIRED)

add_executable(spanner_tests src/basic.cpp)
target_link_libraries(spanner_tests PRIVATE spanner raft doctest::doctest)