// WebAssembly bindings for Open3SDCMLib (Emscripten embind).
//
// The library's native entry points work on files, so these bindings
// round-trip data through Emscripten's in-memory filesystem (MEMFS):
//  - parseBytes(): the input bytes are written to a MEMFS file which is
//    then parsed by the unmodified DCMParser, and removed afterwards.
//  - exportMeshBytes()/exportMeshTo(): the library's own STL/PLY/OBJ
//    writers write into MEMFS and the bytes are read back to JavaScript.
//
// Methods that return mesh data return typed-array VIEWS into WebAssembly
// memory (zero copy). A view stays valid only until the next call on this
// parser (or until the parser is deleted): JavaScript code that needs to
// keep the data must call .slice() on the view synchronously.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "ParseDcm.h"
#include "definitions.h"

namespace fs = std::filesystem;
namespace em = emscripten;

namespace
{
  // The library stores triangles as three size_t indices; on wasm32 a size_t
  // is a uint32, so the triangle buffer can be surfaced as a flat Uint32
  // view without any repacking.
  static_assert(sizeof(Open3SDCM::Triangle) == 3 * sizeof(std::size_t));
  static_assert(sizeof(std::size_t) == sizeof(std::uint32_t));

  constexpr const char* kInputPath = "/open3sdcm-input.dcm";
  constexpr const char* kOutputDir = "/open3sdcm-output";

  const Open3SDCM::TextureCoordinateData* FindDecodedCoordinates(const Open3SDCM::SurfaceData& surface)
  {
    for (const auto& candidate : surface.textureCoordinates)
    {
      if (candidate.HasDecodedCoordinates())
      {
        return &candidate;
      }
    }
    return nullptr;
  }

  const Open3SDCM::EmbeddedTextureImage* FindTextureImage(const Open3SDCM::SurfaceData& surface)
  {
    for (const auto& candidate : surface.textureImages)
    {
      if (!candidate.imageBytes.empty())
      {
        return &candidate;
      }
    }
    return nullptr;
  }

  std::string ExtensionFor(const std::string& format)
  {
    if (format == "stlb")
    {
      return ".stl";
    }
    return "." + format;
  }

