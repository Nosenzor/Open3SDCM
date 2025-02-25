//
// Created by Romain Nosenzo on 15/02/2025.
//

#include "ParseDcm.h"
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

#include "Poco/Base64Decoder.h"
#include <Poco/DOM/AutoPtr.h>
#include <Poco/DOM/DOMParser.h>
#include <Poco/DOM/Document.h>
#include <Poco/DOM/Element.h>
#include <Poco/DOM/NodeList.h>
#include <Poco/DOM/Text.h>
#include <Poco/Exception.h>
#include <Poco/File.h>
#include <Poco/Path.h>
#include <Poco/XML/XMLException.h>

#include <fmt/ostream.h>
namespace fs = std::filesystem;
namespace Open3SDCM
{

  struct Vertex {
    float x;
    float y;
    float z;
  };

  struct Facet {
    Vertex normal;
    Vertex v1;
    Vertex v2;
    Vertex v3;
  };

  void DCMParser::ParseDCM(const fs::path& filePath)
  {
    try
    {
      // Read the file content
      Poco::File file(filePath);
      if (!file.exists())
      {
        throw Poco::FileNotFoundException("File not found: " / filePath);
      }

      std::ifstream fileStream(filePath);
      std::stringstream buffer;
      buffer << fileStream.rdbuf();
      std::string fileContent = buffer.str();

      // Parse the XML content
      Poco::XML::DOMParser parser;

      Poco::AutoPtr<Poco::XML::Document> document = parser.parseString(fileContent);

      // Access elements in the XML
      Poco::AutoPtr<Poco::XML::NodeList> versionNodes = document->getElementsByTagName("HPS");
      if (versionNodes->length() > 0)
      {
        Poco::XML::Element* versionElement = static_cast<Poco::XML::Element*>(versionNodes->item(0));
        std::string version = versionElement->getAttribute("version");
        std::cout << "Version: " << version << std::endl;
      }

      Poco::AutoPtr<Poco::XML::NodeList> schemaNodes = document->getElementsByTagName("Packed_geometry");
      if (schemaNodes->length() > 0)
      {
        auto schemaElement = dynamic_cast<Poco::XML::Element*>(schemaNodes->item(0));
        std::string schema = schemaElement->getAttribute("Schema");
        std::cout << "Schema: " << schema << std::endl;
      }

      Poco::AutoPtr<Poco::XML::NodeList> GeometryBinaryNodes = document->getElementsByTagName("Binary_data");
      if (GeometryBinaryNodes->length() > 0)
      {
        auto GeometryBinaryElement = dynamic_cast<Poco::XML::Element*>(GeometryBinaryNodes->item(0));
        std::string GeometryBinary = GeometryBinaryElement->getAttribute("value");
        std::cout << "GeometryBinary: " << GeometryBinary << std::endl;
        ParseBinaryData(GeometryBinaryNodes);
      }

      Poco::AutoPtr<Poco::XML::NodeList> propertyNodes = document->getElementsByTagName("Property");
      if (propertyNodes->length() > 0)
      {
        Poco::XML::Element* propertyElement = static_cast<Poco::XML::Element*>(propertyNodes->item(0));
        std::string sourceApp = propertyElement->getAttribute("value");
        std::cout << "SourceApp: " << sourceApp << std::endl;
      }
    }
    catch (const Poco::XML::XMLException& ex)
    {
      std::cerr << "Poco XML Exception: " << ex.displayText() << std::endl;
    }
    catch (const Poco::Exception& ex)
    {
      std::cerr << "Poco Exception: " << ex.displayText() << std::endl;
    }
    catch (const std::exception& ex)
    {
      std::cerr << "Exception: " << ex.what() << std::endl;
    }
  }

