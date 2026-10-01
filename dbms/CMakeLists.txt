cmake_minimum_required(VERSION 3.25)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

project(coursework_dbms LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(TEST_RUNTIME_DIR "${CMAKE_BINARY_DIR}/tests/bin")

add_library(dbms_core INTERFACE)

target_include_directories(dbms_core INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.zip
)

FetchContent_Declare(
    grpc
    GIT_REPOSITORY https://github.com/grpc/grpc.git
    GIT_TAG v1.62.2
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(ABSL_PROPAGATE_CXX_STD ON CACHE BOOL "" FORCE)
set(absl_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(gRPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(gRPC_INSTALL OFF CACHE BOOL "" FORCE)
set(protobuf_INSTALL OFF CACHE BOOL "" FORCE)
set(utf8_range_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest nlohmann_json grpc)

target_link_libraries(dbms_core INTERFACE
    nlohmann_json::nlohmann_json
)

set(PROTO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/proto")
set(PROTO_FILE "${PROTO_DIR}/sql_service.proto")
set(GENERATED_PROTO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GENERATED_PROTO_SRC "${GENERATED_PROTO_DIR}/sql_service.pb.cc")
set(GENERATED_PROTO_HDR "${GENERATED_PROTO_DIR}/sql_service.pb.h")
set(GENERATED_GRPC_SRC "${GENERATED_PROTO_DIR}/sql_service.grpc.pb.cc")
set(GENERATED_GRPC_HDR "${GENERATED_PROTO_DIR}/sql_service.grpc.pb.h")

file(MAKE_DIRECTORY "${GENERATED_PROTO_DIR}")

add_custom_command(
    OUTPUT
        "${GENERATED_PROTO_SRC}"
        "${GENERATED_PROTO_HDR}"
        "${GENERATED_GRPC_SRC}"
        "${GENERATED_GRPC_HDR}"
    COMMAND $<TARGET_FILE:protobuf::protoc>
    ARGS
        --grpc_out "${GENERATED_PROTO_DIR}"
        --cpp_out "${GENERATED_PROTO_DIR}"
        -I "${PROTO_DIR}"
        --plugin=protoc-gen-grpc=$<TARGET_FILE:grpc_cpp_plugin>
        "${PROTO_FILE}"
    DEPENDS
        "${PROTO_FILE}"
        protobuf::protoc
        grpc_cpp_plugin
    VERBATIM
)

add_library(dbms_grpc_proto
    "${GENERATED_PROTO_SRC}"
    "${GENERATED_GRPC_SRC}"
)

target_include_directories(dbms_grpc_proto PUBLIC
    "${GENERATED_PROTO_DIR}"
)

target_link_libraries(dbms_grpc_proto PUBLIC
    protobuf::libprotobuf
    grpc++
)

add_executable(dbms
    src/main.cpp
    src/sql/cli.cpp
    src/sql/lexer.cpp
    src/sql/parser.cpp
    src/sql/executor.cpp
    src/sql/sql_api.cpp
)

target_link_libraries(dbms PRIVATE dbms_core)

set_target_properties(dbms PROPERTIES
    OUTPUT_NAME "prog"
)

add_executable(dbms_grpc_server
    src/grpc/grpc_server_main.cpp
    src/grpc/sql_service_impl.cpp
    src/grpc/access_logger.cpp
    src/grpc/telemetry.cpp
    src/sql/lexer.cpp
    src/sql/parser.cpp
    src/sql/executor.cpp
    src/sql/sql_api.cpp
)

target_include_directories(dbms_grpc_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dbms_grpc_server PRIVATE
    dbms_core
    dbms_grpc_proto
)

add_executable(dbms_grpc_client
    src/grpc/grpc_client_main.cpp
)

target_link_libraries(dbms_grpc_client PRIVATE
    dbms_grpc_proto
)

add_executable(dbms_tests
    tests/b_tree_tests.cpp
)

target_include_directories(dbms_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dbms_tests PRIVATE
    dbms_core
    GTest::gtest
)

set_target_properties(dbms_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TEST_RUNTIME_DIR}"
)

add_executable(dbms_sql_tests
    tests/sql/lexer_tests.cpp
    tests/sql/parser_tests.cpp
    tests/sql/executor_tests.cpp
    src/sql/lexer.cpp
    src/sql/parser.cpp
    src/sql/executor.cpp
)

target_include_directories(dbms_sql_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dbms_sql_tests PRIVATE
    dbms_core
    GTest::gtest_main
)

set_target_properties(dbms_sql_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TEST_RUNTIME_DIR}"
)

add_executable(dbms_cli_tests
    tests/sql/cli_script_tests.cpp
    src/sql/cli.cpp
    src/sql/sql_api.cpp
    src/sql/lexer.cpp
    src/sql/parser.cpp
    src/sql/executor.cpp
)

target_include_directories(dbms_cli_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dbms_cli_tests PRIVATE
    dbms_core
    GTest::gtest_main
)

set_target_properties(dbms_cli_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TEST_RUNTIME_DIR}"
)

add_executable(dbms_all_tests
    tests/spec/all_tests.cpp
    src/sql/cli.cpp
    src/sql/sql_api.cpp
    src/sql/lexer.cpp
    src/sql/parser.cpp
    src/sql/executor.cpp
)

target_include_directories(dbms_all_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(dbms_all_tests PRIVATE
    dbms_core
    GTest::gtest_main
)

set_target_properties(dbms_all_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${TEST_RUNTIME_DIR}"
)

enable_testing()
add_test(NAME b_tree_tests COMMAND $<TARGET_FILE:dbms_tests>)
add_test(NAME sql_lexer_tests COMMAND $<TARGET_FILE:dbms_sql_tests>)
add_test(NAME sql_cli_tests COMMAND $<TARGET_FILE:dbms_cli_tests>)
add_test(NAME all_tests COMMAND $<TARGET_FILE:dbms_all_tests>)
add_test(
    NAME grpc_smoke_test
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/grpc_smoke_test.sh
        $<TARGET_FILE:dbms_grpc_server>
        $<TARGET_FILE:dbms_grpc_client>
)

add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --test-dir ${CMAKE_BINARY_DIR} --output-on-failure
    DEPENDS dbms_tests dbms_sql_tests dbms_cli_tests dbms_all_tests
            dbms_grpc_server dbms_grpc_client
    USES_TERMINAL
)
