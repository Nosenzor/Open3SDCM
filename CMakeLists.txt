cmake_minimum_required(VERSION 3.16)

# uncomment to set a default CXX standard for the external tools like clang-tidy and cppcheck
# and the targets that do not specify a standard.
# If not set, the latest supported standard for your compiler is used
# You can later set fine-grained standards for each target using `target_compile_features`
# set(CMAKE_CXX_STANDARD 17)
if (NOT CMAKE_TOOLCHAIN_FILE)
  message(STATUS "CMAKE_TOOLCHAIN_FILE not set, setting it to ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
set(CMAKE_TOOLCHAIN_FILE ${CMAKE_CURRENT_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake
    CACHE STRING "Vcpkg toolchain file")
endif()
message(STATUS "CMAKE_TOOLCHAIN_FILE : ${CMAKE_TOOLCHAIN_FILE}")
#if(NOT CMAKE_SYSTEM_PROCESSOR)
#  message(STATUS "CMAKE_SYSTEM_PROCESSOR not set, setting it to ${CMAKE_HOST_SYSTEM_PROCESSOR}")
#  set(CMAKE_SYSTEM_PROCESSOR ${CMAKE_HOST_SYSTEM_PROCESSOR})
#endif()

#Forcing VCPKG to only use release version
set(VCPKG_BUILD_TYPE release)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


## https://github.com/cpp-best-practices/project_options
include(FetchContent)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.24.0")
  cmake_policy(SET CMP0135 NEW)
endif()


# Add project_options from https://github.com/aminya/project_options
# Change the version in the following URL to update the package (watch the releases of the repository for future updates)
set(PROJECT_OPTIONS_VERSION "v0.41.0")
FetchContent_Declare(
    _project_options
    URL https://github.com/aminya/project_options/archive/refs/tags/${PROJECT_OPTIONS_VERSION}.zip)
FetchContent_MakeAvailable(_project_options)
include(${_project_options_SOURCE_DIR}/Index.cmake)

# install vcpkg dependencies: - should be called before defining project()
run_vcpkg(
    VCPKG_URL "https://github.com/microsoft/vcpkg.git"
)



# Set the project name and language
string(TIMESTAMP TODAY "%Y%m%d")
project(Open3SDCM LANGUAGES CXX C VERSION 0.1)



# Build tests and tutorials
option(Open3SDCM_BUILD_TESTS      "Build All Tests"               ON})
option(Open3SDCM_BUILD_BENCH      "Build Benchmark"               OFF})

message("BUILD TYPE : " ${CMAKE_BUILD_TYPE} " USING :" ${CMAKE_CXX_COMPILER_ID} " version : " ${CMAKE_CXX_COMPILER_VERSION})
message("CMAKE_SYSTEM_PROCESSOR = " ${CMAKE_SYSTEM_PROCESSOR})
message("CMAKE_SYSTEM_NAME = " ${CMAKE_SYSTEM_NAME})
message("CMAKE_SYSTEM_VERSION = " ${CMAKE_SYSTEM_VERSION})
message("CMAKE_SYSTEM = " ${CMAKE_SYSTEM})
message("CMAKE_CXX_COMPILER = " ${CMAKE_CXX_COMPILER})
message("CMAKE_CXX_COMPILER_VERSION = " ${CMAKE_CXX_COMPILER_VERSION})
message("CMAKE_CXX_COMPILER_ID = " ${CMAKE_CXX_COMPILER_ID})
message("CMAKE_CXX_COMPILER_FRONTEND_VARIANT = " ${CMAKE_CXX_COMPILER_FRONTEND_VARIANT})
message("CMAKE_CXX_COMPILER_TARGET = " ${CMAKE_CXX_COMPILER_TARGET})
message("CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN = " ${CMAKE_CXX_COMPILER_EXTERNAL_TOOLCHAIN})
message("CMAKE_CXX_COMPILER_LAUNCHER = " ${CMAKE_CXX_COMPILER_LAUNCHER})
message("CMAKE_CXX_COMPILER_WORKS = " ${CMAKE_CXX_COMPILER_WORKS})

# Build Features
option(FEATURE_TESTS "Enable the tests" OFF)
option(FEATURE_DOCS "Enable the docs" OFF)

# vcpkg test feature
if(FEATURE_TESTS)
  list(APPEND VCPKG_MANIFEST_FEATURES "tests")
endif()

# Enable sanitizers and static analyzers when running the tests
if(FEATURE_TESTS)
  set(ENABLE_CLANG_TIDY "ENABLE_CLANG_TIDY")
  set(ENABLE_CPPCHECK "ENABLE_CPPCHECK")
  set(ENABLE_COVERAGE "ENABLE_COVERAGE")

  check_sanitizers_support(ENABLE_SANITIZER_ADDRESS
      ENABLE_SANITIZER_UNDEFINED_BEHAVIOR
      ENABLE_SANITIZER_LEAK
      ENABLE_SANITIZER_THREAD
      ENABLE_SANITIZER_MEMORY)
endif()

# Enable doxgen for the docs
if(FEATURE_DOCS)
  set(ENABLE_DOXYGEN "ENABLE_DOXYGEN")
endif()

# Initialize project_options variable related to this project
# This overwrites `project_options` and sets `project_warnings`
# uncomment to enable the options. Some of them accept one or more inputs:
project_options(
    PREFIX ${PROJECT_NAME}
    ENABLE_CACHE
    ${ENABLE_CPPCHECK}
    ${ENABLE_CLANG_TIDY}
    ENABLE_VS_ANALYSIS
    # ENABLE_CONAN
    ENABLE_INTERPROCEDURAL_OPTIMIZATION
    ENABLE_NATIVE_OPTIMIZATION
    ${ENABLE_DOXYGEN}
    ${ENABLE_COVERAGE}
    ${ENABLE_SANITIZER_ADDRESS}
    ${ENABLE_SANITIZER_UNDEFINED_BEHAVIOR}
    # ${ENABLE_SANITIZER_THREAD}
    # ${ENABLE_SANITIZER_MEMORY}
    # ENABLE_CONTROL_FLOW_PROTECTION
    # ENABLE_STACK_PROTECTION
    # ENABLE_OVERFLOW_PROTECTION
    # ENABLE_ELF_PROTECTION
    # ENABLE_RUNTIME_SYMBOLS_RESOLUTION
    # ENABLE_COMPILE_COMMANDS_SYMLINK
    # ENABLE_PCH
    # PCH_HEADERS
    # WARNINGS_AS_ERRORS
    # ENABLE_INCLUDE_WHAT_YOU_USE
    # ENABLE_GCC_ANALYZER
    # ENABLE_BUILD_WITH_TIME_TRACE
    # ENABLE_UNITY
    # LINKER "lld"
    # CONAN_PROFILE ${profile_path}
)

if (APPLE)
  add_definitions(-DOPENSSL_ROOT_DIR=/usr/local/opt/openssl)
endif ()

if (${CMAKE_CXX_COMPILER_ID} STREQUAL "MSVC" OR WIN32)
    # set stuff for windows)
    message("DISABLE MINMAX MACRO")
    add_definitions(-DNOMINMAX)
endif()


add_subdirectory(Lib)
add_subdirectory(CLI)
add_subdirectory(TestTools)