  size_t GetElemCount(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes, const std::string& GeomType)
  {
    try
    {
      if (!BinaryNodes.isNull() && BinaryNodes->length() > 0)
      {
        auto binaryElement = dynamic_cast<Poco::XML::Element*>(BinaryNodes->item(0));
        if (binaryElement)
        {
          // Get the CA element inside Binary_data

          Poco::AutoPtr<Poco::XML::NodeList> caNodes = binaryElement->getElementsByTagName(GeomType);
          if (caNodes->length() > 0)
          {
            auto caElement = dynamic_cast<Poco::XML::Element*>(caNodes->item(0));

            std::string AttrName = GeomType == "Vertices" ? "vertex_count" : "facet_count";
            std::string StrCount = caElement->getAttribute(AttrName);
            size_t Count;
            std::from_chars(StrCount.data(), StrCount.data() + StrCount.size(), Count);
            return Count;
          }
        }
      }
      return 0;
    }
    catch (const Poco::Exception& ex)
    {
      std::cerr << "Error getting vertex count: " << ex.displayText() << std::endl;
      return 0;
    }
  }

  size_t GetBufferSize(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes, const std::string& GeomType)
  {
    try
    {
      if (!BinaryNodes.isNull() && BinaryNodes->length() > 0)
      {
        auto binaryElement = dynamic_cast<Poco::XML::Element*>(BinaryNodes->item(0));
        if (binaryElement)
        {
          // Get the CA element inside Binary_data

          Poco::AutoPtr<Poco::XML::NodeList> caNodes = binaryElement->getElementsByTagName(GeomType);
          if (caNodes->length() > 0)
          {
            auto caElement = dynamic_cast<Poco::XML::Element*>(caNodes->item(0));

            std::string StrBuffersize = caElement->getAttribute("base64_encoded_bytes");
            size_t Count;
            std::from_chars(StrBuffersize.data(), StrBuffersize.data() + StrBuffersize.size(), Count);
            return Count;
          }
        }
      }
      return 0;
    }
    catch (const Poco::Exception& ex)
    {
      std::cerr << "Error getting vertex count: " << ex.displayText() << std::endl;
      return 0;
    }
  }
  void DCMParser::ParseBinaryData(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes)
  {
    try
    {
      auto NbVertices = GetElemCount(BinaryNodes, "Vertices");
      auto NbFaces = GetElemCount(BinaryNodes, "Facets");
      fmt::print("Expected to get {} vertices\n", NbVertices);
      fmt::print("Expected to get {} faces\n", NbFaces);
      if (!BinaryNodes.isNull() && BinaryNodes->length() > 0)
      {
        const auto* binaryElem = dynamic_cast<Poco::XML::Element*>(BinaryNodes->item(0));
        if (binaryElem)
        {
          // Decode base64 text
          std::string base64Text = binaryElem->innerText();
          std::istringstream inStream(base64Text);
          std::ostringstream outStream;
          Poco::Base64Decoder decoder(inStream);
          outStream << decoder.rdbuf();
          std::string decodedData = outStream.str();
          std::vector<unsigned int> facets;
          const auto* dataPtr = reinterpret_cast<const std::uint8_t*>(decodedData.data());
          size_t dataSize = decodedData.size();

          // Dummy example of parsing:
          // Assuming we read counts from the start:
          if (dataSize >= 8)
          {
            // std::uint32_t vertexCount = *(reinterpret_cast<const std::uint32_t*>(dataPtr));
            std::uint32_t facetCount = *(reinterpret_cast<const std::uint32_t*>(dataPtr + 4));

            // Move pointer to actual vertex data
            size_t offset = 8;
            m_Vertices.reserve(NbVertices * 3);// x,y,z for each vertex
            for (std::uint32_t i = 0; i < NbVertices * 3 && offset + sizeof(float) <= dataSize; ++i)
            {
              float val = *(reinterpret_cast<const float*>(dataPtr + offset));
              m_Vertices.push_back(val);
              offset += sizeof(float);
            }

            // // Parse facets
            // facets.reserve(facetCount * 3);
            // for (std::uint32_t i = 0; i < facetCount * 3 && offset + sizeof(std::uint32_t) <= dataSize; ++i)
            // {
            //   std::uint32_t idx = *(reinterpret_cast<const std::uint32_t*>(dataPtr + offset));
            //   facets.push_back(idx);
            //   offset += sizeof(std::uint32_t);
            // }
          }

          // Store or use vertices & facets as needed
        }
      }
    }
    catch (const Poco::Exception& ex)
    {
      // Handle errors
    }
  }

}// namespace Open3SDCM