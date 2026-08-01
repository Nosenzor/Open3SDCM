cmake_minimum_required(VERSION 3.26)
project(open3sdcm_python LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Development.SABIModule is what actually enables nanobind's STABLE_ABI below:
# without the Python::SABIModule target, nanobind silently downgrades to a
# version-specific module while scikit-build-core still tags the wheel abi3.
# Optional so that Python < 3.12, which has no stable ABI support, still builds.
find_package(Python 3.9 REQUIRED
    COMPONENTS Interpreter Development.Module
    OPTIONAL_COMPONENTS Development.SABIModule)

# Locate nanobind through the interpreter that is driving this build, so the
# configure works with or without pip build isolation.
if(NOT nanobind_ROOT)
  execute_process(
      COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
      OUTPUT_STRIP_TRAILING_WHITESPACE
      OUTPUT_VARIABLE nanobind_ROOT
      COMMAND_ERROR_IS_FATAL ANY)
endif()
find_package(nanobind CONFIG REQUIRED)

# Reuse the C++ library rather than recompiling its sources here, so the
# binding can never drift from the library's own source/dependency list.
# Its install/export rules are for the vcpkg port, not for this wheel.
set(OPEN3SDCMLIB_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../Lib" "${CMAKE_CURRENT_BINARY_DIR}/Open3SDCMLib")

# STABLE_ABI yields one abi3 wheel per platform on Python >= 3.12 (nanobind
# silently ignores it on older interpreters).
nanobind_add_module(_core STABLE_ABI NB_STATIC src/open3sdcm_ext.cpp)
target_link_libraries(_core PRIVATE Open3SDCMLib)

install(TARGETS _core LIBRARY DESTINATION open3sdcm)