  void ReadMemFile(const fs::path& path, std::string& buffer)
  {
    buffer.clear();
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (file == nullptr)
    {
      return;
    }
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::rewind(file);
    if (size > 0)
    {
      buffer.resize(static_cast<std::size_t>(size));
      const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), file);
      buffer.resize(read);
    }
    std::fclose(file);
  }

  // True when `data` is a typed-array view directly onto the WebAssembly
  // heap, i.e. when its byteOffset is a WebAssembly memory address.
  bool IsHeapView(const em::val& data)
  {
    return data["buffer"].strictlyEquals(em::val::module_property("wasmMemory")["buffer"]);
  }

  class DCMParserWasm
  {
  public:
    DCMParserWasm() = default;

    /// Parses a DCM archive held in memory.
    ///
    /// `data` accepts a Uint8Array (recommended: heap views and external
    /// ArrayBuffers are both copied at native speed), or a plain JS array
    /// of byte values.
    void parseBytes(const em::val& data)
    {
      parser_ = Open3SDCM::DCMParser{};
      WriteInputToMemfs(data);
      parser_.ParseDCM(kInputPath);
      std::error_code ec;
      fs::remove(kInputPath, ec);
    }

    bool hasMesh() const { return !parser_.m_Vertices.empty() && !parser_.m_Triangles.empty(); }

    int vertexCount() const { return static_cast<int>(parser_.m_Vertices.size() / 3); }

    int triangleCount() const { return static_cast<int>(parser_.m_Triangles.size()); }

    /// Flat (N, 3) float32 vertex positions. View; call .slice() to keep.
    em::val vertices()
    {
      return em::val(em::typed_memory_view(parser_.m_Vertices.size(), parser_.m_Vertices.data()));
    }

    /// Flat (M, 3) uint32 triangle indices, 0-based. View; call .slice() to keep.
    em::val triangles()
    {
      return em::val(em::typed_memory_view(parser_.m_Triangles.size() * 3,
                                        reinterpret_cast<const std::uint32_t*>(parser_.m_Triangles.data())));
    }

    /// Mesh-wide base colour as 0xRRGGBB, or -1 when the scan carries none.
    int baseColor() const
    {
      const auto& color = parser_.m_SurfaceData.baseColor;
      return color.has_value() ? static_cast<int>(color->PackedRGB()) : -1;
    }

    /// Raw bytes of the first embedded texture image (typically JPEG),
    /// empty when the scan has none. View; call .slice() to keep.
    em::val textureImage()
    {
      readBuffer_.clear();
      if (const auto* image = FindTextureImage(parser_.m_SurfaceData))
      {
        readBuffer_.assign(image->imageBytes.begin(), image->imageBytes.end());
      }
      return ByteView(readBuffer_);
    }

    /// Per-corner UV pairs (u0, v0, u1, v1, ...); NaN marks a corner whose
    /// coordinate could not be decoded. Empty when the scan has none.
    /// View; call .slice() to keep.
    em::val uv()
    {
      uvBuffer_.clear();
      if (const auto* coords = FindDecodedCoordinates(parser_.m_SurfaceData))
      {
        uvBuffer_.reserve(coords->cornerCoordinates.size() * 2);
        for (const auto& corner : coords->cornerCoordinates)
        {
          uvBuffer_.push_back(corner.has_value() ? corner->u : std::numeric_limits<float>::quiet_NaN());
          uvBuffer_.push_back(corner.has_value() ? corner->v : std::numeric_limits<float>::quiet_NaN());
        }
      }
      return em::val(em::typed_memory_view(uvBuffer_.size(), uvBuffer_.data()));
    }

    /// Writes the decoded mesh with the library's own writers into the
    /// virtual filesystem and returns the bytes of the main file.
    ///
    /// STL/PLY produce a single file. For OBJ only the `.obj` bytes are
    /// returned; the companion `.mtl` and texture image stay in the output
    /// directory and can be fetched with readFile(). An empty view is
    /// returned on failure or when the mesh is empty.
    em::val exportMeshBytes(const std::string& format, const std::string& baseName)
    {
      readBuffer_.clear();
      if (hasMesh())
      {
        std::error_code ec;
        fs::create_directories(kOutputDir, ec);
        const fs::path out = fs::path(kOutputDir) / (baseName + ExtensionFor(format));
        if (parser_.ExportMesh(out, format))
        {
          ReadMemFile(out, readBuffer_);
        }
      }
      return ByteView(readBuffer_);
    }

    /// Lower-level variant of exportMeshBytes(): writes the mesh to any
    /// path of the virtual filesystem (e.g. an IDBFS mount set up via the
    /// Emscripten FS API). Returns the library's success flag.
    bool exportMeshTo(const std::string& virtualPath, const std::string& format)
    {
      return parser_.ExportMesh(virtualPath, format);
    }

    /// Reads any file from the virtual filesystem (e.g. `.mtl` companions
    /// written by exportMeshTo()). View; call .slice() to keep.
    em::val readFile(const std::string& virtualPath)
    {
      readBuffer_.clear();
      ReadMemFile(virtualPath, readBuffer_);
      return ByteView(readBuffer_);
    }

  private:
    static em::val ByteView(const std::string& buffer)
    {
      return em::val(em::typed_memory_view(buffer.size(),
                                        reinterpret_cast<const std::uint8_t*>(buffer.data())));
    }

    void WriteInputToMemfs(const em::val& data)
    {
      const em::val length = data["length"];
      const std::size_t size = length.isUndefined() ? 0 : length.as<std::size_t>();

      std::FILE* file = std::fopen(kInputPath, "wb");
      if (file == nullptr || size == 0)
      {
        if (file != nullptr)
        {
          std::fclose(file);
        }
        return;
      }

      const bool isTypedArray = !data["byteOffset"].isUndefined();
      if (isTypedArray && IsHeapView(data))
      {
        // Heap view: the byteOffset is a WebAssembly address, copy directly.
        const auto* src = reinterpret_cast<const std::uint8_t*>(data["byteOffset"].as<std::uint32_t>());
        std::fwrite(src, 1, size, file);
      }
      else if (isTypedArray)
      {
        // External ArrayBuffer: have JS copy it into the WebAssembly heap
        // with Uint8Array.prototype.set (native-speed memcpy). HEAPU8 is
        // fetched after malloc, which may have grown the heap.
        void* const dst = std::malloc(size);
        em::val::module_property("HEAPU8")
            .call<void>("set", data, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(dst)));
        std::fwrite(dst, 1, size, file);
        std::free(dst);
      }
      else
      {
        // Plain JS array: element-wise fallback.
        std::vector<std::uint8_t> bytes(size);
        for (std::size_t i = 0; i < size; ++i)
        {
          bytes[i] = data[i].as<std::uint8_t>();
        }
        std::fwrite(bytes.data(), 1, size, file);
      }
      std::fclose(file);
    }

    Open3SDCM::DCMParser parser_;
    // Reused scratch buffers so the returned views stay valid until the next
    // call on this parser.
    std::string readBuffer_;
    std::vector<float> uvBuffer_;
  };
}// namespace

EMSCRIPTEN_BINDINGS(open3sdcm)
{
  em::class_<DCMParserWasm>("DCMParser")
      .constructor<>()
      .function("parseBytes", &DCMParserWasm::parseBytes)
      .function("hasMesh", &DCMParserWasm::hasMesh)
      .function("vertexCount", &DCMParserWasm::vertexCount)
      .function("triangleCount", &DCMParserWasm::triangleCount)
      .function("vertices", &DCMParserWasm::vertices)
      .function("triangles", &DCMParserWasm::triangles)
      .function("baseColor", &DCMParserWasm::baseColor)
      .function("textureImage", &DCMParserWasm::textureImage)
      .function("uv", &DCMParserWasm::uv)
      .function("exportMeshBytes", &DCMParserWasm::exportMeshBytes)
      .function("exportMeshTo", &DCMParserWasm::exportMeshTo)
      .function("readFile", &DCMParserWasm::readFile);
}
