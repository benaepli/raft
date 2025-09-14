find_package(lyra REQUIRED)
find_package(tomlplusplus REQUIRED)
find_package(fmt REQUIRED)

add_subdirectory(proto)

add_executable(raft_cli
        src/main.cpp
        src/config/config.hpp
        src/config/config.cpp
        src/errors.hpp
        src/serve.hpp
        src/store/server.hpp
        src/store/data.hpp
        src/utils/grpc_data.hpp
        src/utils/grpc_errors.hpp
        src/store/data.cpp
        src/store/server.cpp
)

target_include_directories(raft_cli
        PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
)

target_link_libraries(raft_cli PRIVATE raft cli_protos bfg::lyra tomlplusplus::tomlplusplus fmt::fmt)