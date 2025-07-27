find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

set(PROTO_FILES spanner_protos/spanner.proto)

add_library(spanner_protos ${PROTO_FILES})
target_link_libraries(spanner_protos
        PUBLIC
        gRPC::grpc++ gRPC::grpc++_alts
        protobuf::libprotobuf
)
target_include_directories(spanner_protos PUBLIC ${CMAKE_CURRENT_BINARY_DIR})

get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)
protobuf_generate(TARGET spanner_protos LANGUAGE cpp)
protobuf_generate(TARGET spanner_protos LANGUAGE grpc GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}")