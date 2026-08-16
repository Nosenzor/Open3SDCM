//
// Created by Romain Nosenzo on 15/02/2025.
//

#pragma once
#include <vector>
#include <filesystem>
#include <map>

#include <Poco/DOM/AutoPtr.h>
#include <Poco/DOM/NodeList.h>

#include "definitions.h"

namespace fs = std::filesystem;

namespace Open3SDCM
{

  /// Parses 3Shape DCM files and exports the decoded mesh.
  ///
  /// A DCM file is a ZIP archive containing an HPS (Himsa Packed Scan) XML
  /// file whose `<Binary_data>` nodes hold base64-encoded geometry. The parser
  /// unzips the archive, locates the HPS XML, decodes the vertices and facets
  /// (optionally decrypting Blowfish-encrypted CE-schema buffers with a key
  /// derived from the `PackageLockList` property via MD5) and verifies each
  /// buffer's CRC32 checksum. The decoded mesh is then available in the public
  /// members below and can be written out by `ExportMesh()`.
  ///
  /// Typical usage:
  /// @code
  ///   Open3SDCM::DCMParser parser;
  ///   parser.ParseDCM("scan.dcm");
  ///   parser.ExportMesh("out/", "stl");
  /// @endcode
  class DCMParser
  {
  public:
    /// Parses a DCM file and populates the public geometry members.
    ///
    /// Clears any previously parsed state, then unzips the archive, extracts
    /// the HPS XML, reads the schema (CA, CB, CC = unencrypted, CE = encrypted
    /// with Blowfish), decodes vertices and triangles and finally extracts the
    /// optional surface data (base colour, UVs, embedded textures). Errors are
    /// reported on `std::cerr` rather than thrown.
    ///
    /// @param filePath Path to the `.dcm` file to parse.
    void ParseDCM(const fs::path& filePath);

    /// Writes the parsed mesh to @p outputPath in the requested @p format.
    ///
    /// Must be called after `ParseDCM()`. The writers are implemented directly
    /// in the library (no external mesh dependency). Returns false (without
    /// writing) when the mesh is empty or contains triangles referencing
    /// out-of-range vertex indices, or when @p format is unsupported.
    ///
    /// @param outputPath Destination file (STL/PLY) or directory base (OBJ
    ///                   writes a companion `.mtl` and texture image).
    /// @param format     Output format: `"stl"` (alias `"stlb"`), `"ply"` or
    ///                   `"obj"`. Defaults to `"stl"`.
    /// @return true on success, false on validation or write failure.
    bool ExportMesh(const fs::path& outputPath, const std::string& format = "stl") const;

    /// Decoded vertex positions as a flat `std::vector<float>` (x, y, z ordered,
    /// three floats per vertex). Divide `size()` by 3 to get the vertex count.
    std::vector<float> m_Vertices;
    /// Decoded triangles, each holding three 0-based indices into `m_Vertices`.
    std::vector<Triangle> m_Triangles;
    /// Optional surface data (base colour, texture coordinates, embedded images).
    SurfaceData m_SurfaceData;
  private:
    /// Decodes the geometry from the `<Binary_data>` nodes of the parsed XML,
    /// dispatching on @p schema to apply Blowfish decryption for CE files and
    /// populating `m_Vertices`, `m_Triangles` and `m_SurfaceData.baseColor`.
    void ParseBinaryData(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes, const std::string& schema, const std::map<std::string, std::string>& properties);

  }; // class DCMParser
}// namespace Open3SDCM
