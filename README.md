# Open3SDCM

An open-source C++20 library and CLI tool for converting 3Shape DCM files to standard 3D formats (STL, OBJ, PLY).

## Features & Achievements

| Status | Feature | Description | Version |
|--------|---------|-------------|---------|
| ✅ | **Read mesh geometry** | Extract vertices and triangles from DCM files | v0.1.0 |
| ✅ | **Support schemas CA, CB, CC** | Basic unencrypted schema support | v0.1.0 |
| ✅ | **Support schema CE** | Encrypted DCM files with Blowfish decryption | v1.0.0 |
| ✅ | **Convert to STL, PLY, OBJ** | Mesh export via Assimp library | v0.1.0 |
| ✅ | **Read mesh colors** | Per-vertex color data extraction | v1.1.0 |
| ✅ | **Read UV mapping and textures** | Texture coordinate and mapping support | v1.1.0 |
| 🚧 | **Read extra curves** | Spline and annotation data | Planned |

### Not in Scope
- Write DCM files
- Read heavily encrypted files without decryption keys

---

## Algorithms for DCM Parsing

Open3SDCM uses a multi-stage pipeline to parse DCM files. Each DCM file is a ZIP archive containing an HPS (Himsa Packed Scan) XML file with base64-encoded binary data.

### Stage 1: File Extraction & XML Parsing

```
DCM File (ZIP archive)
    ↓
Unzip using Poco::Zip::Decompress
    ↓
Locate HPS XML file
    ↓
Parse with Poco::XML::DOMParser
    ↓
Extract:
  - <Schema> element (CA, CB, CC, CE)
  - <Properties> key/value pairs
  - <Binary_data> container
```

### Stage 2: Binary Data Discovery

The XML contains schema-specific elements (CA, CB, CC, CE) within `<Binary_data>`. The parser traverses child nodes sequentially (not using `getElementsByTagName` which returns all matches document-wide) to ensure correct node pairing:

```
<Binary_data>
  ├── CA (or CB, CC, CE)
  │   ├── Vertices (base64, vertex_count, check_value)
  │   └── Facets (base64, facet_count, color)
  └── ... additional geometry nodes
```

**Key Algorithm**: Sequential child node traversal ensures vertices and facets from the same geometry node are processed together.

### Stage 3: Binary Data Processing Pipeline

For each geometry node, the following pipeline executes:

```
Base64-encoded string
    ↓
detail::DecodeBuffer() - Base64 decode to raw bytes
    ↓
(CE schema only) Decrypt with Blowfish:
    - Key derived from PackageLockList property via MD5
    - OpenSSL EVP API for Blowfish-CBC
    ↓
Verify CRC32 checksum
    - Compare computed checksum against check_value attribute
    - Fail if mismatch (data corruption detected)
    ↓
Interpret raw bytes:
```

### Stage 4: Vertex Data Interpretation

**Input**: Raw byte buffer (after base64 decode and optional decryption)
**Output**: `std::vector<float>` in x,y,z order (3 floats = 1 vertex, 12 bytes)

```cpp
// CA Schema: Direct float3 interpretation
for (size_t i = 0; i < vertexCount; ++i) {
    float x = *reinterpret_cast<const float*>(data + offset);
    float y = *reinterpret_cast<const float*>(data + offset + 4);
    float z = *reinterpret_cast<const float*>(data + offset + 8);
    m_Vertices.push_back(x);
    m_Vertices.push_back(y);
    m_Vertices.push_back(z);
    offset += 12;
}
```

### Stage 5: Facet (Triangle) Data Interpretation

**Input**: Raw byte buffer with compressed triangle indices
**Output**: `std::vector<Triangle>` with 0-based vertex indices

The facet data uses a compressed encoding:
- First byte: bitmask indicating which triangle vertices are new vs. reused
- Following bytes: delta-encoded vertex indices

```cpp
// Decode triangle from compressed format
uint8_t flagByte = *data++;
Triangle tri;

// Bit 0: vertex 0 is new (1) or reused (0)
if (flagByte & 0x01) tri.v0 = readDeltaEncodedInt(data);
else tri.v0 = previousVertex0;

// Bit 1: vertex 1 is new (2) or reused (0)  
if (flagByte & 0x02) tri.v1 = readDeltaEncodedInt(data);
else tri.v1 = previousVertex1;

// Bit 2: vertex 2 is new (4) or reused (0)
if (flagByte & 0x04) tri.v2 = readDeltaEncodedInt(data);
else tri.v2 = previousVertex2;
```

Delta encoding reads a variable-length integer:
- Low 7 bits: value
- High bit: continuation flag (1 = more bytes follow)
- Repeats until high bit is 0

### Stage 6: Color and Texture Data (v1.1.0+)

