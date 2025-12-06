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

#include <assimp/Exporter.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

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

    std::vector<Open3SDCM::Triangle> InterpretFacetsBuffer(const std::vector<char>& rawData, size_t expectedFaceCount)
    {
      constexpr uint8_t FACET_COMMAND_MASK = 0x0F;

      std::vector<Open3SDCM::Triangle> Triangles;
      Triangles.reserve(expectedFaceCount);

      std::deque<std::pair<size_t, size_t>> edgeQueue;
      size_t commandOffset = 0;
      size_t vertexOffset = 0;

      // Helper lambda to append a face
      auto appendFace = [&Triangles](size_t v1, size_t v2, size_t v3) {
        Triangles.push_back({v1, v2, v3});
      };

      // Helper to read command
      auto readOp = [&rawData, &commandOffset]() -> uint8_t {
        uint8_t result = static_cast<uint8_t>(rawData[commandOffset]) & FACET_COMMAND_MASK;
        commandOffset++;
        return result;
      };

      // Helper to read int16 (note: increments by 4 bytes as per Python code)
      // Returns absolute vertex index (handles negative indices as relative to vertex_offset)
      auto read16 = [&rawData, &commandOffset, &vertexOffset]() -> size_t {
        int16_t result;
        std::memcpy(&result, &rawData[commandOffset], sizeof(int16_t));
        commandOffset += 4;  // Python code increments by 4, not 2
        // Handle negative indices as relative to vertex_offset
        if (result < 0) {
          // Vérifier que le résultat ne sera pas négatif
          if (static_cast<size_t>(-result) > vertexOffset) {
            std::cerr << "Warning: read16 negative index " << result
                      << " larger than vertex_offset " << vertexOffset << std::endl;
            return 0;
          }
          return vertexOffset + result;  // result est négatif, donc soustraction
        }
        return static_cast<size_t>(result);
      };

      // Helper to read int32
      // Returns absolute vertex index (handles negative indices as relative to vertex_offset)
      auto read32 = [&rawData, &commandOffset, &vertexOffset]() -> size_t {
        int32_t result;
        std::memcpy(&result, &rawData[commandOffset], sizeof(int32_t));
        commandOffset += sizeof(int32_t);
        // Handle negative indices as relative to vertex_offset
        if (result < 0) {
          // Vérifier que le résultat ne sera pas négatif
          if (static_cast<size_t>(-result) > vertexOffset) {
            std::cerr << "Warning: read32 negative index " << result
                      << " larger than vertex_offset " << vertexOffset << std::endl;
            return 0;
          }
          return vertexOffset + result;  // result est négatif, donc soustraction
        }
        return static_cast<size_t>(result);
      };

      // Helper for restart operation
      auto restart = [&](size_t vid0, size_t vid1, size_t vid2) {
        edgeQueue.clear();
        appendFace(vid0, vid1, vid2);
        edgeQueue.push_back({vid0, vid1});
        edgeQueue.push_back({vid1, vid2});
        edgeQueue.push_back({vid2, vid1});
      };

      // Helper for absolute operation
      auto absolute = [&](size_t index) {
        auto current = edgeQueue.front();
        edgeQueue.pop_front();
        appendFace(current.first, index, current.second);
        edgeQueue.push_back({current.first, index});
        edgeQueue.push_back({index, current.second});
      };

      // Process all commands
      while (commandOffset < rawData.size()) {
        uint8_t command = readOp();

        switch (command) {
          case 0: { // op0
            auto current = edgeQueue.front();
            edgeQueue.pop_front();
            appendFace(current.first, vertexOffset, current.second);
            edgeQueue.push_back({current.first, vertexOffset});
            edgeQueue.push_back({vertexOffset, current.second});
            vertexOffset++;
            break;
          }
          case 1: { // op1
            auto current = edgeQueue.front();
            edgeQueue.pop_front();
            auto previous = edgeQueue.back();
            edgeQueue.pop_back();
            appendFace(current.first, previous.first, current.second);
            edgeQueue.push_back({previous.first, current.second});
            break;
          }
          case 2: { // op2
            auto current = edgeQueue.front();
            edgeQueue.pop_front();
            auto next = edgeQueue.front();
            edgeQueue.pop_front();
            appendFace(current.first, next.second, current.second);
            edgeQueue.push_back({current.first, next.second});
            break;
          }
          case 3: { // op3 - rotate
            auto front = edgeQueue.front();
            edgeQueue.pop_front();
            edgeQueue.push_back(front);
            break;
          }
          case 4: { // op4
            restart(vertexOffset, vertexOffset + 1, vertexOffset + 2);
            vertexOffset += 3;
            break;
          }
          case 5: { // op5
            restart(read16(), read16(), read16());
            break;
          }
          case 6: { // op6
            restart(read32(), read32(), read32());
            break;
          }
          case 7: { // op7
            absolute(read16());
            break;
          }
          case 8: { // op8
            absolute(read32());
            break;
          }
          case 9: { // op9
            auto current = edgeQueue.front();
            edgeQueue.pop_front();
            if (edgeQueue.size() > 1) {
              if (edgeQueue.back().first == edgeQueue.front().first) {
                edgeQueue.pop_back();
              } else if (edgeQueue.back().second == current.first &&
                         edgeQueue.back().first == current.second) {
                edgeQueue.pop_back();
              } else {
                edgeQueue.back().second = edgeQueue.front().second;
              }
            }
            break;
          }
          case 10: { // op10
            vertexOffset++;
            break;
          }
          default: {
            std::cerr << "Warning: Invalid command detected: " << static_cast<int>(command) << std::endl;
            break;
          }
        }
      }

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
              auto FaceCount = GetElemCount(BinaryNodes, "Facets");
              auto rawData = DecodeBuffer(base64Text, BufferSize);

              return InterpretFacetsBuffer(rawData, FaceCount);
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

      //Parse facets
      m_Triangles = detail::ParseFacets(BinaryNodes);
      fmt::print(" {} triangles have been read from buffer\n", m_Triangles.size());
      if (m_Triangles.size() != NbFaces)
      {
        fmt::print("Error: Expected to get {} faces but got {}\n", NbFaces, m_Triangles.size());
      }
      else
      {
        fmt::print("Get Correct number of faces\n");
      }
    }
    catch (const Poco::Exception& ex)
    {
      // Handle errors
    }
  }

  bool DCMParser::ExportMesh(const fs::path& outputPath, const std::string& format) const
  {
    if (m_Vertices.empty() || m_Triangles.empty())
    {
      fmt::print("Error: No mesh data to export\n");
      return false;
    }

    // Validate triangle indices
    const size_t numVertices = m_Vertices.size() / 3;
    size_t invalidTriangles = 0;
    for (size_t i = 0; i < m_Triangles.size(); ++i)
    {
      if (m_Triangles[i].v1 >= numVertices ||
          m_Triangles[i].v2 >= numVertices ||
          m_Triangles[i].v3 >= numVertices)
      {
        fmt::print("Warning: Triangle {} has invalid indices: ({}, {}, {}), max vertex index: {}\n",
                   i, m_Triangles[i].v1, m_Triangles[i].v2, m_Triangles[i].v3, numVertices - 1);
        invalidTriangles++;
      }
    }

    if (invalidTriangles > 0)
    {
      fmt::print("Error: Found {} triangles with invalid indices. Cannot export.\n", invalidTriangles);
      return false;
    }

    // Create Assimp scene
    aiScene* scene = new aiScene();
    scene->mRootNode = new aiNode();

    // Create mesh
    scene->mNumMeshes = 1;
    scene->mMeshes = new aiMesh*[1];
    aiMesh* mesh = new aiMesh();
    scene->mMeshes[0] = mesh;
    scene->mRootNode->mNumMeshes = 1;
    scene->mRootNode->mMeshes = new unsigned int[1];
    scene->mRootNode->mMeshes[0] = 0;

    // Set vertices
    mesh->mNumVertices = numVertices;
    mesh->mVertices = new aiVector3D[mesh->mNumVertices];
    for (size_t i = 0; i < mesh->mNumVertices; ++i)
    {
      mesh->mVertices[i].x = m_Vertices[i * 3 + 0];
      mesh->mVertices[i].y = m_Vertices[i * 3 + 1];
      mesh->mVertices[i].z = m_Vertices[i * 3 + 2];
    }

    // Set faces
    mesh->mNumFaces = m_Triangles.size();
    mesh->mFaces = new aiFace[mesh->mNumFaces];
    for (size_t i = 0; i < mesh->mNumFaces; ++i)
    {
      aiFace& face = mesh->mFaces[i];
      face.mNumIndices = 3;
      face.mIndices = new unsigned int[3];
      face.mIndices[0] = m_Triangles[i].v1;
      face.mIndices[1] = m_Triangles[i].v2;
      face.mIndices[2] = m_Triangles[i].v3;
    }

    // Create a default material
    scene->mNumMaterials = 1;
    scene->mMaterials = new aiMaterial*[1];
    scene->mMaterials[0] = new aiMaterial();
    mesh->mMaterialIndex = 0;

    // Determine format ID for Assimp
    std::string formatId = format;
    if (format == "stl") formatId = "stl";
    else if (format == "obj") formatId = "obj";
    else if (format == "ply") formatId = "ply";
    else if (format == "stlb") formatId = "stlb";  // STL binary

    // Export using Assimp without JoinIdenticalVertices to avoid crashes
    Assimp::Exporter exporter;
    aiReturn result = exporter.Export(scene, formatId, outputPath.string(), 0);

    // Clean up
    delete scene;

    if (result != AI_SUCCESS)
    {
      fmt::print("Error: Failed to export mesh - {}\n", exporter.GetErrorString());
      return false;
    }

    fmt::print("Successfully exported mesh to: {}\n", outputPath.string());
    return true;
  }

}// namespace Open3SDCM