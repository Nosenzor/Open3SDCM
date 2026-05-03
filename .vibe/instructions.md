# Open3SDCM - Vibe Instructions

## Project Context

**Open3SDCM** is a C++20 library and CLI tool for parsing unencrypted 3Shape DCM dental scan files and exporting meshes to STL, PLY, or OBJ formats.

- **DCM files** are ZIP archives containing XML-based HPS (Himsa Packed Scan) files
- **Geometry nodes** hold base64-encoded binary vertex and triangle data
- **Namespace**: `Open3SDCM` (library), `Open3SDCM::detail` (internal helpers)

## Repository Structure

```
Open3SDCM/
├── Lib/              # Static library (Open3SDCMLib) - DCM parsing and mesh export
├── CLI/              # CLI executable (Open3SDCMCLI) - batch conversion
├── TestTools/        # MeshComparisonTest binary for regression testing
├── TestData/         # Sample DCM input files
├── TestOutput/       # Generated output (not committed)
├── CMakeLists.txt    # Root CMake configuration
├── CMakePresets.json # Build presets for different configurations
├── vcpkg.json        # vcpkg manifest for dependencies
└── VERSION           # Version file (read by CMake)
```

## Build System

- **Dependencies**: vcpkg manifest mode (`boost-test`, `boost-program-options`, `boost-dynamic-bitset`, `fmt`, `poco`, `spdlog`, `assimp`, `openssl`)
- **Build command**: `cmake -DCMAKE_BUILD_TYPE=Release --preset ninja-release-vcpkg -S . -B ./builds/ninja-release-vcpkg`
- **Version**: Read from `VERSION` file at repo root - do NOT hardcode versions
- **Presets**: Defined in `CMakePresets.json` for different build configurations

## Key Conventions

### Code Style
- **Formatting**: LLVM-based clang-format style, 2-space indent, pointer-left (`int* p`)
- **Line length**: No enforcement (column limit 0)
- ** run clang-format** before committing

### C++ Standards
- **C++20** throughout
- **RAII**: Prefer value semantics, make ownership explicit
- **Error handling**: Return sensible defaults (empty vectors, `false`) from public API rather than throwing
- **Logging**: Use `spdlog` for structured logging, `fmt::print` for CLI output (not `std::cout`)

### Platform Compatibility
- **Windows**: Wrap with `#ifdef MSVC`, add `/wd4251` and `/utf-8` flags, `-DNOMINMAX` globally
- **Cross-platform**: All code must work on Linux, macOS, and Windows

### Libraries
- **XML/Zip**: Use `Poco::XML::DOMParser` and `Poco::Zip::Decompress` (NOT other libraries)
- **Mesh export**: assimp (`aiScene` exporter)
- **Crypto**: OpenSSL for Blowfish decryption (CE schema only)

## Parse Flow (Lib/src/ParseDcm.cpp)

1. `DCMParser::ParseDCM(path)` - unzips DCM, locates HPS XML, reads `<Schema>`, collects `<Properties>`
2. `DCMParser::ParseBinaryData(nodes, schema, props)` - dispatches to `detail::ParseVertices` and `detail::ParseFacets`
3. Each helper: base64-decode → optionally decrypt (Blowfish, CE only) → verify CRC32 → interpret raw bytes
4. Vertices: flat `std::vector<float>` in `x,y,z` order (3 floats = 1 vertex)
5. Triangles: `std::vector<Triangle>` with 0-based indices
6. `DCMParser::ExportMesh(outputPath, format)` - builds `aiScene`, uses assimp exporter

## Supported HPS Schemas

- **CA, CB, CC**: Fully working
- **CE**: Requires `PackageLockList` property for decryption key derivation via MD5

## CLI Flow (CLI/src/main.cpp)

- Arguments: `--input_dir`, `--output_dir`, `--format` (stl/ply/obj)
- Recursively scans for `.dcm`/`.DCM` files
- Unzips each, parses, exports to timestamped subdirectory under `--output_dir`

## CI/CD (GitHub Actions)

- **CI** (`ci.yml`): Builds on Linux (GCC/Clang), macOS, Windows with tests and sanitizers
- **Release** (`release.yml`): Builds and publishes cross-platform binaries on tag push (`v*`)
- **Presets used**: `ninja-release-vcpkg-tests`, `ninja-sanitizers-vcpkg`

## Testing

- **ctest** with CMake presets
- **Sanitizers**: ASAN, UBSAN enabled in sanitizers job
- **Clang-tidy**: Enabled on `FEATURE_TESTS=ON` builds

## Releasing

1. Bump the `VERSION` file
2. Commit the change
3. Push a `vX.Y.Z` tag
4. GitHub Actions automatically builds and publishes release

## Vibe Configuration

- **Primary agent**: Use `expert-cpp-software-engineer.agent.md` for C++ tasks
- **Skills**: Available in `.vibe/skills/` directory
- **Instructions**: Domain-specific instructions in `.vibe/instructions/`
