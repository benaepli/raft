find_package(asio CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)

add_subdirectory(proto)

add_library(raft
        ../include/raft/client.hpp
        ../include/raft/raft.hpp
        src/client.cpp
        src/raft.cpp
        src/utils/grpc_data.hpp
        ../include/raft/server.hpp
)

target_link_libraries(raft PUBLIC tl::expected)
target_link_libraries(raft PRIVATE raft_protos spdlog::spdlog asio::asio)

# Set up include directories
target_include_directories(raft
        PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(raft PROPERTIES EXPORT_NAME Raft)