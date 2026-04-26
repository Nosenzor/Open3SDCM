// Real-world scan batch conversion test.
// Parses each DCM in TestData/real-world/ and exports to PLY, verifying:
//   - exact vertex/face counts (regression guard for decryption + facet decoding)
//   - geometry integrity (all vertex floats finite, all indices in range)
//   - surface metadata + decoded UVs for textured CE samples
//   - successful PLY/OBJ export with preserved color/texture artifacts where supported

#define BOOST_TEST_MODULE RealWorldConversionTest
#include <boost/test/included/unit_test.hpp>

#include "ParseDcm.h"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace fs = std::filesystem;

#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

struct ScanSpec
{
  const char* filename;
  std::size_t expectedVertices;
  std::size_t expectedFaces;
  std::uint32_t expectedPackedColor;
  bool hasTextureData;
  bool verifyObjExport;
  bool expectUvSeams;
};

static constexpr ScanSpec k_Scans[] = {
  {"scan_040.dcm",  50648, 101292, 8421504U, false, false, false},
  {"scan_039.dcm",  68698, 136995, 8421504U, false, false, false},
  {"scan_012.dcm",  60117, 120230, 8421504U, true,  true,  true },
  {"scan_019.dcm",  95497, 190206, 8421504U, true,  false, true },
  {"scan_045.dcm",  99619, 198513, 8421504U, true,  false, true },
};

struct TempOutputDir
{
  fs::path path;

  explicit TempOutputDir(const char* scanName)
    : path(fs::temp_directory_path() / "Open3SDCM_real_world_test" / scanName)
  {
    fs::remove_all(path);
    fs::create_directories(path);
  }

  ~TempOutputDir()
  {
    std::error_code ec;
    fs::remove_all(path, ec);
  }
};

static bool allVerticesFinite(const std::vector<float>& vertices)
{
  for (float v : vertices)
  {
    if (!std::isfinite(v))
      return false;
  }
  return true;
}

static bool allIndicesInRange(const std::vector<Open3SDCM::Triangle>& triangles,
                              std::size_t vertexCount)
{
  for (const auto& tri : triangles)
  {
    if (tri.v1 >= vertexCount || tri.v2 >= vertexCount || tri.v3 >= vertexCount)
      return false;
  }
  return true;
}

