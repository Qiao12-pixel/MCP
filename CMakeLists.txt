cmake_minimum_required(VERSION 3.24)
project(MCP_Server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_ARCHITECTURES arm64)

# 加载 Conan
include(${CMAKE_BINARY_DIR}/build/Debug/generators/conan_toolchain.cmake)

find_package(Threads REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)
find_package(CURL REQUIRED)
find_package(GTest REQUIRED)
find_package(httplib REQUIRED)

enable_testing()

add_library(mcp_config
        src/config/config.cpp
        src/config/config.h
)

target_include_directories(mcp_config PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(mcp_config PUBLIC
        nlohmann_json::nlohmann_json
)

add_library(mcp_logger
        src/logger/logger.cpp
        src/logger/logger.h
)

target_include_directories(mcp_logger PUBLIC
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(mcp_logger PUBLIC
        spdlog::spdlog
)

add_library(mcp_jsonrpc
        src/json_rpc/jsonrpc_types.h
        src/json_rpc/jsonrpc_dispatcher.cpp
        src/json_rpc/jsonrpc_dispatcher.h
        src/json_rpc/stdio_jsonrpc_server.cpp
        src/json_rpc/stdio_jsonrpc_server.h
        src/json_rpc/jsonrpc_serialization.cpp
        src/json_rpc/jsonrpc_serialization.h
        mcp/mcp_types.cpp
        mcp/mcp_types.h
        mcp/mcp_server.cpp
        mcp/mcp_server.h
        src/json_rpc/http_jsonrpc.cpp
        src/json_rpc/http_jsonrpc.h
)

target_include_directories(mcp_jsonrpc PUBLIC
        ${CMAKE_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(mcp_jsonrpc PUBLIC
        nlohmann_json::nlohmann_json
        httplib::httplib
        mcp_config
        mcp_logger
)

add_executable(MCP_Server
        src/mcp_server_main.cpp
)

target_link_libraries(MCP_Server PRIVATE
        mcp_jsonrpc
        mcp_config
        CURL::libcurl
        httplib::httplib
)

add_executable(config_tests
        tests/config_test.cpp
)

target_compile_definitions(config_tests PRIVATE
        MCP_PROJECT_ROOT="${CMAKE_SOURCE_DIR}"
)

target_link_libraries(config_tests PRIVATE
        mcp_config
        GTest::gtest_main
)

add_executable(logger_tests
        tests/logger_test.cpp
)

target_link_libraries(logger_tests PRIVATE
        mcp_logger
        GTest::gtest_main
)

add_executable(jsonrpc_tests
        tests/jsonrpc_test.cpp
)

target_link_libraries(jsonrpc_tests PRIVATE
        mcp_jsonrpc
        GTest::gtest_main
)

add_executable(mcp_types_tests
        tests/mcp_types_test.cpp
)

target_link_libraries(mcp_types_tests PRIVATE
        mcp_jsonrpc
        GTest::gtest_main
)

add_executable(mcp_server_tests
        tests/mcp_server_test.cpp
)

target_link_libraries(mcp_server_tests PRIVATE
        mcp_jsonrpc
        GTest::gtest_main
)

add_executable(http_jsonrpc_tests
        tests/http_jsonrpc_test.cpp
)

target_link_libraries(http_jsonrpc_tests PRIVATE
        mcp_jsonrpc
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(config_tests)
gtest_discover_tests(logger_tests)
gtest_discover_tests(jsonrpc_tests)
gtest_discover_tests(mcp_types_tests)
gtest_discover_tests(mcp_server_tests)
gtest_discover_tests(http_jsonrpc_tests)
