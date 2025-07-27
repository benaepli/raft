find_package(spdlog CONFIG REQUIRED)
find_package(tl-expected CONFIG REQUIRED)

add_subdirectory(proto)

add_library(spanner
        src/spanner.cpp
)

target_link_libraries(spanner PUBLIC tl::expected)
target_link_libraries(spanner PRIVATE spanner_protos spdlog::spdlog)

# Set up include directories
target_include_directories(spanner
        PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
        PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(spanner PROPERTIES EXPORT_NAME Spanner)