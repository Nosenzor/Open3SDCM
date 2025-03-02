//
// Created by Romain Nosenzo on 15/02/2025.
//

#include "ParseDcm.h"
#include "definitions.h"

#include "boost/dynamic_bitset.hpp"
#include <algorithm>
#include <array>
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

  namespace detail
  {
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

    std::vector<char> DecodeBuffer(std::string& base64Text, size_t EstimatedBufferSize)
    {
      // remove blankspace and empty lines
      std::erase_if(base64Text, [](char c) { return c == ' ' || c == '\n' || c == '\r'; });
      std::istringstream inStream(base64Text);
      std::ostringstream outStream;
      Poco::Base64Decoder decoder(inStream);
      std::vector<char> rawData;
      rawData.reserve(EstimatedBufferSize);
      std::array<char, 4096U> chunk;
      while (decoder.read(chunk.data(), sizeof(chunk)))
      {
        rawData.insert(rawData.end(), chunk.begin(), chunk.begin() + decoder.gcount());
      }
      if (decoder.gcount() > 0)
      {
        rawData.insert(rawData.end(), chunk.begin(), chunk.begin() + decoder.gcount());
      }
      return rawData;
    }

    std::vector<float> ParseVertices(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes)
    {
      try
      {
        if (!BinaryNodes.isNull() && BinaryNodes->length() > 0)
        {
          auto binaryElement = dynamic_cast<Poco::XML::Element*>(BinaryNodes->item(0));
          if (binaryElement)
          {
            // Get the CA element inside Binary_data

            Poco::AutoPtr<Poco::XML::NodeList> caNodes = binaryElement->getElementsByTagName("Vertices");
            if (caNodes->length() > 0)
            {

              auto caElement = dynamic_cast<Poco::XML::Element*>(caNodes->item(0));
              std::string base64Text = caElement->innerText();

              auto BufferSize = GetBufferSize(BinaryNodes, "Vertices");
              auto rawData = DecodeBuffer(base64Text, BufferSize);
              // Reinterpret the raw bytes as floats
              auto floatPtr = reinterpret_cast<const float*>(rawData.data());
              std::size_t floatCount = rawData.size() / sizeof(float);
              std::vector<float> floatData(floatPtr, floatPtr + floatCount);

              return floatData;
            }
          }
        }
        return {};
      }
      catch (const Poco::Exception& ex)
      {

        return {};
      }
    }

    std::vector<Open3SDCM::Triangle> InterpretFacetsBuffer(const std::vector<char>& rawData)
    {

      //convert rawdata to bitset to ease interpretation
      boost::dynamic_bitset<> bitset(rawData.begin(), rawData.end());

      std::vector<Open3SDCM::Triangle> Triangles;

      return Triangles;
    }

    std::vector<Open3SDCM::Triangle> ParseFacets(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes)
    {
      try
      {
        if (!BinaryNodes.isNull() && BinaryNodes->length() > 0)
        {
          auto binaryElement = dynamic_cast<Poco::XML::Element*>(BinaryNodes->item(0));
          if (binaryElement)
          {
            // Get the CA element inside Binary_data

            Poco::AutoPtr<Poco::XML::NodeList> caNodes = binaryElement->getElementsByTagName("Facets");
            if (caNodes->length() > 0)
            {
              auto caElement = dynamic_cast<Poco::XML::Element*>(caNodes->item(0));
              std::string base64Text = caElement->innerText();
              auto BufferSize = GetBufferSize(BinaryNodes, "Facets");
              auto rawData = DecodeBuffer(base64Text, BufferSize);

              return InterpretFacetsBuffer(rawData);
            }
          }
        }
        return {};
      }
      catch (const Poco::Exception& ex)
      {

        return {};
      }
    }
  }// namespace detail

  void DCMParser::ParseDCM(const fs::path& filePath)
  {
    try
    {
      // Read the file content

      if (Poco::File file(filePath.string()); !file.exists())
      {
        throw Poco::FileNotFoundException(fmt::format("File not found: {}", filePath.string()));
      }

      std::ifstream fileStream(filePath);
      std::stringstream buffer;
      buffer << fileStream.rdbuf();
      std::string fileContent = buffer.str();

      // Parse the XML content
      Poco::XML::DOMParser parser;

      Poco::AutoPtr<Poco::XML::Document> document = parser.parseString(fileContent);

      // Access elements in the XML
      if (Poco::AutoPtr<Poco::XML::NodeList> versionNodes = document->getElementsByTagName("HPS");
          versionNodes->length() > 0)
      {
        auto versionElement = dynamic_cast<Poco::XML::Element*>(versionNodes->item(0));
        std::string version = versionElement->getAttribute("version");
        std::cout << "Version: " << version << std::endl;
      }

      if (Poco::AutoPtr<Poco::XML::NodeList> schemaNodes = document->getElementsByTagName("Packed_geometry");
          schemaNodes->length() > 0)
      {
        auto schemaElement = dynamic_cast<Poco::XML::Element*>(schemaNodes->item(0));
        std::string schema = schemaElement->getAttribute("Schema");
        std::cout << "Schema: " << schema << std::endl;
      }

      if (Poco::AutoPtr<Poco::XML::NodeList> GeometryBinaryNodes = document->getElementsByTagName("Binary_data");
          GeometryBinaryNodes->length() > 0)
      {
        auto GeometryBinaryElement = dynamic_cast<Poco::XML::Element*>(GeometryBinaryNodes->item(0));
        std::string GeometryBinary = GeometryBinaryElement->getAttribute("value");
        std::cout << "GeometryBinary: " << GeometryBinary << std::endl;
        ParseBinaryData(GeometryBinaryNodes);
      }

      if (Poco::AutoPtr<Poco::XML::NodeList> propertyNodes = document->getElementsByTagName("Property");
          propertyNodes->length() > 0)
      {
        auto propertyElement = dynamic_cast<Poco::XML::Element*>(propertyNodes->item(0));
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

  void DCMParser::ParseBinaryData(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes)
  {
    try
    {
      auto NbVertices = detail::GetElemCount(BinaryNodes, "Vertices");
      auto NbFaces = detail::GetElemCount(BinaryNodes, "Facets");
      fmt::print("Expected to get {} vertices\n", NbVertices);
      fmt::print("Expected to get {} faces\n", NbFaces);

      //Parse vertices
      m_Vertices = detail::ParseVertices(BinaryNodes);
      fmt::print(" {} floats ({} vertices) have been read from buffer\n", m_Vertices.size(), m_Vertices.size() / 3);
      if (m_Vertices.size() != NbVertices * 3)
      {
        fmt::print("Error: Expected to get {} vertices but got {}\n", NbVertices, m_Vertices.size() / 3);
      }
      else
      {
        fmt::print("Get Correct number of vertices\n");
      }
      detail::ParseFacets(BinaryNodes);
    }
    catch (const Poco::Exception& ex)
    {
      // Handle errors
    }
  }

}// namespace Open3SDCM