static std::string readTextFile(const fs::path& path)
{
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

static std::size_t countLinesWithPrefix(const std::string& text, const std::string_view prefix)
{
  std::size_t count = 0;
  std::size_t lineStart = 0;
  while (lineStart < text.size())
  {
    const std::size_t lineEnd = text.find('\n', lineStart);
    const std::string_view line(text.data() + lineStart,
      (lineEnd == std::string::npos ? text.size() : lineEnd) - lineStart);
    if (line.starts_with(prefix))
    {
      ++count;
    }
    if (lineEnd == std::string::npos)
    {
      break;
    }
    lineStart = lineEnd + 1;
  }
  return count;
}

static std::size_t countDecodedUvCorners(const Open3SDCM::TextureCoordinateData& textureCoordinateData)
{
  std::size_t count = 0;
  for (const auto& coordinate : textureCoordinateData.cornerCoordinates)
  {
    if (coordinate.has_value())
    {
      ++count;
    }
  }
  return count;
}

static std::vector<Open3SDCM::TextureCoordinate> parseObjTextureCoordinates(const std::string& objText)
{
  std::vector<Open3SDCM::TextureCoordinate> coordinates;
  std::istringstream input(objText);
  std::string line;
  while (std::getline(input, line))
  {
    if (!line.starts_with("vt "))
    {
      continue;
    }

    std::istringstream lineInput(line.substr(3));
    Open3SDCM::TextureCoordinate coordinate;
    if (lineInput >> coordinate.u >> coordinate.v)
    {
      coordinates.push_back(coordinate);
    }
  }
  return coordinates;
}

static bool textureCoordinatesNearlyEqual(const Open3SDCM::TextureCoordinate& lhs,
                                          const Open3SDCM::TextureCoordinate& rhs,
                                          float tolerance = 1.0e-5F)
{
  return std::fabs(lhs.u - rhs.u) <= tolerance && std::fabs(lhs.v - rhs.v) <= tolerance;
}

static bool allTextureCoordinatesFinite(const Open3SDCM::TextureCoordinateData& textureCoordinateData)
{
  for (const auto& coordinate : textureCoordinateData.cornerCoordinates)
  {
    if (!coordinate.has_value())
    {
      continue;
    }
    if (!std::isfinite(coordinate->u) || !std::isfinite(coordinate->v))
    {
      return false;
    }
  }
  return true;
}

static bool hasVertexUvSeam(const std::vector<Open3SDCM::Triangle>& triangles,
                            const Open3SDCM::TextureCoordinateData& textureCoordinateData,
                            std::size_t vertexCount)
{
  std::vector<std::optional<Open3SDCM::TextureCoordinate>> firstCoordinateByVertex(vertexCount);

  const auto differs = [](const Open3SDCM::TextureCoordinate& lhs, const Open3SDCM::TextureCoordinate& rhs) -> bool
  {
    return std::fabs(lhs.u - rhs.u) > 1.0e-6F || std::fabs(lhs.v - rhs.v) > 1.0e-6F;
  };

  for (std::size_t faceIndex = 0; faceIndex < triangles.size(); ++faceIndex)
  {
    const std::array<std::size_t, 3> vertices = {triangles[faceIndex].v1, triangles[faceIndex].v2, triangles[faceIndex].v3};
    for (std::size_t cornerIndex = 0; cornerIndex < vertices.size(); ++cornerIndex)
    {
      const auto& coordinate = textureCoordinateData.cornerCoordinates[faceIndex * 3 + cornerIndex];
      if (!coordinate.has_value())
      {
        continue;
      }

      auto& firstCoordinate = firstCoordinateByVertex[vertices[cornerIndex]];
      if (!firstCoordinate.has_value())
      {
        firstCoordinate = coordinate;
        continue;
      }

      if (differs(*firstCoordinate, *coordinate))
      {
        return true;
      }
    }
  }

  return false;
}

static void runConversionTest(const ScanSpec& spec)
{
  const fs::path dcm = fs::path(TEST_DATA_DIR) / "real-world" / spec.filename;

  BOOST_TEST_MESSAGE("DCM: " << dcm.string());
  BOOST_REQUIRE_MESSAGE(fs::exists(dcm), "DCM file not found: " << dcm.string());

  Open3SDCM::DCMParser parser;
  parser.ParseDCM(dcm);

  BOOST_REQUIRE_GT(parser.m_Vertices.size(), 0u);
  BOOST_REQUIRE_GT(parser.m_Triangles.size(), 0u);

  BOOST_CHECK_EQUAL(parser.m_Vertices.size() / 3, spec.expectedVertices);
  BOOST_CHECK_EQUAL(parser.m_Triangles.size(),     spec.expectedFaces);

  const std::size_t vertexCount = parser.m_Vertices.size() / 3;
  BOOST_CHECK_MESSAGE(allVerticesFinite(parser.m_Vertices),
    "Non-finite vertex coordinate detected in " << spec.filename);
  BOOST_CHECK_MESSAGE(allIndicesInRange(parser.m_Triangles, vertexCount),
    "Out-of-range triangle index detected in " << spec.filename);

  BOOST_REQUIRE_MESSAGE(parser.m_SurfaceData.baseColor.has_value(),
    "Missing mesh-level base color in " << spec.filename);
  BOOST_CHECK_EQUAL(parser.m_SurfaceData.baseColor->PackedRGB(), spec.expectedPackedColor);

  if (spec.hasTextureData)
  {
    BOOST_REQUIRE_EQUAL(parser.m_SurfaceData.textureCoordinates.size(), 1u);
    BOOST_REQUIRE_EQUAL(parser.m_SurfaceData.textureImages.size(), 1u);

    const auto& textureCoordinates = parser.m_SurfaceData.textureCoordinates.front();
    const auto& textureImage = parser.m_SurfaceData.textureImages.front();

    BOOST_CHECK_EQUAL(textureCoordinates.textureCoordId.value_or(""), "0");
    BOOST_CHECK_EQUAL(textureImage.refTextureCoordId.value_or(""), "0");
    BOOST_CHECK_EQUAL(textureImage.id.value_or(""), "0");
    BOOST_CHECK(!textureImage.imageBytes.empty());

    BOOST_CHECK(textureCoordinates.HasDecodedCoordinates());
    BOOST_REQUIRE_EQUAL(textureCoordinates.cornerCoordinates.size(), parser.m_Triangles.size() * 3u);
    BOOST_CHECK_GT(countDecodedUvCorners(textureCoordinates), 0u);
    BOOST_CHECK(allTextureCoordinatesFinite(textureCoordinates));
    if (spec.expectUvSeams)
    {
      BOOST_CHECK_MESSAGE(hasVertexUvSeam(parser.m_Triangles, textureCoordinates, vertexCount),
        "Expected to observe at least one vertex/corner UV split in " << spec.filename);
    }
  }
  else
  {
    BOOST_CHECK(parser.m_SurfaceData.textureCoordinates.empty());
    BOOST_CHECK(parser.m_SurfaceData.textureImages.empty());
  }

  TempOutputDir tmp(spec.filename);
  const fs::path stem = fs::path(spec.filename).stem();
  const fs::path ply = tmp.path / (stem.string() + ".ply");

  BOOST_CHECK_MESSAGE(parser.ExportMesh(ply, "ply"),
    "ExportMesh failed for " << spec.filename);
  BOOST_REQUIRE(fs::exists(ply));
  BOOST_CHECK_GT(fs::file_size(ply), 0u);

  const std::string plyText = readTextFile(ply);
  BOOST_CHECK_NE(plyText.find("property uchar red"), std::string::npos);
  BOOST_CHECK_NE(plyText.find("property uchar green"), std::string::npos);
  BOOST_CHECK_NE(plyText.find("property uchar blue"), std::string::npos);

  BOOST_TEST_MESSAGE("PLY: " << ply.string()
    << "  (" << (fs::file_size(ply) / 1024) << " KB)");

  if (spec.verifyObjExport)
  {
    const fs::path obj = tmp.path / (stem.string() + ".obj");
    const fs::path mtl = tmp.path / (stem.string() + ".mtl");
    const fs::path texture = tmp.path / (stem.string() + "_texture0.jpg");

    BOOST_CHECK_MESSAGE(parser.ExportMesh(obj, "obj"),
      "OBJ export failed for " << spec.filename);
    BOOST_REQUIRE(fs::exists(obj));
    BOOST_REQUIRE(fs::exists(mtl));
    BOOST_REQUIRE(fs::exists(texture));
    BOOST_CHECK_GT(fs::file_size(obj), 0u);
    BOOST_CHECK_GT(fs::file_size(mtl), 0u);
    BOOST_CHECK_EQUAL(fs::file_size(texture), parser.m_SurfaceData.textureImages.front().imageBytes.size());

    const std::string objText = readTextFile(obj);
    const std::string mtlText = readTextFile(mtl);
    BOOST_CHECK_NE(objText.find("mtllib "), std::string::npos);
    BOOST_CHECK_NE(objText.find("usemtl material0"), std::string::npos);
    BOOST_CHECK_EQUAL(countLinesWithPrefix(objText, "vt "), parser.m_Triangles.size() * 3u);
    BOOST_CHECK_EQUAL(countLinesWithPrefix(objText, "f "), parser.m_Triangles.size());
    BOOST_CHECK_NE(mtlText.find("map_Kd " + texture.filename().string()), std::string::npos);

    const auto exportedCoordinates = parseObjTextureCoordinates(objText);
    BOOST_REQUIRE_EQUAL(exportedCoordinates.size(), parser.m_Triangles.size() * 3u);
    const auto& decodedCoordinates = parser.m_SurfaceData.textureCoordinates.front().cornerCoordinates;
    BOOST_REQUIRE_EQUAL(decodedCoordinates.size(), exportedCoordinates.size());

    for (std::size_t index = 0; index < decodedCoordinates.size(); ++index)
    {
      const auto& decodedCoordinate = decodedCoordinates[index];
      BOOST_REQUIRE_MESSAGE(decodedCoordinate.has_value(),
        "Expected decoded UV for exported textured OBJ corner " << index << " in " << spec.filename);

      const Open3SDCM::TextureCoordinate expectedCoordinate{decodedCoordinate->u, 1.0F - decodedCoordinate->v};
      BOOST_CHECK_MESSAGE(textureCoordinatesNearlyEqual(exportedCoordinates[index], expectedCoordinate),
        "Unexpected exported OBJ UV at corner " << index << " in " << spec.filename
        << " (decoded: " << decodedCoordinate->u << ", " << decodedCoordinate->v
        << " exported: " << exportedCoordinates[index].u << ", " << exportedCoordinates[index].v
        << " expected OBJ: " << expectedCoordinate.u << ", " << expectedCoordinate.v << ")");
    }
  }
}

BOOST_AUTO_TEST_SUITE(RealWorldConversion)

BOOST_AUTO_TEST_CASE(ConvertScan040) { runConversionTest(k_Scans[0]); }
BOOST_AUTO_TEST_CASE(ConvertScan039) { runConversionTest(k_Scans[1]); }
BOOST_AUTO_TEST_CASE(ConvertScan012) { runConversionTest(k_Scans[2]); }
BOOST_AUTO_TEST_CASE(ConvertScan019) { runConversionTest(k_Scans[3]); }
BOOST_AUTO_TEST_CASE(ConvertScan045) { runConversionTest(k_Scans[4]); }

BOOST_AUTO_TEST_SUITE_END()
