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
# perl is additionally required to configure OpenSSL, and ninja is the generator
# vcpkg drives for compiler detection.
missing=""
for tool in zip unzip perl ninja; do
  command -v "${tool}" >/dev/null 2>&1 || missing="${missing} ${tool}"
done
if [ -n "${missing}" ]; then
  echo "installing missing vcpkg prerequisites:${missing}"
  # Package names differ from binary names for ninja.
  pkgs="$(echo "${missing}" | sed 's/\bninja\b/ninja-build/')"
  if command -v dnf >/dev/null 2>&1; then
    dnf install -y ${pkgs} || echo "warning: dnf could not install${missing}" >&2
  elif command -v yum >/dev/null 2>&1; then
    yum install -y ${pkgs} || echo "warning: yum could not install${missing}" >&2
  elif command -v apt-get >/dev/null 2>&1; then
    apt-get update && apt-get install -y ${pkgs} || echo "warning: apt could not install${missing}" >&2
  else
    echo "warning: no supported package manager found to install${missing}" >&2
  fi
fi

# OpenSSL's Configure is a Perl program that pulls in modules the manylinux image
# ships Perl without -- it fails with "Perl cannot find IPC::Cmd". Debian-family
# perl packages bundle these, so this is RHEL-family only.
if ! perl -MIPC::Cmd -e1 >/dev/null 2>&1; then
  perl_pkgs="perl-IPC-Cmd perl-Data-Dumper perl-FindBin perl-File-Compare perl-File-Copy perl-Digest-SHA"
  echo "installing Perl modules for OpenSSL's Configure"
  if command -v dnf >/dev/null 2>&1; then
    dnf install -y ${perl_pkgs} || echo "warning: dnf could not install Perl modules" >&2
  elif command -v yum >/dev/null 2>&1; then
    yum install -y ${perl_pkgs} || echo "warning: yum could not install Perl modules" >&2
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
# vcpkg reports failures by pointing at log files under the buildtrees root, which
# CI never surfaces -- the run just says "See logs for more information" and exits.
# Dump them on the way out so a failed run is diagnosable from the job output alone.
dump_vcpkg_logs() {
  status=$?
  [ "${status}" -eq 0 ] && return 0
  echo "=== vcpkg failed (exit ${status}); dumping logs ==="
  find "${ROOT}/.vcpkg-bt" -name '*.log' -size -256k 2>/dev/null | head -20 | while read -r log; do
    echo "--- ${log} ---"
    tail -60 "${log}"
  done
  echo "=== compiler probe ==="
  echo "CC=${CC:-unset} CXX=${CXX:-unset}"
  command -v cc gcc g++ ninja cmake perl 2>/dev/null || true
  (gcc --version 2>&1 | head -1) || true
  return "${status}"
}
trap dump_vcpkg_logs EXIT

"${VCPKG_BIN}" install \
  --classic \
  --triplet "${TRIPLET}" \
  --x-buildtrees-root "${ROOT}/.vcpkg-bt" \
  --clean-after-build \
  poco \
  openssl \
  boost-headers

trap - EXIT
echo "vcpkg dependencies ready for ${TRIPLET}"
