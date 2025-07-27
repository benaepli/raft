add_executable(spanner-cli
        src/main.cpp
)

target_include_directories(spanner-cli
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

target_link_libraries(spanner-cli PRIVATE spanner raft)