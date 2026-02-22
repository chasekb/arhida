cmake_minimum_required(VERSION 3.16)
project(arhida-cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED libpq)
pkg_check_modules(LIBCURL REQUIRED libcurl)
pkg_check_modules(LIBXML2 REQUIRED libxml-2.0)

# Fetch header-only libraries
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

FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
)
FetchContent_MakeAvailable(cli11)

# Include directories
include_directories(${LIBPQ_INCLUDE_DIRS})
include_directories(${LIBCURL_INCLUDE_DIRS})
include_directories(${LIBXML2_INCLUDE_DIRS})
include_directories(include)

# Source files
set(SOURCES
    src/main.cpp
    src/config/Config.cpp
    src/oai/OaiClient.cpp
    src/oai/Record.cpp
    src/db/Database.cpp
    src/db/QueryBuilder.cpp
    src/harvester/Harvester.cpp
    src/harvester/RateLimiter.cpp
    src/utils/Logger.cpp
    src/utils/JsonHelper.cpp
)

# Create executable
add_executable(arhida-cpp ${SOURCES})

# Link libraries
target_link_libraries(arhida-cpp
    ${LIBPQ_LIBRARIES}
    ${LIBCURL_LIBRARIES}
    ${LIBXML2_LIBRARIES}
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    CLI11::CLI11
    pthread
)

# Install target
install(TARGETS arhida-cpp DESTINATION bin)

# Testing (optional)
enable_testing()
