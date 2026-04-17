//
// Created by Romain Nosenzo on 15/02/2025.
//

#include "ParseDcm.h"
#include "definitions.h"

#include "boost/dynamic_bitset.hpp"
#include <algorithm>
#include <array>
#include <deque>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <map>
#include <set>
#include <iomanip>
#include <charconv>

#include <openssl/blowfish.h>
#include <openssl/md5.h>

#include "Poco/Base64Decoder.h"
#include "Poco/Checksum.h"
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

    std::string ComputePackageLockHash(const std::map<std::string, std::string>& props)
    {
      auto it = props.find("PackageLockList");
      if (it == props.end()) return "";

      std::string value = it->second;
      if (value.empty()) return "";

      std::set<std::string> items;
      std::stringstream ss(value);
      std::string item;
      while (std::getline(ss, item, ';')) {
        if (!item.empty()) items.insert(item);
      }

      if (items.empty()) return "";

      std::string canonical;
      for (const auto& i : items) {
        canonical += i + ";";
      }

      unsigned char digest[MD5_DIGEST_LENGTH];
      MD5((unsigned char*)canonical.c_str(), canonical.length(), digest);

      std::stringstream hex;
      hex << std::hex << std::uppercase;
      for(int i = 0; i < MD5_DIGEST_LENGTH; ++i)
        hex << std::setw(2) << std::setfill('0') << (int)digest[i];

      return hex.str();
    }

    void SwapEndianness(std::vector<char>& data)
    {
      for (size_t i = 0; i + 8 <= data.size(); i += 8)
      {
           // Swap two 32-bit integers from LE to BE (or vice versa)
           // [0 1 2 3] [4 5 6 7] -> [3 2 1 0] [7 6 5 4]
           std::swap(data[i+0], data[i+3]);
           std::swap(data[i+1], data[i+2]);

           std::swap(data[i+4], data[i+7]);
           std::swap(data[i+5], data[i+6]);
      }
    }

    std::vector<char> DecryptBuffer(std::vector<char>& data, const std::string& schema, const std::map<std::string, std::string>& props)
    {
        if (schema != "CE") return data;

        // Base key (extracted from dcm2stl reference implementation)
        std::vector<unsigned char> key = {
            0x34, 0x90, 0x02, 0x93, 0x58, 0x2F, 0x49, 0x94,
            0x76, 0x02, 0x19, 0xDF, 0x3B, 0x56, 0x44, 0x1C
        };

        // Check EKID
        std::string ekid = "1";
        auto it = props.find("EKID");
        if (it != props.end()) ekid = it->second;

        std::string packageHash = ComputePackageLockHash(props);
        std::vector<unsigned char> finalKey = key;

        if (ekid == "1" && !packageHash.empty()) {
             for (char c : packageHash) {
                 finalKey.push_back((unsigned char)c);
             }
        }

        // Decrypt
        BF_KEY bfKey;
        BF_set_key(&bfKey, finalKey.size(), finalKey.data());

        // Pad to 8 bytes
        size_t originalSize = data.size();
        size_t padding = 0;
        if (data.size() % 8 != 0) {
            padding = 8 - (data.size() % 8);
            data.resize(data.size() + padding, 0);
        }

        SwapEndianness(data);

        std::vector<char> decrypted(data.size());
        for (size_t i = 0; i < data.size(); i += 8) {
            BF_ecb_encrypt((unsigned char*)&data[i], (unsigned char*)&decrypted[i], &bfKey, BF_DECRYPT);
        }

        SwapEndianness(decrypted);

        // Truncate to original size?
        // The python code truncates to vertex_count * 12.
        // Here we return the full decrypted buffer (potentially padded).
        // The caller should handle truncation if needed, or we can truncate to originalSize.
        // But originalSize might not be 8-byte aligned, so padding was added.
        // If we truncate to originalSize, we might lose data if the original data was indeed padded?
        // No, padding is added for encryption/decryption block size.
        // If the original data was not a multiple of 8, it must have been padded before encryption?
        // Or maybe the stream is just bytes.
        // In Python: `decoded = decoded[:original_size]` where `original_size = vertex_count * 12`.
        // So we should probably let the caller handle truncation based on vertex count.

        return decrypted;
    }

    std::vector<float> ParseVertices(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes, const std::string& schema, const std::map<std::string, std::string>& props)
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

              rawData = DecryptBuffer(rawData, schema, props);

              auto vertexCount = GetElemCount(BinaryNodes, "Vertices");
              std::size_t expectedSize = vertexCount * 3 * sizeof(float);

              // Truncate to expected size
              if (rawData.size() > expectedSize) {
                  rawData.resize(expectedSize);
              }

              // Verify checksum for CE schema
              if (schema == "CE" && caElement->hasAttribute("check_value")) {
                  std::string checkValueStr = caElement->getAttribute("check_value");
                  uint32_t checkValue = 0;
                  auto [ptr, ec] = std::from_chars(checkValueStr.data(), checkValueStr.data() + checkValueStr.size(), checkValue);

                  if (ec == std::errc()) {
                      Poco::Checksum checksum(Poco::Checksum::TYPE_ADLER32);
                      checksum.update(rawData.data(), rawData.size());
                      uint32_t adler = checksum.checksum();

                      // Swap endianness to match reference implementation
                      uint32_t swappedAdler = ((adler & 0xFF000000) >> 24) |
                                              ((adler & 0x00FF0000) >> 8)  |
                                              ((adler & 0x0000FF00) << 8)  |
                                              ((adler & 0x000000FF) << 24);

                      if (swappedAdler != checkValue) {
                          std::cerr << "Error: CE schema checksum mismatch! Expected: " << checkValue
                                    << ", got: " << swappedAdler << ". Decryption key may be incorrect." << std::endl;
                      }
                  }
              }

              // Reinterpret the raw bytes as floats
              auto floatPtr = reinterpret_cast<const float*>(rawData.data());
              std::size_t floatCount = vertexCount * 3;

              // Ensure we don't read past buffer
              if (floatCount * sizeof(float) > rawData.size()) {
                  std::cerr << "Error: Decrypted buffer too small for vertex count" << std::endl;
                  return {};
              }

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
      // Edge in the circular edge list
      struct Edge
      {
        size_t start;
        size_t end;
      };

      // Run the full decode with a given payload width for opcodes 5 and 7.
      // Returns the produced triangles so we can retry with a different mode.
      auto decode = [&](bool use32BitPayload) -> std::vector<Open3SDCM::Triangle> {
        std::vector<Open3SDCM::Triangle> triangles;
        triangles.reserve(expectedFaceCount);

        std::vector<Edge> edgeList;
        size_t currentEdgeIdx  = 0;
        size_t globalVertexPtr = 0;
        size_t offset          = 0;

        // ── helpers ──────────────────────────────────────────────────────────

        auto requireBytes = [&](size_t n) -> bool {
          return offset + n <= rawData.size();
        };

        auto readUint16 = [&]() -> size_t {
          uint16_t v = 0;
          std::memcpy(&v, &rawData[offset], sizeof(v));
          offset += sizeof(v);
          return static_cast<size_t>(v);
        };

        auto readUint32 = [&]() -> size_t {
          uint32_t v = 0;
          std::memcpy(&v, &rawData[offset], sizeof(v));
          offset += sizeof(v);
          return static_cast<size_t>(v);
        };

        // Read one index according to the current payload mode
        auto readIdx = [&]() -> size_t {
          return use32BitPayload ? readUint32() : readUint16();
        };

        auto advanceEdgePointer = [&](size_t n = 1) {
          if (!edgeList.empty())
            currentEdgeIdx = (currentEdgeIdx + n) % edgeList.size();
        };

        auto createRestartFace = [&](size_t v0, size_t v1, size_t v2) {
          triangles.push_back({v0, v1, v2});
          edgeList       = {{v0, v1}, {v1, v2}, {v2, v0}};
          currentEdgeIdx = 0;
        };

        auto extendCurrentEdge = [&](size_t v) {
          if (edgeList.empty())
            return;
          Edge curr = edgeList[currentEdgeIdx];
          triangles.push_back({v, curr.end, curr.start});
          edgeList.erase(edgeList.begin() + static_cast<std::ptrdiff_t>(currentEdgeIdx));
          edgeList.insert(edgeList.begin() + static_cast<std::ptrdiff_t>(currentEdgeIdx),
                          Edge{v, curr.end});
          edgeList.insert(edgeList.begin() + static_cast<std::ptrdiff_t>(currentEdgeIdx),
                          Edge{curr.start, v});
        };

        auto handlePrevious = [&]() {
          if (edgeList.size() < 2)
            return;
          const size_t n       = edgeList.size();
          const size_t prevIdx = (currentEdgeIdx + n - 1) % n;
          const size_t currIdx = currentEdgeIdx;

          Edge prevEdge = edgeList[prevIdx];
          Edge currEdge = edgeList[currIdx];
          triangles.push_back({currEdge.start, prevEdge.start, currEdge.end});

          Edge newEdge             = {prevEdge.start, currEdge.end};
          const size_t highIdx     = std::max(currIdx, prevIdx);
          const size_t lowIdx      = std::min(currIdx, prevIdx);
          edgeList.erase(edgeList.begin() + static_cast<std::ptrdiff_t>(highIdx));
          edgeList.erase(edgeList.begin() + static_cast<std::ptrdiff_t>(lowIdx));
          edgeList.insert(edgeList.begin() + static_cast<std::ptrdiff_t>(lowIdx), newEdge);
          currentEdgeIdx = (lowIdx + 1) % edgeList.size();
        };

        auto handleNext = [&]() {
          if (edgeList.size() < 2)
            return;
          const size_t currIdx = currentEdgeIdx;
          const size_t nextIdx = (currIdx + 1) % edgeList.size();

          Edge currEdge = edgeList[currIdx];
          Edge nextEdge = edgeList[nextIdx];
          triangles.push_back({currEdge.start, nextEdge.end, currEdge.end});

          Edge newEdge             = {currEdge.start, nextEdge.end};
          const size_t highIdx     = std::max(currIdx, nextIdx);
          const size_t lowIdx      = std::min(currIdx, nextIdx);
          edgeList.erase(edgeList.begin() + static_cast<std::ptrdiff_t>(highIdx));
          edgeList.erase(edgeList.begin() + static_cast<std::ptrdiff_t>(lowIdx));
          edgeList.insert(edgeList.begin() + static_cast<std::ptrdiff_t>(lowIdx), newEdge);
          currentEdgeIdx = (lowIdx + 1) % edgeList.size();
        };

        auto removeCurrentEdge = [&]() {
          if (edgeList.empty())
            return;
          const size_t n       = edgeList.size();
          const size_t prevIdx = (currentEdgeIdx + n - 1) % n;
          const size_t currIdx = currentEdgeIdx;

          Edge prevEdge = edgeList[prevIdx];
          Edge currEdge = edgeList[currIdx];

          if (prevEdge.start == currEdge.end && n > 2) {
            const size_t highIdx = std::max(currIdx, prevIdx);
            const size_t lowIdx  = std::min(currIdx, prevIdx);
            edgeList.erase(edgeList.begin() + static_cast<std::ptrdiff_t>(highIdx));
            edgeList.erase(edgeList.begin() + static_cast<std::ptrdiff_t>(lowIdx));
            if (!edgeList.empty()) {
              const size_t newPrevIdx = (lowIdx + edgeList.size() - 1) % edgeList.size();
              const size_t newCurrIdx = lowIdx % edgeList.size();
              edgeList[newPrevIdx].end = edgeList[newCurrIdx].start;
              currentEdgeIdx           = newCurrIdx;
            } else {
              currentEdgeIdx = 0;
            }
          } else {
            edgeList[prevIdx].end = currEdge.end;
            edgeList.erase(edgeList.begin() + static_cast<std::ptrdiff_t>(currIdx));
            currentEdgeIdx = edgeList.empty() ? 0 : currIdx % edgeList.size();
          }
        };

        // ── main decode loop ─────────────────────────────────────────────────

        while (offset < rawData.size()) {
          // Early-abort when clearly in the wrong mode:
          // face count >10% over expected, OR edge list grown absurdly large
          // (both happen when the wrong payload width misinterprets the byte stream)
          if (expectedFaceCount > 0 &&
              (triangles.size() > expectedFaceCount + expectedFaceCount / 10 ||
               edgeList.size() > expectedFaceCount / 4 + 1000))
            break;

          const uint8_t opcode = static_cast<uint8_t>(rawData[offset]) & 0x0F;
          offset++;

          switch (opcode) {
            case 0: { // VERTEX_LIST
              extendCurrentEdge(globalVertexPtr++);
              advanceEdgePointer(2);
              break;
            }
            case 1: { // PREVIOUS
              handlePrevious();
              break;
            }
            case 2: { // NEXT
              handleNext();
              break;
            }
            case 3: { // IGNORE
              advanceEdgePointer(1);
              break;
            }
            case 4: { // RESTART
              const size_t v0 = globalVertexPtr++;
              const size_t v1 = globalVertexPtr++;
              const size_t v2 = globalVertexPtr++;
              createRestartFace(v0, v1, v2);
              break;
            }
            case 5: { // RESTART_16 — payload width depends on mode
              if (!requireBytes(use32BitPayload ? 12 : 6)) break;
              createRestartFace(readIdx(), readIdx(), readIdx());
              break;
            }
            case 6: { // RESTART_32 — always 32-bit
              if (!requireBytes(12)) break;
              createRestartFace(readUint32(), readUint32(), readUint32());
              break;
            }
            case 7: { // ABSOLUTE_16 — payload width depends on mode
              if (!requireBytes(use32BitPayload ? 4 : 2)) break;
              extendCurrentEdge(readIdx());
              advanceEdgePointer(2);
              break;
            }
            case 8: { // ABSOLUTE_32 — always 32-bit
              if (!requireBytes(4)) break;
              extendCurrentEdge(readUint32());
              advanceEdgePointer(2);
              break;
            }
            case 9: { // REMOVE
              removeCurrentEdge();
              break;
            }
            case 10: { // INCREASE_VERTEX_LIST_POINTER
              globalVertexPtr++;
              break;
            }
            default:
              break;
          }
        }

        return triangles;
      }; // end decode lambda

      // ── mode detection: try 16-bit, fall back to 32-bit (mirrors hpsdecode) ──

      auto result = decode(false);
      if (result.size() == expectedFaceCount)
        return result;

      auto result32 = decode(true);
      if (result32.size() == expectedFaceCount)
        return result32;

      // Neither mode matched — return whichever is closer and warn
      std::cerr << "Warning: Face count mismatch — expected " << expectedFaceCount
                << ", got " << result32.size() << " (32-bit) or " << result.size() << " (16-bit)"
                << std::endl;
      return result32;
    }

    std::vector<Open3SDCM::Triangle> ParseFacets(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes, const std::string& schema, const std::map<std::string, std::string>& props)
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

              // Facets don't seem to be encrypted in CE schema based on Python implementation
              // But if they were, we would do:
              // rawData = DecryptBuffer(rawData, schema, props);

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

      std::string schema;
      if (Poco::AutoPtr<Poco::XML::NodeList> versionNodes = document->getElementsByTagName("HPS");
          versionNodes->length() > 0)
      {
        auto versionElement = dynamic_cast<Poco::XML::Element*>(versionNodes->item(0));
        std::string version = versionElement->getAttribute("version");
      }

      if (Poco::AutoPtr<Poco::XML::NodeList> schemaNodes = document->getElementsByTagName("Schema");
          schemaNodes->length() > 0)
      {
        auto schemaElement = dynamic_cast<Poco::XML::Element*>(schemaNodes->item(0));
        // Get the text content of the Schema element
        if (schemaElement->hasChildNodes())
        {
          auto firstChild = schemaElement->firstChild();
          if (firstChild)
          {
            schema = firstChild->nodeValue();
          }
        }
      }

      std::map<std::string, std::string> properties;
      if (Poco::AutoPtr<Poco::XML::NodeList> propertyNodes = document->getElementsByTagName("Property");
          propertyNodes->length() > 0)
      {
        for (unsigned long i = 0; i < propertyNodes->length(); ++i)
        {
            auto propertyElement = dynamic_cast<Poco::XML::Element*>(propertyNodes->item(i));
            if (propertyElement) {
                std::string name = propertyElement->getAttribute("name");
                std::string value = propertyElement->getAttribute("value");
                if (!name.empty()) {
                    properties[name] = value;
                }
            }
        }
      }

      if (Poco::AutoPtr<Poco::XML::NodeList> GeometryBinaryNodes = document->getElementsByTagName("Binary_data");
          GeometryBinaryNodes->length() > 0)
      {
        auto GeometryBinaryElement = dynamic_cast<Poco::XML::Element*>(GeometryBinaryNodes->item(0));
        ParseBinaryData(GeometryBinaryNodes, schema, properties);
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

  void DCMParser::ParseBinaryData(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes, const std::string& schema, const std::map<std::string, std::string>& properties)
  {
    try
    {
      auto NbVertices = detail::GetElemCount(BinaryNodes, "Vertices");
      auto NbFaces = detail::GetElemCount(BinaryNodes, "Facets");
      fmt::print("Expected to get {} vertices\n", NbVertices);
      fmt::print("Expected to get {} faces\n", NbFaces);

      //Parse vertices
      m_Vertices = detail::ParseVertices(BinaryNodes, schema, properties);
      fmt::print(" {} floats ({} vertices) have been read from buffer\n", m_Vertices.size(), m_Vertices.size() / 3);
      if (m_Vertices.size() != NbVertices * 3)
      {
        fmt::print("Error: Expected to get {} floats but got {}\n", NbVertices * 3, m_Vertices.size());
      }
      else
      {
        fmt::print("Get Correct number of vertices\n");
      }

      //Parse facets
      m_Triangles = detail::ParseFacets(BinaryNodes, schema, properties);
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
