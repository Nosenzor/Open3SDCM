#!/usr/bin/env bash
# Provision the native dependencies the Python wheel needs.
#
# Usage: ci_bootstrap_vcpkg.sh <triplet> <root-dir>
#
# Deliberately uses vcpkg's classic mode rather than the repository manifest:
# the root vcpkg.json also pulls in assimp, spdlog, fmt and boost-test for the
# CLI and test tools, none of which the bindings link against. Building only
# what Lib/CMakeLists.txt actually needs keeps wheel CI substantially shorter.
set -euo pipefail

TRIPLET="${1:?expected a vcpkg triplet as the first argument}"
ROOT="${2:?expected a root directory as the second argument}"

# On Windows the caller hands us a native path (D:\a\...). Left as-is, bash
# treats the backslash form as relative and silently prefixes the cwd, which
# produced paths like D:\a\repo\D:\a\repo\.vcpkg\scripts\bootstrap.ps1.
if command -v cygpath >/dev/null 2>&1; then
  ROOT="$(cygpath -u "${ROOT}")"
fi

# vcpkg's macOS toolchain passes -isysroot "$SDKROOT". cibuildwheel does not
# export SDKROOT into before_all, so the flag consumed the *next* argument as
# its value (-isysroot -mmacosx-version-min=11.0) and no system header could
# be found. Resolve it up front.
if [ "$(uname -s)" = "Darwin" ] && [ -z "${SDKROOT:-}" ]; then
  SDKROOT="$(xcrun --show-sdk-path)"
  export SDKROOT
  echo "SDKROOT=${SDKROOT}"
fi

VCPKG_COMMIT="${VCPKG_COMMIT:-efb1e7436979a30c4d3e5ab2375fd8e2e461d541}"
VCPKG_DIR="${ROOT}/.vcpkg"
CACHE_DIR="${ROOT}/.vcpkg-cache"

mkdir -p "${CACHE_DIR}"
export VCPKG_DEFAULT_BINARY_CACHE="${CACHE_DIR}"

if [ ! -d "${VCPKG_DIR}/.git" ]; then
  git clone https://github.com/microsoft/vcpkg.git "${VCPKG_DIR}"
fi

git -C "${VCPKG_DIR}" fetch --depth 1 origin "${VCPKG_COMMIT}"
git -C "${VCPKG_DIR}" checkout --force "${VCPKG_COMMIT}"

# vcpkg's bootstrap shells out to zip and unzip, which the manylinux_2_28 image
# does not ship -- it fails with "Could not find zip. Please install it". The
# GitHub-hosted Linux, macOS and Windows runners already have them, so this is a
# no-op outside the cibuildwheel container.
if ! command -v zip >/dev/null 2>&1 || ! command -v unzip >/dev/null 2>&1; then
  echo "installing zip/unzip for vcpkg bootstrap"
  if command -v dnf >/dev/null 2>&1; then
    dnf install -y zip unzip
  elif command -v yum >/dev/null 2>&1; then
    yum install -y zip unzip
  elif command -v apt-get >/dev/null 2>&1; then
    apt-get update && apt-get install -y zip unzip
  else
    echo "warning: no supported package manager found to install zip/unzip" >&2
  fi
fi

if [ -x "${VCPKG_DIR}/vcpkg" ] || [ -x "${VCPKG_DIR}/vcpkg.exe" ]; then
  echo "vcpkg already bootstrapped"
elif [ -f "${VCPKG_DIR}/bootstrap-vcpkg.bat" ] && [ "${OS:-}" = "Windows_NT" ]; then
  "${VCPKG_DIR}/bootstrap-vcpkg.bat" -disableMetrics
else
  "${VCPKG_DIR}/bootstrap-vcpkg.sh" -disableMetrics
fi

VCPKG_BIN="${VCPKG_DIR}/vcpkg"
[ -x "${VCPKG_BIN}" ] || VCPKG_BIN="${VCPKG_DIR}/vcpkg.exe"

# Mirrors the find_package() calls in Lib/CMakeLists.txt.
#
# poco is requested without a feature list on purpose. At the baseline pinned
# above, the poco port builds XML, JSON and Zip into the core package and does
# not expose them as named features -- asking for poco[xml,json,zip] there fails
# with "poco has no feature named zip". Newer vcpkg splits them out into
# features instead, so the correct spelling depends on the baseline; plain
# "poco" is what the repository's own vcpkg.json requests and what the C++ CI
# already builds against. Revisit this if VCPKG_COMMIT moves forward.
"${VCPKG_BIN}" install \
  --classic \
  --triplet "${TRIPLET}" \
  --x-buildtrees-root "${ROOT}/.vcpkg-bt" \
  --clean-after-build \
  poco \
  openssl \
  boost-headers

echo "vcpkg dependencies ready for ${TRIPLET}"
