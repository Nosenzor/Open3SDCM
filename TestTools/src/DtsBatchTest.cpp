// Real-world scan batch conversion test.
// Parses each DCM in TestData/real-world/ and exports to PLY, verifying:
//   - exact vertex/face counts (regression guard for decryption + facet decoding)
//   - geometry integrity (all vertex floats finite, all indices in range)
//   - successful PLY export with non-empty output file

#define BOOST_TEST_MODULE DtsBatchConversionTest
#include <boost/test/included/unit_test.hpp>

#include "ParseDcm.h"

#include <cmath>
#include <filesystem>

namespace fs = std::filesystem;

// TEST_DATA_DIR is injected by CMake as the absolute path to TestData/.
// Forward slashes are guaranteed by CMake's internal path representation.
#ifndef TEST_DATA_DIR
#define TEST_DATA_DIR "."
#endif

// ---------------------------------------------------------------------------
// Known-good counts — verified against a clean parse of the real-world batch.
// Divergence here immediately flags a decryption or facet-decode regression.
// ---------------------------------------------------------------------------
struct ScanSpec
{
  const char* filename;
  std::size_t expectedVertices;
  std::size_t expectedFaces;
};

static constexpr ScanSpec k_Scans[] = {
  {"scan_040.dcm",  50648, 101292},
  {"scan_039.dcm",  68698, 136995},
  {"scan_012.dcm",  60117, 120230},
  {"scan_019.dcm",  95497, 190206},
  {"scan_045.dcm",  99619, 198513},
};

// ---------------------------------------------------------------------------
// Fixture: unique per-test temp subdirectory, cleaned up on destruction.
// Using the scan filename as the subdir name avoids collisions under ctest -j.
// ---------------------------------------------------------------------------
struct TempOutputDir
{
  fs::path path;

  explicit TempOutputDir(const char* scanName)
    : path(fs::temp_directory_path() / "Open3SDCM_real_world_test" / scanName)
  {
    fs::remove_all(path);          // clear any stale output from a previous run
    fs::create_directories(path);
  }

  ~TempOutputDir()
  {
    std::error_code ec;
    fs::remove_all(path, ec);      // best-effort; ignore errors on cleanup
  }
};

// ---------------------------------------------------------------------------
// Geometry validity helpers
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// Shared test logic
// ---------------------------------------------------------------------------
static void runConversionTest(const ScanSpec& spec)
{
  const fs::path dcm = fs::path(TEST_DATA_DIR) / "real-world" / spec.filename;

  BOOST_TEST_MESSAGE("DCM: " << dcm.string());
  BOOST_REQUIRE_MESSAGE(fs::exists(dcm), "DCM file not found: " << dcm.string());

  Open3SDCM::DCMParser parser;
  parser.ParseDCM(dcm);

  // Fail fast if the mesh is empty — indicates a parse or decryption failure
  BOOST_REQUIRE_GT(parser.m_Vertices.size(), 0u);
  BOOST_REQUIRE_GT(parser.m_Triangles.size(), 0u);

  // Exact counts — regression guard for decryption and facet decoding
  BOOST_CHECK_EQUAL(parser.m_Vertices.size() / 3, spec.expectedVertices);
  BOOST_CHECK_EQUAL(parser.m_Triangles.size(),     spec.expectedFaces);

  // Geometry integrity — catch NaN/Inf vertices and out-of-range indices
  const std::size_t vertexCount = parser.m_Vertices.size() / 3;
  BOOST_CHECK_MESSAGE(allVerticesFinite(parser.m_Vertices),
    "Non-finite vertex coordinate detected in " << spec.filename);
  BOOST_CHECK_MESSAGE(allIndicesInRange(parser.m_Triangles, vertexCount),
    "Out-of-range triangle index detected in " << spec.filename);

  // Export to PLY in an isolated temp dir for this test case
  TempOutputDir tmp(spec.filename);
  const fs::path ply = tmp.path / (fs::path(spec.filename).stem().string() + ".ply");

  BOOST_CHECK_MESSAGE(parser.ExportMesh(ply, "ply"),
    "ExportMesh failed for " << spec.filename);
  BOOST_CHECK(fs::exists(ply));
  BOOST_CHECK_GT(fs::file_size(ply), 0u);

  BOOST_TEST_MESSAGE("PLY: " << ply.string()
    << "  (" << (fs::file_size(ply) / 1024) << " KB)");
}

// ---------------------------------------------------------------------------
// Test suite — one case per scan for independent CTest entries
// ---------------------------------------------------------------------------
BOOST_AUTO_TEST_SUITE(DtsBatchConversion)

BOOST_AUTO_TEST_CASE(ConvertScan040) { runConversionTest(k_Scans[0]); }
BOOST_AUTO_TEST_CASE(ConvertScan039) { runConversionTest(k_Scans[1]); }
BOOST_AUTO_TEST_CASE(ConvertScan012) { runConversionTest(k_Scans[2]); }
BOOST_AUTO_TEST_CASE(ConvertScan019) { runConversionTest(k_Scans[3]); }
BOOST_AUTO_TEST_CASE(ConvertScan045) { runConversionTest(k_Scans[4]); }

BOOST_AUTO_TEST_SUITE_END()
