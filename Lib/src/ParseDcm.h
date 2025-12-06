//
// Created by Romain Nosenzo on 15/02/2025.
//

#pragma once
#include <vector>
#include <filesystem>

#include <Poco/DOM/AutoPtr.h>
#include <Poco/DOM/NodeList.h>

#include "definitions.h"

namespace fs = std::filesystem;

namespace Open3SDCM
{

  class DCMParser
  {
  public:
    void ParseDCM(const fs::path& filePath);
    bool ExportMesh(const fs::path& outputPath, const std::string& format = "stl") const;

    std::vector<float> m_Vertices; //Buffer of vertices (x,y,z) contigous size/3 to get Nb of Vertices
    std::vector<Triangle> m_Triangles; //Buffer of triangles (indices)
  private:
    void ParseBinaryData(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes);

  }; // class DCMParser
}// namespace Open3SDCM
