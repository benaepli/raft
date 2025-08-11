add_executable(raft-cli
        src/main.cpp
)

target_include_directories(raft-cli
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

target_link_libraries(raft-cli PRIVATE raft)