**Per-vertex colors**: Stored as RGB or RGBA in separate binary nodes
- Format: 3 or 4 bytes per vertex (0-255 range)
- Normalized to float [0,1] during processing

**UV coordinates**: Stored as float pairs per vertex
- Format: 2 floats per vertex (u, v)
- Stored in separate `<UV>` nodes

**Texture mapping**: Texture images and mapping data in `<Texture>` nodes

### Stage 7: Mesh Export

```
Parsed data:
  - m_Vertices: std::vector<float> (x,y,z ordered)
  - m_Triangles: std::vector<Triangle> (0-based indices)
  - m_Colors: std::vector<Color> (optional, per-vertex)
  - m_UVs: std::vector<UV> (optional, per-vertex)
    ↓
Build aiScene (Assimp data structure)
    ↓
aiExportSceneToFile() with selected format:
    - STL: Binary or ASCII triangle mesh
    - PLY: ASCII or binary with optional colors
    - OBJ: Wavefront format with UVs and materials
```

### CE Schema Decryption Algorithm

For encrypted CE schema files:

```
1. Extract PackageLockList property from XML
2. Derive decryption key:
   - MD5 hash of PackageLockList string
   - Truncate to 16 bytes for Blowfish key
3. Initialize Blowfish-CBC context with:
   - Key: MD5(PackageLockList)
   - IV: Zero-initialized (8 bytes)
4. Decrypt each binary buffer:
   - Input: base64-decoded bytes
   - Output: decrypted raw data
   - Padding: PKCS#7 (standard OpenSSL padding)
5. Verify CRC32 on decrypted data
```

---

## Building from Source

### Prerequisites

- **C++20 compatible compiler** (GCC 10+, Clang 12+, MSVC 2019+)
- **CMake** 3.16 or higher
- **Git** (for submodules)

The project uses vcpkg to manage dependencies automatically:
- Boost (program_options, dynamic_bitset)
- Poco (XML, Zip, Util)
- Assimp (mesh export)
- fmt (formatting)
- spdlog (logging)
- OpenSSL (CE schema decryption)

### Build Instructions

#### Linux / macOS

```bash
# Clone with submodules
git clone --recursive https://github.com/Nosenzor/Open3SDCM.git
cd Open3SDCM

# Configure with CMake (uses vcpkg preset)
cmake --preset ninja-release-vcpkg

# Build
cmake --build builds/ninja-release-vcpkg -j

# The executable will be in: builds/ninja-release-vcpkg/bin/Open3SDCMCLI
```

#### Windows

```bash
# Clone with submodules
git clone --recursive https://github.com/Nosenzor/Open3SDCM.git
cd Open3SDCM

# Configure with CMake
cmake --preset ninja-release-vcpkg

# Build
cmake --build builds/ninja-release-vcpkg --config Release

# The executable will be in: builds\ninja-release-vcpkg\bin\Open3SDCMCLI.exe
```

#### Debug Build

For development with debug symbols and tests:

```bash
# With tests enabled
cmake --preset ninja-release-vcpkg-tests
cmake --build builds/ninja-release-vcpkg-tests -j

# Run tests
ctest --preset ninja-release-vcpkg-tests --output-on-failure
```

---

## Usage

### Command Line Interface

The CLI tool `Open3SDCMCLI` can convert DCM files in two modes:

#### Single File Conversion

```bash
# Convert to STL
./Open3SDCMCLI -i input.dcm -o output_directory -f stl

# Convert to PLY (with colors if available)
./Open3SDCMCLI -i input.dcm -o output_directory -f ply

# Convert to OBJ (with UVs and textures if available)
./Open3SDCMCLI -i input.dcm -o output_directory -f obj
```

#### Batch Directory Conversion

```bash
# Convert all DCM files in a directory to STL
./Open3SDCMCLI -i input_directory -o output_directory -f stl

# Convert all DCM files to PLY
./Open3SDCMCLI -i input_directory -o output_directory -f ply
```

### Examples

```bash
# Example 1: Convert a single scan to STL
./Open3SDCMCLI -i TestData/Handle/HandleAngledLarge.dcm -o ./output -f stl

# Example 2: Convert all DCM files in a folder to PLY (preserves colors)
./Open3SDCMCLI -i TestData/Scan-01 -o ./output -f ply

# Example 3: Convert with full paths
./Open3SDCMCLI -i /path/to/scan.dcm -o /path/to/output -f obj

# Display help
./Open3SDCMCLI --help
```

### Command-line Options

| Option | Description |
|--------|-------------|
| `-i, --input <path>` | Input DCM file or directory containing DCM files (required) |
| `-o, --output_dir <path>` | Output directory for converted files (required) |
| `-f, --format <format>` | Output format: `stl`, `ply`, or `obj` (default: `stl`) |
| `-h, --help` | Display help message |

