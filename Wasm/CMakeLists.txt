# WebAssembly bindings for Open3SDCMLib.
#
# Only reachable when the build is driven by the Emscripten toolchain
# (see the 'wasm-release' CMake preset). Produces in ${CMAKE_BINARY_DIR}/bin:
#   open3sdcm.js   - ES6 module loader (MODULARIZE + EXPORT_ES6)
#   open3sdcm.wasm - the compiled library and bindings

if(NOT EMSCRIPTEN)
  message(FATAL_ERROR
      "Wasm/ requires the Emscripten toolchain. Install and activate the "
      "emsdk (source emsdk_env.sh), then configure with the 'wasm-release' "
      "CMake preset.")
endif()

add_executable(Open3SDCMWasm src/open3sdcm_wasm.cpp)

target_compile_features(Open3SDCMWasm PRIVATE cxx_std_20)
target_link_libraries(Open3SDCMWasm PRIVATE Open3SDCMLib)

# Emscripten disables exception catching by default, which would turn every
# Poco error (e.g. a malformed DCM) into an abort. Enable exceptions for the
# bindings; Lib/CMakeLists.txt does the same for the library itself.
target_compile_options(Open3SDCMWasm PRIVATE "-fexceptions")

set_target_properties(Open3SDCMWasm PROPERTIES
    OUTPUT_NAME "open3sdcm"
)

target_link_options(Open3SDCMWasm PRIVATE
    "-lembind"
    "-fexceptions"
    "-sMODULARIZE=1"
    "-sEXPORT_ES6=1"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sENVIRONMENT=web,worker,node"
    "-sFILESYSTEM=1"
    "-sEXIT_RUNTIME=0"
    "-sEXPORTED_RUNTIME_METHODS=FS,HEAPU8,wasmMemory"
    "-sEXPORT_NAME=createOpen3SDCM"
)
