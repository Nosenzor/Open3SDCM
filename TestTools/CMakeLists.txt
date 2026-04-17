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

if(MSVC)
  target_compile_options(MeshComparisonTest PRIVATE "/utf-8")
endif()

# Set output directory
set_target_properties(MeshComparisonTest PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# ---------------------------------------------------------------------------
# Real-world scan batch conversion tests (Boost.Test + CTest)
# Each of the 5 real-world dental scans is registered as an independent CTest
# entry so that failures are reported per-scan in CI output.
# ---------------------------------------------------------------------------
if(BUILD_TESTING)
  # CMAKE_SOURCE_DIR uses forward slashes on all platforms (CMake convention),
  # so passing it as a string literal is safe without backslash escaping.
  add_executable(RealWorldTest
      src/RealWorldTest.cpp
  )

  target_compile_definitions(RealWorldTest
      PRIVATE "TEST_DATA_DIR=\"${CMAKE_SOURCE_DIR}/TestData\""
  )

  target_link_libraries(RealWorldTest
      PRIVATE
          Open3SDCMLib
          fmt::fmt
  )

  target_include_directories(RealWorldTest
      PRIVATE
          ${CMAKE_SOURCE_DIR}/Lib/src
  )

  target_compile_features(RealWorldTest PRIVATE cxx_std_20)

  if(MSVC)
    target_compile_options(RealWorldTest PRIVATE "/utf-8")
  endif()

  set_target_properties(RealWorldTest PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
  )

  add_test(NAME RealWorld_scan_040
      COMMAND RealWorldTest --run_test=RealWorldConversion/ConvertScan040 --log_level=message)
  add_test(NAME RealWorld_scan_039
      COMMAND RealWorldTest --run_test=RealWorldConversion/ConvertScan039 --log_level=message)
  add_test(NAME RealWorld_scan_012
      COMMAND RealWorldTest --run_test=RealWorldConversion/ConvertScan012 --log_level=message)
  add_test(NAME RealWorld_scan_019
      COMMAND RealWorldTest --run_test=RealWorldConversion/ConvertScan019 --log_level=message)
  add_test(NAME RealWorld_scan_045
      COMMAND RealWorldTest --run_test=RealWorldConversion/ConvertScan045 --log_level=message)
endif()

