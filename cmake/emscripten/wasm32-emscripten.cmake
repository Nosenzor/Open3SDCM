# vcpkg triplet for WebAssembly/Emscripten builds.
#
# Overlay of vcpkg's community wasm32-emscripten triplet (point
# VCPKG_OVERLAY_TRIPLETS at this directory) that chainloads the wrapper
# toolchain shipped next to it instead of Emscripten.cmake directly, so
# Emscripten-specific port fixes apply (see emscripten-toolchain.cmake).

set(VCPKG_ENV_PASSTHROUGH_UNTRACKED EMSCRIPTEN_ROOT EMSDK PATH)

set(VCPKG_TARGET_ARCHITECTURE wasm32)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_CMAKE_SYSTEM_NAME Emscripten)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/emscripten-toolchain.cmake")
