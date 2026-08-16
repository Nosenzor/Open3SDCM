#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

/// Core public types and data structures of the Open3SDCM library.
///
/// A DCM file is a ZIP archive holding an HPS (Himsa Packed Scan) XML file
/// whose `<Binary_data>` nodes contain base64-encoded geometry. The structs in
/// this namespace model the decoded result: mesh geometry (vertices, triangles)
/// and the optional surface data (base colour, texture coordinates, embedded
/// texture images). See `DCMParser` for the entry point that populates them.
namespace Open3SDCM
{
  /// A single 3D vertex stored as single-precision coordinates.
  ///
  /// The members `x`, `y`, `z` and the `data` array are unioned together so the
  /// same bytes can be addressed either by name or by index. Vertex positions
  /// are exported from the DCM as a flat `std::vector<float>` (x, y, z ordered)
  /// rather than as a vector of `Vertex`; this struct is provided for callers
  /// that want named access.
  struct Vertex
  {
    union
    {
      /// Flat array view of the same memory as `x`, `y`, `z`.
      std::array<float, 3> data;
      struct
      {
        float x; ///< X coordinate.
        float y; ///< Y coordinate.
        float z; ///< Z coordinate.
      };
    };

    /// Zero-initialises the vertex at the origin.
    Vertex() : x(0.0f), y(0.0f), z(0.0f) {}

    /// Builds a vertex from explicit coordinates.
    Vertex(float x, float y, float z) : x(x), y(y), z(z) {}

    /// Array-like access to the coordinate at @p index (0, 1 or 2).
    float& operator[](int index) { return data[index]; }
    /// Read-only array-like access to the coordinate at @p index (0, 1 or 2).
    const float& operator[](int index) const { return data[index]; }
  };

  /// A triangle defined by three 0-based indices into the vertex buffer.
  ///
  /// DCM facet data is delta-encoded and compressed: a per-triangle flag byte
  /// marks which corners are new versus reused from the previous triangle.
  /// After decoding, indices are normalised to 0-based references into the
  /// `m_Vertices` flat float buffer (three floats per vertex).
  struct Triangle
  {
    size_t v1{0}; ///< Index of the first corner.
    size_t v2{0}; ///< Index of the second corner.
    size_t v3{0}; ///< Index of the third corner.
  };

  /// An RGB colour with 8-bit-per-channel components (0-255).
  ///
  /// DCM stores a single `color` attribute on `<Facets>` for the entire mesh;
  /// it is very often the placeholder `(128, 128, 128)`. Scans that hold real
  /// colour do so through an embedded texture (see `EmbeddedTextureImage`).
  struct ColorRGB
  {
    std::uint8_t r{0}; ///< Red component.
    std::uint8_t g{0}; ///< Green component.
    std::uint8_t b{0}; ///< Blue component.

    /// Packs the three channels into a single 0xRRGGBB integer.
    [[nodiscard]] std::uint32_t PackedRGB() const
    {
      return (static_cast<std::uint32_t>(r) << 16U) |
             (static_cast<std::uint32_t>(g) << 8U) |
             static_cast<std::uint32_t>(b);
    }
  };

  /// A single texture coordinate (UV pair).
  ///
  /// In the decoded mesh, UVs are stored per triangle corner (matching
  /// `Triangle` indices, not per unique vertex): a vertex shared by several
  /// triangles can carry a different coordinate in each.
  struct TextureCoordinate
  {
    float u{0.0F}; ///< Horizontal texture coordinate.
    float v{0.0F}; ///< Vertical texture coordinate.
  };

  /// One decoded `<UV>` node: texture-coordinate data for a set of corners.
  ///
  /// @p cornerCoordinates holds one (possibly null) entry per triangle corner;
  /// a null entry means that corner's coordinate could not be decoded and is
  /// represented as NaN when surfaced to consumers (e.g. the Python bindings).
  struct TextureCoordinateData
  {
    std::optional<std::string> textureCoordId;    ///< Identifier of this UV set.
    std::optional<std::string> textureId;         ///< Texture this coordinate set maps onto.
    std::optional<std::string> key;               ///< Optional lookup key from the XML.
    std::size_t encodedByteCount{0};              ///< Number of raw bytes before decoding.
    /// Per-corner coordinates, one entry per triangle corner (may be `nullopt`).
    std::vector<std::optional<TextureCoordinate>> cornerCoordinates;

    /// Returns true when at least one corner coordinate was decoded.
    [[nodiscard]] bool HasDecodedCoordinates() const
    {
      return !cornerCoordinates.empty();
    }
  };

  /// An embedded texture image extracted from a `<Texture>` node.
  ///
  /// The DCM embeds texture images directly; `imageBytes` holds the raw decoded
  /// byte buffer (typically JPEG-encoded content, as exposed by the Python
  /// bindings via `mesh.texture`). Width, height and pixel layout describe how
  /// to interpret that buffer.
  struct EmbeddedTextureImage
  {
    std::optional<std::string> id;                 ///< Identifier of this image.
    std::optional<std::string> textureId;          ///< Texture this image belongs to.
    std::optional<std::string> refTextureCoordId;  ///< UV set referenced by this image.
    std::optional<std::string> textureCoordSet;    ///< Optional coordinate-set selector.
    std::optional<std::string> textureName;        ///< Human-readable texture name.
    std::optional<std::string> version;            ///< Format/version of the embedded data.
    std::optional<std::string> mimeType;           ///< MIME type of the embedded image data.
    std::size_t width{0};             ///< Image width in pixels.
    std::size_t height{0};            ///< Image height in pixels.
    std::size_t bytesPerPixel{0};     ///< Number of bytes per pixel.
    std::size_t encodedByteCount{0};  ///< Number of raw bytes before decoding.
    std::vector<std::uint8_t> imageBytes;  ///< Raw decoded image byte buffer.
  };

  /// All non-geometry surface information attached to a parsed mesh.
  ///
  /// This aggregates the optional colour and texture data that `DCMParser`
  /// extracts alongside vertices and triangles. `ExportMesh()` consults it to
  /// decide what each writer can emit (e.g. PLY writes `baseColor` as a
  /// per-vertex tint, OBJ writes the embedded texture image + UVs).
  struct SurfaceData
  {
    std::optional<ColorRGB> baseColor;                       ///< Mesh-wide tint from `<Facets>`.
    std::vector<TextureCoordinateData> textureCoordinates;   ///< Decoded UV sets.
    std::vector<EmbeddedTextureImage> textureImages;         ///< Embedded texture images.

    /// Returns true when any colour, UV or texture data is present.
    [[nodiscard]] bool HasData() const
    {
      return baseColor.has_value() || !textureCoordinates.empty() || !textureImages.empty();
    }
  };
}// namespace Open3SDCM
