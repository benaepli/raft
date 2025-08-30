add_subdirectory(proto)

add_executable(raft_cli
        src/main.cpp
)

target_include_directories(raft_cli
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

target_link_libraries(raft_cli PRIVATE raft cli_protos)