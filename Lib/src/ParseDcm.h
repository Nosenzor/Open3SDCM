//
// Created by Romain Nosenzo on 15/02/2025.
//

#pragma once
#include <filesystem>

#include <Poco/DOM/AutoPtr.h>
#include <Poco/DOM/NodeList.h>

namespace fs = std::filesystem;

namespace Open3SDCM
{
  class DCMParser
  {
  public:
    void ParseDCM(const fs::path& filePath);
  private:
    void ParseBinaryData(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes);

  }; // class DCMParser
}// namespace Open3SDCM
