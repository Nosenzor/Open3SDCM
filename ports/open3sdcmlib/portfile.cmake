# This port packages only the Lib/ subdirectory of the Open3SDCM monorepo
# (the CLI executable is intentionally not part of this port).
#
# NOTE: on every version bump, REF must point at a newly tagged commit that
# contains Lib/CMakeLists.txt's install()/export() support, and SHA512 must
# be updated to match. To pick up the new hash: set SHA512 to "0", run
# `vcpkg install open3sdcmlib --overlay-ports=<repo>/ports`, and copy the
# value vcpkg reports back in here.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Nosenzor/Open3SDCM
    REF "v${VERSION}"
    SHA512 765b97c3bd3d858dc6c8e64b3fe1254b8073b7f1da52b5cce255189bbc046b2a1d591bc47f4149b9620971ac0ed2cb6ea5a2586b3d0e734974b5a87c5e231289
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}/Lib"
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH "lib/cmake/Open3SDCMLib")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
