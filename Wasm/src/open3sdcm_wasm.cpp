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
// memory. A view stays valid only until the next call into the module:
// besides the next call on this parser, any allocation (another parser,
// another export) may grow the WebAssembly memory, which detaches every
// outstanding view. JavaScript code that needs to keep the data must call
// .slice() on the view synchronously.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
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

  using FilePtr = std::unique_ptr<std::FILE, decltype(&std::fclose)>;

  // Removes the MEMFS input file when a parse ends, including by an
  // exception, so a (possibly large) scan never stays pinned in memory.
  struct RemoveInputOnExit
  {
    RemoveInputOnExit() = default;
    RemoveInputOnExit(const RemoveInputOnExit&) = delete;
    RemoveInputOnExit& operator=(const RemoveInputOnExit&) = delete;
    ~RemoveInputOnExit()
    {
      std::error_code ec;
      fs::remove(kInputPath, ec);
    }
  };

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
    const FilePtr file{std::fopen(path.c_str(), "rb"), &std::fclose};
    if (!file)
    {
      return;
    }
    std::fseek(file.get(), 0, SEEK_END);
    const long size = std::ftell(file.get());
    std::rewind(file.get());
    if (size > 0)
    {
      buffer.resize(static_cast<std::size_t>(size));
      const std::size_t read = std::fread(buffer.data(), 1, buffer.size(), file.get());
      buffer.resize(read);
    }
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

    /// Parses a DCM file held in memory.
    ///
    /// `data` accepts a Uint8Array (recommended), any other ArrayBuffer view
    /// (its raw bytes are used), a raw ArrayBuffer, or a plain JS array of
    /// byte values. Anything else is treated as empty input. A file that
    /// cannot be parsed does not throw: hasMesh() is false afterwards and the
    /// library logs the reason to the console.
    void parseBytes(const em::val& data)
    {
      // binding_ points into the surface data that is about to be replaced.
      binding_ = {};
      parser_ = Open3SDCM::DCMParser{};
      const RemoveInputOnExit removeInput;
      WriteInputToMemfs(data);
      parser_.ParseDCM(kInputPath);
      binding_ = Open3SDCM::FindTextureBinding(parser_.m_SurfaceData);
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

    /// Raw bytes of the embedded texture image (typically JPEG) that the
    /// library's OBJ writer exports: image and uv() come from the same
    /// Open3SDCM::FindTextureBinding() selection. Empty when the scan has
    /// none. View; call .slice() to keep.
    em::val textureImage()
    {
      if (binding_.image == nullptr)
      {
        return em::val(em::typed_memory_view(0, static_cast<const std::uint8_t*>(nullptr)));
      }
      const auto& bytes = binding_.image->imageBytes;
      return em::val(em::typed_memory_view(bytes.size(), bytes.data()));
    }

    /// Per-corner UV pairs (u0, v0, u1, v1, ...) of the UV set paired with
    /// textureImage(); NaN marks a corner whose coordinate could not be
    /// decoded. Empty when the scan has none, or when the set does not have
    /// exactly one entry per triangle corner (the OBJ writer then writes no
    /// UVs either). View; call .slice() to keep.
    em::val uv()
    {
      uvBuffer_.clear();
      if (binding_.coordinates != nullptr &&
          binding_.coordinates->cornerCoordinates.size() == parser_.m_Triangles.size() * 3)
      {
        uvBuffer_.reserve(binding_.coordinates->cornerCoordinates.size() * 2);
        for (const auto& corner : binding_.coordinates->cornerCoordinates)
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
    /// Each call first empties the output directory (/open3sdcm-output), so
    /// it only ever holds the files of the latest export. STL/PLY produce a
    /// single file. For OBJ only the `.obj` bytes are returned; the companion
    /// `.mtl` and texture image are written next to it and can be fetched
    /// with readFile(). An empty view is returned on failure or when the mesh
    /// is empty.
    em::val exportMeshBytes(const std::string& format, const std::string& baseName)
    {
      readBuffer_.clear();
      std::error_code ec;
      fs::remove_all(kOutputDir, ec);
      if (hasMesh())
      {
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
    /// path of the virtual filesystem (the directory must exist, e.g.
    /// created with the FS runtime method). Returns the library's success
    /// flag.
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

    // Writes the input to kInputPath (an empty file for empty or unsupported
    // input, which the parser then reports as having no mesh).
    //
    // All inspection of the JS value happens before the file is opened: a JS
    // exception (e.g. from a getter) bypasses C++ destructors, so it must not
    // be able to fire while the FILE* is open.
    void WriteInputToMemfs(const em::val& data)
    {
      const em::val uint8Array = em::val::global("Uint8Array");
      std::vector<std::uint8_t> staged;
      em::val bytes = em::val::undefined();  // Uint8Array over the raw input bytes
      bool onHeap = false;

      // (val::isTrue() is `=== true`, not truthiness, so test types instead.)
      const auto isObject = [](const em::val& value)
      { return !value.isNull() && value.typeOf().as<std::string>() == "object"; };
      const auto isNumber = [](const em::val& value) { return value.typeOf().as<std::string>() == "number"; };

      if (isObject(data) && isObject(data["buffer"]) && isNumber(data["byteOffset"]))
      {
        // Any ArrayBuffer view (Uint8Array, other typed arrays, DataView):
        // take its raw bytes, whatever the element type.
        bytes = uint8Array.new_(data["buffer"], data["byteOffset"], data["byteLength"]);
        onHeap = IsHeapView(data);
      }
      else if (isObject(data) && isNumber(data["byteLength"]))
      {
        // Raw ArrayBuffer (fetch().arrayBuffer() etc.).
        bytes = uint8Array.new_(data);
      }
      else if (isObject(data) && isNumber(data["length"]))
      {
        // Plain JS array of byte values: element-wise fallback.
        const auto size = data["length"].as<std::size_t>();
        staged.resize(size);
        for (std::size_t i = 0; i < size; ++i)
        {
          staged[i] = data[i].as<std::uint8_t>();
        }
      }

      const std::size_t byteSize = bytes.isUndefined() ? staged.size() : bytes["byteLength"].as<std::size_t>();
      const std::uint8_t* src = staged.data();
      std::unique_ptr<void, decltype(&std::free)> heapCopy{nullptr, &std::free};
      if (!bytes.isUndefined() && byteSize > 0)
      {
        if (onHeap)
        {
          // Heap view: its byteOffset is a WebAssembly address.
          src = reinterpret_cast<const std::uint8_t*>(bytes["byteOffset"].as<std::uint32_t>());
        }
        else
        {
          // External buffer: have JS copy it into the WebAssembly heap with
          // Uint8Array.prototype.set (native-speed memcpy). HEAPU8 is fetched
          // after malloc, which may have grown the heap.
          heapCopy.reset(std::malloc(byteSize));
          if (!heapCopy)
          {
            throw std::bad_alloc();
          }
          em::val::module_property("HEAPU8")
              .call<void>("set", bytes, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(heapCopy.get())));
          src = static_cast<const std::uint8_t*>(heapCopy.get());
        }
      }

      const FilePtr file{std::fopen(kInputPath, "wb"), &std::fclose};
      if (file && byteSize > 0)
      {
        std::fwrite(src, 1, byteSize, file.get());
      }
    }

    Open3SDCM::DCMParser parser_;
    // Texture image + UV set selected by the library (computed once per
    // parse), so textureImage() and uv() always describe the same texture.
    Open3SDCM::TextureBinding binding_;
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
