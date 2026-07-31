# This port packages only the Lib/ subdirectory of the Open3SDCM monorepo
# (the CLI executable is intentionally not part of this port).
#
# NOTE: REF/SHA512 must point at a commit or tag that already contains
# Lib/CMakeLists.txt's install()/export() support. Until that is tagged and
# pushed, configure will fail to fetch. To pick up the real hash: set
# SHA512 to "0", run `vcpkg install open3sdcmlib --overlay-ports=<repo>/ports`,
# and copy the value vcpkg reports back in here.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO Nosenzor/Open3SDCM
    REF "v${VERSION}"
    SHA512 0
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
