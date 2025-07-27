find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

set(PROTO_FILES raft_protos/raft.proto)

add_library(raft_protos ${PROTO_FILES})
target_link_libraries(raft_protos
        PUBLIC
        gRPC::grpc++ gRPC::grpc++_alts
        protobuf::libprotobuf
)
target_include_directories(raft_protos PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET raft_protos LANGUAGE cpp)
protobuf_generate(TARGET raft_protos LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")