cmake_minimum_required(VERSION 3.16)
# set the project name
project(Open3SDCMLib LANGUAGES CXX VERSION 1.1.2)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
#disable CRT Warnings (use _getv _putenv etc ...)
if (MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif ()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Embedders that consume this library via add_subdirectory (e.g. the Python
# bindings wheel build) don't want its install/export rules leaking into their
# own install tree. The vcpkg port and standalone builds need them, so ON.
option(OPEN3SDCMLIB_INSTALL "Generate install and export rules for Open3SDCMLib" ON)

#find_package(Boost REQUIRED)
find_package(Poco CONFIG REQUIRED COMPONENTS XML JSON Zip)
find_package(Boost REQUIRED)
find_package(OpenSSL REQUIRED)
set(SOURCES
        src/ParseDcm.h
        src/ParseDcm.cpp
        src/definitions.h
)


add_library(${PROJECT_NAME} STATIC ${SOURCES})
add_library(Open3SDCM::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)

# Emscripten disables exception catching by default, which would strip the
# try/catch blocks around the Poco calls and turn any malformed DCM into a
# WebAssembly abort instead of a graceful parse failure. The Wasm/ bindings
# enable the same flag for the final link.
if(EMSCRIPTEN)
  target_compile_options(${PROJECT_NAME} PRIVATE "-fexceptions")
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/Open3SDCM>
)

# Link libraries
# Poco::XML is PUBLIC: ParseDcm.h includes <Poco/DOM/AutoPtr.h> / <Poco/DOM/NodeList.h>
# in its public interface, so consumers need Poco::XML's include dirs too.
target_link_libraries(${PROJECT_NAME}
    PUBLIC
        Poco::XML
    PRIVATE
        Boost::headers
        Poco::Zip
        OpenSSL::Crypto
)

if(MSVC)
  # Disable warning C4251 (needs to have dll-interface to be used by clients)
  target_compile_options(${PROJECT_NAME} PRIVATE "/wd4251")
endif()

# ---- Install / export (required for vcpkg packaging) ----

if(OPEN3SDCMLIB_INSTALL)

set(Open3SDCMLib_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")

install(TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}Targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(FILES
    src/ParseDcm.h
    src/definitions.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/Open3SDCM
)

install(EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE Open3SDCM::
    DESTINATION ${Open3SDCMLib_INSTALL_CMAKEDIR}
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION ${Open3SDCMLib_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    DESTINATION ${Open3SDCMLib_INSTALL_CMAKEDIR}
)

endif() # OPEN3SDCMLIB_INSTALL
