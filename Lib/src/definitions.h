#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Open3SDCM
{
  struct Vertex
  {
    union
    {
      std::array<float, 3> data;
      struct
      {
        float x;
        float y;
        float z;
      };
    };

    // Constructors for convenience
    Vertex() : x(0.0f), y(0.0f), z(0.0f) {}
    Vertex(float x, float y, float z) : x(x), y(y), z(z) {}

    // Array-like access
    float& operator[](int index) { return data[index]; }
    const float& operator[](int index) const { return data[index]; }
  };

  struct Triangle
  {
    size_t v1{0};
    size_t v2{0};
    size_t v3{0};
  };

  struct ColorRGB
  {
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};

    [[nodiscard]] std::uint32_t PackedRGB() const
    {
      return (static_cast<std::uint32_t>(r) << 16U) |
             (static_cast<std::uint32_t>(g) << 8U) |
             static_cast<std::uint32_t>(b);
    }
  };

  struct TextureCoordinate
  {
    float u{0.0F};
    float v{0.0F};
  };

  struct TextureCoordinateData
  {
    std::optional<std::string> textureCoordId;
    std::optional<std::string> textureId;
    std::optional<std::string> key;
    std::size_t encodedByteCount{0};
    std::vector<std::optional<TextureCoordinate>> cornerCoordinates;

    [[nodiscard]] bool HasDecodedCoordinates() const
    {
      return !cornerCoordinates.empty();
    }
  };

  struct EmbeddedTextureImage
  {
    std::optional<std::string> id;
    std::optional<std::string> textureId;
    std::optional<std::string> refTextureCoordId;
    std::optional<std::string> textureCoordSet;
    std::optional<std::string> textureName;
    std::optional<std::string> version;
    std::optional<std::string> mimeType;
    std::size_t width{0};
    std::size_t height{0};
    std::size_t bytesPerPixel{0};
    std::size_t encodedByteCount{0};
    std::vector<std::uint8_t> imageBytes;
  };

  struct SurfaceData
  {
    std::optional<ColorRGB> baseColor;
    std::vector<TextureCoordinateData> textureCoordinates;
    std::vector<EmbeddedTextureImage> textureImages;

    [[nodiscard]] bool HasData() const
    {
      return baseColor.has_value() || !textureCoordinates.empty() || !textureImages.empty();
    }
  };
}// namespace Open3SDCM
