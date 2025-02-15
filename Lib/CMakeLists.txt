cmake_minimum_required(VERSION 3.10)
# set the project name
project(OPen3SDCMLib VERSION 0.1 LANGUAGES CXX)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
#disable CRT Warnings (use _getv _putenv etc ...)
if (MSVC)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif ()

#find_package(Boost REQUIRED)
find_package(Poco CONFIG REQUIRED COMPONENTS XML JSON Zip)

set(SOURCES
        ParseDcm.h
        ParseDcm.cpp
)


add_library(${PROJECT_NAME} STATIC ${SOURCES})

#  External include : this will filter Warnings from external libraries
target_include_system_directories(${PROJECT_NAME} PRIVATE
    ${Boost_INCLUDE_DIRS}

    )

set_property(TARGET ${PROJECT_NAME} PROPERTY POSITION_INDEPENDENT_CODE ON)


target_link_system_libraries(${PROJECT_NAME} PRIVATE
    ${Boost_LIBRARIES} Poco::Zip Poco::XML
)

if(MSVC)
  #    Disable warning C4251 (needs to have dll-interface to be used by clients)
  message("DISABLE warning C4251 (needs to have dll-interface to be used by clients)")
  target_compile_options(${PROJECT_NAME} PRIVATE "/wd4251;" )

  message("DISABLE warning C4251 (needs to have dll-interface to be used by clients) + fp:strict")
  target_compile_options(${PROJECT_NAME}
      PRIVATE
      "/wd4251;")
endif()

