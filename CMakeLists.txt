# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Copyright (C) 2026 Ezequiel Alves. All rights reserved.

cmake_minimum_required(VERSION 3.20)
project(condor VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDEs and tooling
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler warning flags
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
endif()

include(FetchContent)

# Option to use local vendored third_party dependencies (defaults to ON if present)
option(CONDOR_USE_LOCAL_DEPS "Use local vendored third_party dependencies if available" ON)

# --- CLI11 Dependency ---
if(CONDOR_USE_LOCAL_DEPS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CLI11/CMakeLists.txt")
    message(STATUS "Condor: Using local third_party CLI11")
    set(FETCHCONTENT_SOURCE_DIR_CLI11 "${CMAKE_CURRENT_SOURCE_DIR}/third_party/CLI11")
endif()

FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG        v2.6.0
    GIT_SHALLOW    TRUE
)

# --- yaml-cpp Dependency ---
if(CONDOR_USE_LOCAL_DEPS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/yaml-cpp/CMakeLists.txt")
    message(STATUS "Condor: Using local third_party yaml-cpp")
    set(FETCHCONTENT_SOURCE_DIR_YAML-CPP "${CMAKE_CURRENT_SOURCE_DIR}/third_party/yaml-cpp")
endif()

# Disable yaml-cpp tests and tooling
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "Build static yaml-cpp" FORCE)

FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        yaml-cpp-0.9.0
    GIT_SHALLOW    TRUE
)

# --- nlohmann_json Dependency ---
if(CONDOR_USE_LOCAL_DEPS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json/CMakeLists.txt")
    message(STATUS "Condor: Using local third_party nlohmann_json")
    set(FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON "${CMAKE_CURRENT_SOURCE_DIR}/third_party/nlohmann_json")
endif()

set(JSON_BuildTests OFF CACHE BOOL "Disable nlohmann_json tests" FORCE)
set(JSON_Install OFF CACHE BOOL "Disable nlohmann_json install" FORCE)

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)

# --- json-schema-validator Dependency ---
if(CONDOR_USE_LOCAL_DEPS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json-schema-validator/CMakeLists.txt")
    message(STATUS "Condor: Using local third_party json-schema-validator")
    set(FETCHCONTENT_SOURCE_DIR_NLOHMANN_JSON_SCHEMA_VALIDATOR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/json-schema-validator")
endif()

set(JSON_VALIDATOR_BUILD_TESTS OFF CACHE BOOL "Disable validator tests" FORCE)
set(JSON_VALIDATOR_BUILD_EXAMPLES OFF CACHE BOOL "Disable validator examples" FORCE)
set(JSON_VALIDATOR_INSTALL OFF CACHE BOOL "Disable validator install" FORCE)

FetchContent_Declare(
    nlohmann_json_schema_validator
    GIT_REPOSITORY https://github.com/pboettch/json-schema-validator.git
    GIT_TAG        2.4.1
    GIT_SHALLOW    TRUE
)

# Resolution: Check system first if custom flow, otherwise FetchContent
find_package(yaml-cpp 0.9.0 QUIET)
if(NOT yaml-cpp_FOUND)
    FetchContent_MakeAvailable(yaml-cpp)
endif()

find_package(CLI11 2.6 QUIET)
if(NOT CLI11_FOUND)
    FetchContent_MakeAvailable(CLI11)
endif()

find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    FetchContent_MakeAvailable(nlohmann_json)
endif()

find_package(nlohmann_json_schema_validator QUIET)
if(NOT nlohmann_json_schema_validator_FOUND)
    FetchContent_MakeAvailable(nlohmann_json_schema_validator)
endif()

# Core library source files
set(CONDOR_LIB_SOURCES
    src/logging.cpp
    src/variable.cpp
    src/expression.cpp
    src/workspace.cpp
    src/parser.cpp
    src/schema.cpp
    src/backend.cpp
    src/progress.cpp
    src/menu.cpp
    src/scheduler.cpp
    src/html_report.cpp
)

add_library(condor_core STATIC ${CONDOR_LIB_SOURCES})
target_include_directories(condor_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_link_libraries(condor_core PUBLIC
    CLI11::CLI11
    yaml-cpp::yaml-cpp
    nlohmann_json::nlohmann_json
    nlohmann_json_schema_validator::validator
    pthread
)

# Condor Main Executable
add_executable(condor src/main.cpp)
target_compile_definitions(condor PRIVATE CONDOR_VERSION="${PROJECT_VERSION}")
target_link_libraries(condor PRIVATE condor_core)

# Tests
enable_testing()

add_executable(condor_tests
    tests/test_main.cpp
    tests/test_validation.cpp
    tests/test_variable.cpp
    tests/test_expression.cpp
    tests/test_workspace.cpp
    tests/test_scheduler.cpp
    tests/test_pipeline.cpp
)
target_link_libraries(condor_tests PRIVATE condor_core)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(condor_core PRIVATE -Werror)
    target_compile_options(condor PRIVATE -Werror)
    target_compile_options(condor_tests PRIVATE -Werror)
endif()

add_test(NAME AllCondorTests COMMAND condor_tests)

# ==============================================================================
# Installation Rules
# ==============================================================================
include(GNUInstallDirs)

install(TARGETS condor
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY examples/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/condor/examples
    PATTERN ".*" EXCLUDE
)

install(DIRECTORY res/backends/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/condor/backends
    PATTERN ".*" EXCLUDE
)

install(DIRECTORY res/schema/
    DESTINATION ${CMAKE_INSTALL_DATADIR}/condor/schema
    PATTERN ".*" EXCLUDE
)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    DESTINATION ${CMAKE_INSTALL_DOCDIR}
)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/_site/index.html")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/_site/"
        DESTINATION ${CMAKE_INSTALL_DOCDIR}/html
        PATTERN ".*" EXCLUDE
    )
endif()

# ==============================================================================
# CPack Packaging Configuration
# ==============================================================================
include(cmake/Packaging.cmake)
