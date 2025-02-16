cmake_minimum_required(VERSION 3.10)

project(Open3SDCMCLI VERSION 0.1 LANGUAGES CXX)

# specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
#disable CRT Warnings (use _getv _putenv etc ...)
if (MSVC)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif ()


set(SRC_DIR "src")
set(LOCALITF_DIR "LocalInterfaces")
add_executable(${PROJECT_NAME} src/main.cpp)


#find_package(assimp CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS  program_options)
find_package(Poco CONFIG REQUIRED COMPONENTS XML JSON Zip)
find_package(assimp CONFIG REQUIRED)

target_include_directories(${PROJECT_NAME} PRIVATE
    "../Lib/src"
)

target_include_system_directories( ${PROJECT_NAME} PRIVATE
    ${Boost_INCLUDE_DIRS}
)

target_link_libraries(${PROJECT_NAME} PRIVATE Open3SDCM_project_options Open3SDCM_project_warnings
        Open3SDCMLib
)

target_link_system_libraries(${PROJECT_NAME} PRIVATE
    Boost::program_options
    assimp::assimp
    Poco::Zip
    spdlog::spdlog_header_only
    )

if(MSVC)
  #    Disable warning C4251 (needs to have dll-interface to be used by clients)
  message("DISABLE warning C4251 (needs to have dll-interface to be used by clients)")
  target_compile_options(${PROJECT_NAME}
      PRIVATE
      "/wd4251;"
  )
endif()