find_package(gRPC REQUIRED)
find_package(Protobuf REQUIRED)

set(PROTO_FILES cli_protos/kv_store.proto)

add_library(cli_protos ${PROTO_FILES})
target_link_libraries(cli_protos
        PUBLIC
        gRPC::grpc++ gRPC::grpc++_alts
        protobuf::libprotobuf
)
target_include_directories(cli_protos PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
target_include_directories(cli_protos PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET cli_protos LANGUAGE cpp)
protobuf_generate(TARGET cli_protos LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")