find_package(asio CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)

add_subdirectory(proto)

add_library(raft
    ../include/raft/client.hpp
    ../include/raft/errors.hpp
    ../include/raft/fmt/errors.hpp
    ../include/raft/network.hpp
    ../include/raft/persister.hpp
    ../include/raft/server.hpp
    src/client.cpp
    src/common/mpsc_queue.hpp
    src/impl/persistence.hpp
    src/network.cpp
    src/raft.cpp
    src/server.cpp
    src/utils/grpc_data.hpp
    src/utils/grpc_errors.hpp
    src/utils/serialize.cpp
    src/utils/serialize.hpp
)

target_link_libraries(raft PUBLIC tl::expected)
target_link_libraries(raft PRIVATE raft_protos spdlog::spdlog fmt::fmt asio::asio)

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