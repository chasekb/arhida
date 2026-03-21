cmake_minimum_required(VERSION 3.16)
project(arhida-embeddings-service VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
)
FetchContent_MakeAvailable(json)

FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)
FetchContent_MakeAvailable(spdlog)

find_package(Drogon CONFIG REQUIRED)

# ONNX Runtime is intentionally linked as an interface dependency in Phase 6.
# Concrete model/session usage is introduced in Phase 8.
find_package(onnxruntime CONFIG QUIET)
if(NOT onnxruntime_FOUND)
    add_library(onnxruntime_external INTERFACE)
    if(DEFINED ENV{ONNXRUNTIME_ROOT})
        target_include_directories(onnxruntime_external INTERFACE $ENV{ONNXRUNTIME_ROOT}/include)
        target_link_directories(onnxruntime_external INTERFACE $ENV{ONNXRUNTIME_ROOT}/lib)
        target_link_libraries(onnxruntime_external INTERFACE onnxruntime)
    endif()
else()
    add_library(onnxruntime_external INTERFACE)
    target_link_libraries(onnxruntime_external INTERFACE onnxruntime::onnxruntime)
endif()

add_executable(arhida-embeddings-service
    src/main.cpp
    src/config/EmbeddingServiceConfig.cpp
    src/server/HttpServer.cpp
)

target_include_directories(arhida-embeddings-service PRIVATE include)

target_link_libraries(arhida-embeddings-service
    Drogon::Drogon
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    onnxruntime_external
)