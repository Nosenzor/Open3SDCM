cmake_minimum_required(VERSION 3.10)
# set the project name
project(Open3SDCMLib VERSION ${Open3SDCM_VERSION} LANGUAGES CXX)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
#disable CRT Warnings (use _getv _putenv etc ...)
if (MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif ()

#find_package(Boost REQUIRED)
find_package(Poco CONFIG REQUIRED COMPONENTS XML JSON Zip)
find_package(boost_dynamic_bitset CONFIG REQUIRED)
find_package(assimp CONFIG REQUIRED)
find_package(OpenSSL REQUIRED)
set(SOURCES
        src/ParseDcm.h
        src/ParseDcm.cpp
        src/definitions.h
)


add_library(${PROJECT_NAME} STATIC ${SOURCES})


#  External include : this will filter Warnings from external libraries
target_include_system_directories(${PROJECT_NAME} PRIVATE
    ${Boost_INCLUDE_DIRS}

    )

set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)


target_link_system_libraries(${PROJECT_NAME} PRIVATE
    ${Boost_LIBRARIES} Boost::dynamic_bitset Poco::Zip Poco::XML assimp::assimp OpenSSL::Crypto
)

if(MSVC)
  #    Disable warning C4251 (needs to have dll-interface to be used by clients)
  message("DISABLE warning C4251 (needs to have dll-interface to be used by clients)")
  target_compile_options(${PROJECT_NAME} PRIVATE "/wd4251;" )

  message("DISABLE warning C4251 (needs to have dll-interface to be used by clients) + fp:strict")
  target_compile_options(${PROJECT_NAME}
      PRIVATE
      "/wd4251;"
      "/utf-8")
endif()
