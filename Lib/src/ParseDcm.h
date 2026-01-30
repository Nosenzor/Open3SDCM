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

  class DCMParser
  {
  public:
    void ParseDCM(const fs::path& filePath);
    bool ExportMesh(const fs::path& outputPath, const std::string& format = "stl") const;
    void SetCustomDecryptionKey(const std::vector<unsigned char>& key) { m_CustomDecryptionKey = key; }
    void SetKeyDiscoveryMode(bool enable) { m_KeyDiscoveryMode = enable; }

    std::vector<float> m_Vertices; //Buffer of vertices (x,y,z) contigous size/3 to get Nb of Vertices
    std::vector<Triangle> m_Triangles; //Buffer of triangles (indices)
  private:
    void ParseBinaryData(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes, const std::string& schema, const std::map<std::string, std::string>& properties);
    std::vector<unsigned char> m_CustomDecryptionKey;
    bool m_KeyDiscoveryMode = false;

  }; // class DCMParser
}// namespace Open3SDCM
