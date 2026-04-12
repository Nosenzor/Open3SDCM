# Open3SDCM – Copilot Instructions

## Project Overview

C++20 library and CLI tool for parsing unencrypted 3Shape DCM dental scan files and exporting meshes to STL, PLY, or OBJ. A DCM file is a ZIP archive containing an XML-based HPS (Himsa Packed Scan) file whose geometry nodes hold base64-encoded binary data.

## Build

Dependencies are managed with **vcpkg** (bundled as a git submodule at `./vcpkg`). Use the Ninja preset for local builds:

```bash
cmake -DCMAKE_BUILD_TYPE=Release --preset ninja-release-vcpkg -S . -B ./builds/ninja-release-vcpkg
cmake --build ./builds/ninja-release-vcpkg
```

Binaries land in `./builds/ninja-release-vcpkg/bin/`.

All subprojects inherit the version from the `VERSION` file at the repository root. CMake reads it automatically — do not hardcode versions elsewhere.

## Repository Structure

| Path | Purpose |
|------|---------|
| `Lib/` | Static library (`Open3SDCMLib`) — DCM parsing and mesh export |
| `CLI/` | CLI executable (`Open3SDCMCLI`) — batch-converts a directory of DCM files |
| `TestTools/` | `MeshComparisonTest` binary — compares meshes for regression testing |
| `TestData/` | Sample DCM input files for manual testing |
| `TestOutput/` | Generated output (not committed) |

## Architecture

**Parse flow** (`Lib/src/ParseDcm.cpp`):
1. `DCMParser::ParseDCM(path)` — unzips the DCM, locates the HPS XML, reads the `<Schema>` element, and collects `<Properties>` key/value pairs.
2. `DCMParser::ParseBinaryData(nodes, schema, props)` — dispatches to `detail::ParseVertices` and `detail::ParseFacets`.
3. Each helper base64-decodes the data (`detail::DecodeBuffer`), optionally decrypts (Blowfish/OpenSSL, CE schema only), verifies a CRC32 checksum, then interprets the raw bytes.
4. Vertices are stored in `m_Vertices` as a flat `std::vector<float>` in `x,y,z` order (every 3 floats = one vertex). Triangles are stored in `m_Triangles` as `std::vector<Triangle>` with 0-based indices.
5. `DCMParser::ExportMesh(outputPath, format)` builds an `aiScene` and uses assimp's exporter.

**Supported HPS schemas**: CA, CB, CC (fully working), CE (requires `PackageLockList` property for decryption key derivation via MD5).

**CLI flow** (`CLI/src/main.cpp`):
- Accepts `--input_dir`, `--output_dir`, `--format` (stl/ply/obj).
- Recursively scans for `.dcm`/`.DCM` files, unzips each, parses, and exports.
- Output is written to a timestamped subdirectory under `--output_dir`.

## Key Conventions

- **Namespace**: all library code lives in `namespace Open3SDCM`. Internal implementation helpers go in `namespace Open3SDCM::detail` (anonymous or named) inside `.cpp` files.
- **Poco for XML and ZIP**: use `Poco::XML::DOMParser` / `Poco::AutoPtr<Poco::XML::NodeList>` for XML traversal; use `Poco::Zip::Decompress` for unzipping. Do not use other XML or zip libraries.
- **Formatting**: `fmt::print` / `fmt::format` (not `std::cout` in new CLI code). `spdlog` is available for structured logging.
- **Error reporting**: use `std::cerr` for exceptions caught from Poco, return sensible defaults (empty vectors, `false`) rather than throwing out of public API.
- **Windows compatibility**: wrap `#ifdef MSVC` to add `/wd4251` and `/utf-8` compiler flags; add `-DNOMINMAX` globally on Windows.
- **Clang-format**: LLVM-based style, 2-space indent, pointer-left (`int* p`), column limit 0 (no line-length enforcement). Run `clang-format` before committing.
- **Clang-tidy**: enabled on `FEATURE_TESTS=ON` builds. The `.clang-tidy` file enables a curated set of `bugprone-*`, `modernize-*`, and `performance-*` checks.

## Dependencies (vcpkg manifest)

`boost-test`, `boost-program-options`, `boost-dynamic-bitset`, `fmt`, `poco`, `spdlog`, `assimp`, `openssl`

## Releasing

Bump the `VERSION` file, commit, then push a `vX.Y.Z` tag. GitHub Actions (`release.yml`) will build Linux/macOS/Windows binaries and publish a release automatically.
