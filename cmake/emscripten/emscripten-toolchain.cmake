# Chainload wrapper toolchain for vcpkg port builds targeting Emscripten.
#
# vcpkg's community wasm32-emscripten triplet includes Emscripten.cmake
# directly, which silently drops the VCPKG_* flag variables. This wrapper
# includes Emscripten.cmake and then applies them (like vcpkg's own
# per-platform toolchain files do), plus two Emscripten-specific settings
# required by this project's dependencies:
#
#  * -fexceptions: the parser library relies on catching Poco exceptions
#    (malformed DCM files); Emscripten disables exception catching by
#    default, which would turn those errors into WebAssembly aborts.
#  * -DPOCO_NO_INOTIFY: Poco 1.14 detects Emscripten as Linux and would
#    otherwise compile its inotify-based DirectoryWatcher, but Emscripten
#    has no sys/inotify.h.

if(DEFINED ENV{EMSCRIPTEN_ROOT})
    set(EMSCRIPTEN_ROOT "$ENV{EMSCRIPTEN_ROOT}")
elseif(DEFINED ENV{EMSDK})
    set(EMSCRIPTEN_ROOT "$ENV{EMSDK}/upstream/emscripten")
else()
    find_path(EMSCRIPTEN_ROOT emcc)
endif()

if(NOT EMSCRIPTEN_ROOT OR NOT EXISTS "${EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")
    message(FATAL_ERROR
        "Emscripten.cmake toolchain file not found. Install the emsdk and "
        "activate it (source emsdk_env.sh) so EMSDK is set or emcc is in PATH.")
endif()

include("${EMSCRIPTEN_ROOT}/cmake/Modules/Platform/Emscripten.cmake")

# Apply the vcpkg flag variables that the plain Emscripten toolchain drops.
if(VCPKG_C_FLAGS)
    string(APPEND CMAKE_C_FLAGS_INIT " ${VCPKG_C_FLAGS}")
endif()
if(VCPKG_CXX_FLAGS)
    string(APPEND CMAKE_CXX_FLAGS_INIT " ${VCPKG_CXX_FLAGS}")
endif()
if(VCPKG_C_FLAGS_DEBUG)
    string(APPEND CMAKE_C_FLAGS_DEBUG_INIT " ${VCPKG_C_FLAGS_DEBUG}")
endif()
if(VCPKG_CXX_FLAGS_DEBUG)
    string(APPEND CMAKE_CXX_FLAGS_DEBUG_INIT " ${VCPKG_CXX_FLAGS_DEBUG}")
endif()
if(VCPKG_C_FLAGS_RELEASE)
    string(APPEND CMAKE_C_FLAGS_RELEASE_INIT " ${VCPKG_C_FLAGS_RELEASE}")
endif()
if(VCPKG_CXX_FLAGS_RELEASE)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE_INIT " ${VCPKG_CXX_FLAGS_RELEASE}")
endif()
if(VCPKG_LINKER_FLAGS)
    string(APPEND CMAKE_EXE_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS}")
    string(APPEND CMAKE_SHARED_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS}")
    string(APPEND CMAKE_MODULE_LINKER_FLAGS_INIT " ${VCPKG_LINKER_FLAGS}")
endif()

string(APPEND CMAKE_CXX_FLAGS_INIT " -fexceptions -DPOCO_NO_INOTIFY")

# Poco's POSIX thread implementation (built as part of Foundation) includes
# <sys/prctl.h> for thread naming, which Emscripten does not provide; the
# shim next to this file satisfies it.
string(APPEND CMAKE_CXX_FLAGS_INIT " -isystem ${CMAKE_CURRENT_LIST_DIR}/shim")
string(APPEND CMAKE_C_FLAGS_INIT " -isystem ${CMAKE_CURRENT_LIST_DIR}/shim")
