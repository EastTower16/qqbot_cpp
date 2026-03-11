cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND EXISTS "G:/vcpkg/scripts/buildsystems/vcpkg.cmake")
    set(CMAKE_TOOLCHAIN_FILE "G:/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "Vcpkg toolchain file")
endif()

if(WIN32 AND NOT DEFINED VCPKG_TARGET_TRIPLET)
    set(VCPKG_TARGET_TRIPLET "x64-windows" CACHE STRING "Vcpkg target triplet")
endif()

project(qqbot_cpp LANGUAGES CXX)

include(FetchContent)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00 WINVER=0x0A00)
endif()

find_package(OpenSSL CONFIG REQUIRED)
find_package(CURL CONFIG REQUIRED)
find_package(nlohmann_json CONFIG QUIET)

if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json not found via package manager, fetching from GitHub...")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

add_library(qqbot_sdk
    src/common/access_token.cpp
    src/common/bot_config.cpp
    src/common/sdk_error.cpp
    src/common/sdk_info.cpp
    src/common/url.cpp
    src/transport/http_transport.cpp
    src/openapi/openapi.cpp
    src/openapi/v1/resource.cpp
    src/openapi/v1/openapi_v1.cpp
    src/websocket/websocket_client.cpp
    src/sdk/bot_sdk.cpp
)

target_include_directories(qqbot_sdk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(qqbot_sdk PUBLIC
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(qqbot_sdk PUBLIC
        ws2_32
        crypt32
        bcrypt
    )
endif()

add_executable(qqbot_sdk_smoke
    src/examples/smoke_test.cpp
)

target_link_libraries(qqbot_sdk_smoke PRIVATE qqbot_sdk)

add_executable(qqbot_sdk_api_example
    src/examples/api_example.cpp
)

target_link_libraries(qqbot_sdk_api_example PRIVATE qqbot_sdk)

add_executable(qqbot_sdk_message_reply_example
    src/examples/message_reply_example.cpp
)

target_link_libraries(qqbot_sdk_message_reply_example PRIVATE qqbot_sdk)

enable_testing()
add_test(NAME qqbot_sdk_smoke COMMAND qqbot_sdk_smoke)
