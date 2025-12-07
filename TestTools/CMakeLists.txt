# TestTools CMakeLists.txt
cmake_minimum_required(VERSION 3.16)




# Find required packages
find_package(assimp CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Boost REQUIRED COMPONENTS program_options)

# Create the test executable
add_executable(MeshComparisonTest
    src/main.cpp
    src/MeshComparator.cpp
    src/MeshComparator.h
)

target_link_libraries(MeshComparisonTest
    PRIVATE
        Open3SDCMLib
        assimp::assimp
        fmt::fmt
        Boost::program_options
)

target_include_directories(MeshComparisonTest
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/Lib/src
)

target_compile_features(MeshComparisonTest PRIVATE cxx_std_20)

# Set output directory
set_target_properties(MeshComparisonTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