### Output

The tool creates a timestamped subdirectory in the output directory (e.g., `2026-04-25-14-30-45/`) containing the converted files. The output filename preserves the original DCM filename with the new extension.

---

## Technical Documentation

### DCM File Format

DCM files are ZIP archives containing HPS (Himsa Packed Scan) XML files. The internal format uses base64-encoded binary data for geometry.

**Key components:**
- **Vertices**: Base64-encoded binary data (floats, 12 bytes per vertex: 4 bytes each for x, y, z)
- **Facets**: Base64-encoded compressed triangle data (delta-encoded indices)
- **Schema**: Indicates encoding type (CA, CB, CC = unencrypted; CE = encrypted with Blowfish)
- **Properties**: Metadata including EKID, timestamps, etc.
- **Colors**: Per-vertex RGB/RGBA data (optional)
- **UV**: Texture coordinates (optional)
- **Textures**: Texture images and mapping data (optional)

**Example DCM structure:**

```xml
<HPS version="1.0">
  <Packed_geometry>
    <Schema>CE</Schema>
    <Properties>
      <Property name="EKID" value="1"/>
    </Properties>
    <Binary_data>
      <CE version="1.0">
        <Vertices vertex_count="376" base64_encoded_bytes="4512" check_value="330137282">
          ...base64 data...
        </Vertices>
        <Facets facet_count="748" base64_encoded_bytes="778" color="8421504">
          ...base64 data...
        </Facets>
      </CE>
    </Binary_data>
  </Packed_geometry>
  <FacetMarks>...</FacetMarks>
  <Annotations/>
  <Objects/>
  <Splines/>
</HPS>
```

### Format References

- [Packed Scan Standard Documentation](https://himsanoah.atlassian.net/wiki/spaces/AD/pages/1309803049/Packed+Scan+Standard)
- [Packed Scan Standard format 501 PDF](Packed%20Scan%20Standard%20format%20501.pdf)

### Useful Links

- [dcm2stl.appspot.com](https://dcm2stl.appspot.com/) - A web service that can convert DCM to STL for comparison

---

## Releases

### Creating a Release

#### Method 1: Create a Git Tag (Automated)

1. Update the version in the `VERSION` file
2. Commit your changes
3. Create and push the matching tag:

```bash
git tag v1.1.0
git push origin v1.1.0
```

The GitHub Actions "Build and Publish Release" workflow will automatically:
- Build binaries for Linux, macOS, and Windows
- Create a GitHub release
- Upload all assets

#### Method 2: Manual Workflow Dispatch

You can trigger a release build without creating a tag:

1. Go to the "Actions" tab on GitHub
2. Select the "Build and Publish Release" workflow
3. Click "Run workflow"
4. Choose the branch whose `VERSION` file you want to release
5. Click "Run workflow"

The workflow reads the version directly from the repository's `VERSION` file.

---

## Version Management

The project version is managed in the `VERSION` file at the repository root. This version is automatically read by CMake and used for all subprojects (CLI, Lib, TestTools).

**Current Version**: `1.1.0`

---

## Architecture

```
Open3SDCM/
├── Lib/              # Static library (Open3SDCMLib) - Core DCM parsing
│   └── src/
│       ├── ParseDcm.cpp    # Main parser implementation
│       ├── ParseDcm.h      # Parser interface
│       └── definitions.h   # Data structures (Triangle, Vertex, etc.)
├── CLI/              # Command-line executable (Open3SDCMCLI)
│   └── src/
│       └── main.cpp        # CLI entry point
├── TestTools/        # Test utilities
│   └── src/
│       └── RealWorldTest.cpp   # Regression tests with real DCM files
├── TestData/         # Sample DCM input files for testing
├── CMakeLists.txt    # Root CMake configuration
├── CMakePresets.json # Build presets
└── vcpkg.json        # Dependency manifest
```

### Namespace Convention

- **Public API**: `namespace Open3SDCM`
- **Internal implementation**: `namespace Open3SDCM::detail` (in .cpp files)

---

## Code Quality

- **Formatting**: LLVM clang-format style, 2-space indent, column limit 0 (no line length enforcement)
- **Static Analysis**: Clang-tidy with bugprone-, modernize-, and performance- checks
- **Sanitizers**: ASAN and UBSAN enabled in CI
- **Testing**: ctest with CMake presets

Run `clang-format` before committing to ensure consistent style.

---

## Contributing

1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Run `clang-format` on modified files
5. Build and test locally
6. Submit a pull request

All contributions are welcome: bug reports, feature requests, documentation improvements, and code contributions.

---

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
