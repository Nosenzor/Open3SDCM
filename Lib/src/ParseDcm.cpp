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
#include <optional>

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

    std::optional<std::string> GetOptionalAttribute(const Poco::XML::Element& element, const std::string& attributeName)
    {
      if (!element.hasAttribute(attributeName))
      {
        return std::nullopt;
      }

      std::string value = element.getAttribute(attributeName);
      if (value.empty())
      {
        return std::nullopt;
      }

      return value;
    }

    std::optional<std::size_t> ParseSizeT(const std::string& value)
    {
      std::size_t parsedValue = 0;
      const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
      if (ec != std::errc())
      {
        return std::nullopt;
      }

      return parsedValue;
    }

    std::optional<std::uint32_t> ParseUint32(const std::string& value)
    {
      std::uint32_t parsedValue = 0;
      const auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsedValue);
      if (ec != std::errc())
      {
        return std::nullopt;
      }

      return parsedValue;
    }

    std::optional<std::size_t> GetOptionalSizeTAttribute(const Poco::XML::Element& element, const std::string& attributeName)
    {
      const auto value = GetOptionalAttribute(element, attributeName);
      if (!value.has_value())
      {
        return std::nullopt;
      }

      return ParseSizeT(*value);
    }

    Open3SDCM::ColorRGB DecodePackedColor(const std::uint32_t packedColor)
    {
      return {
        static_cast<std::uint8_t>((packedColor >> 16U) & 0xFFU),
        static_cast<std::uint8_t>((packedColor >> 8U) & 0xFFU),
        static_cast<std::uint8_t>(packedColor & 0xFFU)
      };
    }

    Poco::XML::Element* FindFirstDirectChildElement(Poco::XML::Node* parent, const std::string& elementName)
    {
      if (parent == nullptr)
      {
        return nullptr;
      }

      for (auto child = parent->firstChild(); child != nullptr; child = child->nextSibling())
      {
        if (child->nodeType() != Poco::XML::Node::ELEMENT_NODE)
        {
          continue;
        }

        if (child->nodeName() == elementName)
        {
          return dynamic_cast<Poco::XML::Element*>(child);
        }
      }

      return nullptr;
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

    std::vector<unsigned char> BuildCeKey(const std::map<std::string, std::string>& props, const bool scramble)
    {
      std::vector<unsigned char> key = {
        0x34, 0x90, 0x02, 0x93, 0x58, 0x2F, 0x49, 0x94,
        0x76, 0x02, 0x19, 0xDF, 0x3B, 0x56, 0x44, 0x1C
      };

      std::string ekid = "1";
      const auto ekidIt = props.find("EKID");
      if (ekidIt != props.end())
      {
        ekid = ekidIt->second;
      }

      const std::string packageHash = ComputePackageLockHash(props);
      std::vector<unsigned char> finalKey = key;
      if (ekid == "1" && !packageHash.empty())
      {
        for (const char c : packageHash)
        {
          finalKey.push_back(static_cast<unsigned char>(c));
        }
      }

      if (scramble)
      {
        std::reverse(finalKey.begin(), finalKey.end());
        for (auto& byte : finalKey)
        {
          byte ^= 0x7BU;
        }
      }

      return finalKey;
    }

    std::vector<char> DecryptBuffer(std::vector<char> data,
                                    const std::string& schema,
                                    const std::map<std::string, std::string>& props,
                                    const bool scrambleKey = false,
                                    const std::size_t truncateSize = 0)
    {
      if (schema != "CE")
      {
        if (truncateSize > 0 && data.size() > truncateSize)
        {
          data.resize(truncateSize);
        }
        return data;
      }

      const auto finalKey = BuildCeKey(props, scrambleKey);

      BF_KEY bfKey;
      BF_set_key(&bfKey, finalKey.size(), finalKey.data());

      if (data.size() % 8 != 0)
      {
        const std::size_t padding = 8 - (data.size() % 8);
        data.resize(data.size() + padding, 0);
      }

      SwapEndianness(data);

      std::vector<char> decrypted(data.size());
      for (size_t i = 0; i < data.size(); i += 8)
      {
        BF_ecb_encrypt(reinterpret_cast<unsigned char*>(&data[i]),
                       reinterpret_cast<unsigned char*>(&decrypted[i]),
                       &bfKey,
                       BF_DECRYPT);
      }

      SwapEndianness(decrypted);
      if (truncateSize > 0 && decrypted.size() > truncateSize)
      {
        decrypted.resize(truncateSize);
      }

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

              auto vertexCount = GetElemCount(BinaryNodes, "Vertices");
              const std::size_t expectedSize = vertexCount * 3 * sizeof(float);
              rawData = DecryptBuffer(std::move(rawData), schema, props, false, expectedSize);

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
    std::optional<Open3SDCM::ColorRGB> ParseFacetBaseColor(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes)
    {
      try
      {
        if (!BinaryNodes.isNull() && BinaryNodes->length() > 0)
        {
          auto binaryElement = dynamic_cast<Poco::XML::Element*>(BinaryNodes->item(0));
          if (binaryElement)
          {
            Poco::AutoPtr<Poco::XML::NodeList> facetNodes = binaryElement->getElementsByTagName("Facets");
            if (facetNodes->length() > 0)
            {
              auto facetsElement = dynamic_cast<Poco::XML::Element*>(facetNodes->item(0));
              if (facetsElement != nullptr)
              {
                const auto colorValue = GetOptionalAttribute(*facetsElement, "color");
                if (colorValue.has_value())
                {
                  const auto packedColor = ParseUint32(*colorValue);
                  if (packedColor.has_value())
                  {
                    return DecodePackedColor(*packedColor);
                  }
                }
              }
            }
          }
        }
      }
      catch (const Poco::Exception&)
      {
      }

      return std::nullopt;
    }

    std::optional<Open3SDCM::TextureCoordinate> DecodePackedTextureCoordinate(const std::uint32_t packedTextureCoordinate)
    {
      if (packedTextureCoordinate == 0xFFFFFFFFU)
      {
        return std::nullopt;
      }

      const auto decodeComponent = [](const std::uint16_t componentBits) -> float
      {
        const bool outside = (componentBits & 0x8000U) != 0;
        const std::uint16_t value = componentBits & 0x7FFFU;
        if (!outside)
        {
          return static_cast<float>(value) / 32767.0F;
        }

        return static_cast<float>(value) * (512.0F / 32767.0F) - 256.0F;
      };

      return Open3SDCM::TextureCoordinate{
        decodeComponent(static_cast<std::uint16_t>(packedTextureCoordinate & 0xFFFFU)),
        decodeComponent(static_cast<std::uint16_t>((packedTextureCoordinate >> 16U) & 0xFFFFU))
      };
    }

    std::vector<std::vector<std::size_t>> BuildVertexCornerMap(const std::vector<Open3SDCM::Triangle>& triangles,
                                                               const std::size_t vertexCount)
    {
      std::vector<std::vector<std::size_t>> cornersByVertex(vertexCount);
      for (std::size_t faceIndex = 0; faceIndex < triangles.size(); ++faceIndex)
      {
        const std::array<std::size_t, 3> faceVertices = {triangles[faceIndex].v1, triangles[faceIndex].v2, triangles[faceIndex].v3};
        for (std::size_t cornerIndex = 0; cornerIndex < faceVertices.size(); ++cornerIndex)
        {
          const auto vertexIndex = faceVertices[cornerIndex];
          if (vertexIndex >= vertexCount)
          {
            continue;
          }
          cornersByVertex[vertexIndex].push_back(faceIndex * 3 + cornerIndex);
        }
      }

      return cornersByVertex;
    }

    std::vector<std::optional<Open3SDCM::TextureCoordinate>> DecodePerVertexTextureCoordinates(
      const std::vector<char>& decryptedBytes,
      const std::size_t vertexCount,
      const std::vector<Open3SDCM::Triangle>& triangles)
    {
      const auto cornersByVertex = BuildVertexCornerMap(triangles, vertexCount);
      std::vector<std::optional<Open3SDCM::TextureCoordinate>> cornerCoordinates(triangles.size() * 3);

      std::size_t offset = 0;
      const auto readByte = [&](std::uint8_t& value) -> bool
      {
        if (offset + sizeof(value) > decryptedBytes.size())
        {
          return false;
        }

        value = static_cast<std::uint8_t>(decryptedBytes[offset]);
        offset += sizeof(value);
        return true;
      };

      const auto readUint32LE = [&](std::uint32_t& value) -> bool
      {
        if (offset + sizeof(value) > decryptedBytes.size())
        {
          return false;
        }

        const auto* bytes = reinterpret_cast<const unsigned char*>(decryptedBytes.data() + offset);
        value = static_cast<std::uint32_t>(bytes[0]) |
                (static_cast<std::uint32_t>(bytes[1]) << 8U) |
                (static_cast<std::uint32_t>(bytes[2]) << 16U) |
                (static_cast<std::uint32_t>(bytes[3]) << 24U);
        offset += sizeof(value);
        return true;
      };

      for (std::size_t vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex)
      {
        std::uint8_t flag = 0;
        if (!readByte(flag))
        {
          std::cerr << "Error: Unexpected end of UV stream while reading vertex flag" << std::endl;
          return {};
        }

        const auto& vertexCorners = cornersByVertex[vertexIndex];
        const std::size_t degree = vertexCorners.size();
        if (flag == 0)
        {
          if (degree != 0)
          {
            std::cerr << "Error: Invalid UV stream, vertex degree mismatch" << std::endl;
            return {};
          }
          continue;
        }

        std::size_t uvCount = 0;
        if (flag == 1)
        {
          uvCount = degree == 0 ? 1U : 1U;
        }
        else if (flag == degree)
        {
          uvCount = degree;
        }
        else
        {
          std::cerr << "Error: Invalid UV stream, flag " << static_cast<unsigned int>(flag)
                    << " does not match vertex degree " << degree << std::endl;
          return {};
        }

        std::vector<std::optional<Open3SDCM::TextureCoordinate>> decodedCoordinates;
        decodedCoordinates.reserve(uvCount);
        for (std::size_t uvIndex = 0; uvIndex < uvCount; ++uvIndex)
        {
          std::uint32_t packedTextureCoordinate = 0;
          if (!readUint32LE(packedTextureCoordinate))
          {
            std::cerr << "Error: Unexpected end of UV stream while reading packed coordinate" << std::endl;
            return {};
          }
          decodedCoordinates.push_back(DecodePackedTextureCoordinate(packedTextureCoordinate));
        }

        if (degree == 0)
        {
          continue;
        }

        if (flag == 1)
        {
          for (const auto cornerIndex : vertexCorners)
          {
            cornerCoordinates[cornerIndex] = decodedCoordinates.front();
          }
          continue;
        }

        for (std::size_t cornerOrdinal = 0; cornerOrdinal < degree; ++cornerOrdinal)
        {
          cornerCoordinates[vertexCorners[cornerOrdinal]] = decodedCoordinates[cornerOrdinal];
        }
      }

      return cornerCoordinates;
    }

    void ParseTextureCoordinateMetadata(Poco::XML::Element* textureDataElement,
                                        const std::string& schema,
                                        const std::map<std::string, std::string>& properties,
                                        const std::size_t vertexCount,
                                        const std::vector<Open3SDCM::Triangle>& triangles,
                                        Open3SDCM::SurfaceData& surfaceData)
    {
      if (textureDataElement == nullptr)
      {
        return;
      }

      for (auto child = textureDataElement->firstChild(); child != nullptr; child = child->nextSibling())
      {
        auto textureCoordElement = dynamic_cast<Poco::XML::Element*>(child);
        if (textureCoordElement == nullptr || textureCoordElement->tagName() != "PerVertexTextureCoord")
        {
          continue;
        }

        Open3SDCM::TextureCoordinateData textureCoordinate;
        textureCoordinate.textureCoordId = GetOptionalAttribute(*textureCoordElement, "TextureCoordId");
        textureCoordinate.textureId = GetOptionalAttribute(*textureCoordElement, "TextureId");
        textureCoordinate.key = GetOptionalAttribute(*textureCoordElement, "Key");
        if (const auto encodedByteCount = GetOptionalSizeTAttribute(*textureCoordElement, "Base64EncodedBytes"))
        {
          textureCoordinate.encodedByteCount = *encodedByteCount;
        }

        std::string base64Text = textureCoordElement->innerText();
        const std::size_t estimatedBufferSize = textureCoordinate.encodedByteCount > 0
          ? textureCoordinate.encodedByteCount
          : base64Text.size();
        auto rawData = DecodeBuffer(base64Text, estimatedBufferSize);
        rawData = DecryptBuffer(std::move(rawData),
                                schema,
                                properties,
                                textureCoordinate.key.has_value(),
                                textureCoordinate.encodedByteCount);
        textureCoordinate.cornerCoordinates = DecodePerVertexTextureCoordinates(rawData, vertexCount, triangles);

        surfaceData.textureCoordinates.push_back(std::move(textureCoordinate));
      }
    }

    void ParseTextureImages(Poco::XML::Element* textureImagesElement, Open3SDCM::SurfaceData& surfaceData)
    {
      if (textureImagesElement == nullptr)
      {
        return;
      }

      for (auto child = textureImagesElement->firstChild(); child != nullptr; child = child->nextSibling())
      {
        auto textureImageElement = dynamic_cast<Poco::XML::Element*>(child);
        if (textureImageElement == nullptr || textureImageElement->tagName() != "TextureImage")
        {
          continue;
        }

        Open3SDCM::EmbeddedTextureImage textureImage;
        textureImage.version = GetOptionalAttribute(*textureImageElement, "Version");
        textureImage.textureName = GetOptionalAttribute(*textureImageElement, "TextureName");
        textureImage.id = GetOptionalAttribute(*textureImageElement, "Id");
        textureImage.textureId = GetOptionalAttribute(*textureImageElement, "TextureId");
        textureImage.refTextureCoordId = GetOptionalAttribute(*textureImageElement, "RefTextureCoordId");
        textureImage.textureCoordSet = GetOptionalAttribute(*textureImageElement, "TextureCoordSet");
        textureImage.mimeType = std::string("image/jpeg");

        if (const auto width = GetOptionalSizeTAttribute(*textureImageElement, "Width"))
        {
          textureImage.width = *width;
        }
        if (const auto height = GetOptionalSizeTAttribute(*textureImageElement, "Height"))
        {
          textureImage.height = *height;
        }
        if (const auto bytesPerPixel = GetOptionalSizeTAttribute(*textureImageElement, "BytesPerPixel"))
        {
          textureImage.bytesPerPixel = *bytesPerPixel;
        }
        if (const auto encodedByteCount = GetOptionalSizeTAttribute(*textureImageElement, "Base64EncodedBytes"))
        {
          textureImage.encodedByteCount = *encodedByteCount;
        }

        std::string base64Text = textureImageElement->innerText();
        const std::size_t estimatedBufferSize = textureImage.encodedByteCount > 0 ? textureImage.encodedByteCount : base64Text.size();
        auto decodedBytes = DecodeBuffer(base64Text, estimatedBufferSize);
        textureImage.imageBytes.assign(decodedBytes.begin(), decodedBytes.end());

        surfaceData.textureImages.push_back(std::move(textureImage));
      }
    }

    void ParseSurfaceData(Poco::AutoPtr<Poco::XML::Document> document,
                          const std::string& schema,
                          const std::map<std::string, std::string>& properties,
                          const std::size_t vertexCount,
                          const std::vector<Open3SDCM::Triangle>& triangles,
                          Open3SDCM::SurfaceData& surfaceData)
    {
      if (document.isNull())
      {
        return;
      }

      auto* rootElement = document->documentElement();
      if (rootElement == nullptr)
      {
        return;
      }

      auto* textureDataElement = FindFirstDirectChildElement(rootElement, "TextureData2");
      if (textureDataElement == nullptr)
      {
        ParseTextureImages(FindFirstDirectChildElement(rootElement, "TextureImages"), surfaceData);
        return;
      }

      ParseTextureCoordinateMetadata(textureDataElement, schema, properties, vertexCount, triangles, surfaceData);
      ParseTextureImages(FindFirstDirectChildElement(textureDataElement, "TextureImages"), surfaceData);
    }

    bool EnsureParentDirectoryExists(const fs::path& outputPath)
    {
      if (!outputPath.has_parent_path())
      {
        return true;
      }

      std::error_code errorCode;
      fs::create_directories(outputPath.parent_path(), errorCode);
      return !errorCode;
    }

    float NormalizeColorChannel(const std::uint8_t value)
    {
      return static_cast<float>(value) / 255.0F;
    }

    const Open3SDCM::TextureCoordinateData* FindTextureCoordinateDataById(
      const Open3SDCM::SurfaceData& surfaceData,
      const std::optional<std::string>& textureCoordId,
      const std::optional<std::string>& textureId)
    {
      const auto matchesCoordId = [&](const Open3SDCM::TextureCoordinateData& candidate) -> bool
      {
        return textureCoordId.has_value() && candidate.textureCoordId == textureCoordId && candidate.HasDecodedCoordinates();
      };
      const auto matchesTextureId = [&](const Open3SDCM::TextureCoordinateData& candidate) -> bool
      {
        return textureId.has_value() && candidate.textureId == textureId && candidate.HasDecodedCoordinates();
      };

      for (const auto& candidate : surfaceData.textureCoordinates)
      {
        if (matchesCoordId(candidate))
        {
          return &candidate;
        }
      }
      for (const auto& candidate : surfaceData.textureCoordinates)
      {
        if (matchesTextureId(candidate))
        {
          return &candidate;
        }
      }
      for (const auto& candidate : surfaceData.textureCoordinates)
      {
        if (candidate.HasDecodedCoordinates())
        {
          return &candidate;
        }
      }

      return nullptr;
    }

    struct ExportTextureBinding
    {
      const Open3SDCM::TextureCoordinateData* coordinates{nullptr};
      const Open3SDCM::EmbeddedTextureImage* image{nullptr};
    };

    ExportTextureBinding FindTextureBinding(const Open3SDCM::SurfaceData& surfaceData)
    {
      for (const auto& textureImage : surfaceData.textureImages)
      {
        if (textureImage.imageBytes.empty())
        {
          continue;
        }

        const auto* coordinates = FindTextureCoordinateDataById(surfaceData,
                                                                textureImage.refTextureCoordId,
                                                                textureImage.textureId);
        if (coordinates != nullptr)
        {
          return {coordinates, &textureImage};
        }
      }

      const auto* coordinates = FindTextureCoordinateDataById(surfaceData, std::nullopt, std::nullopt);
      if (coordinates != nullptr)
      {
        return {coordinates, surfaceData.textureImages.empty() ? nullptr : &surfaceData.textureImages.front()};
      }

      if (!surfaceData.textureImages.empty())
      {
        return {nullptr, &surfaceData.textureImages.front()};
      }

      return {};
    }

    std::string GuessTextureExtension(const Open3SDCM::EmbeddedTextureImage& textureImage)
    {
      if (textureImage.mimeType.has_value())
      {
        if (*textureImage.mimeType == "image/jpeg" || *textureImage.mimeType == "image/jpg")
        {
          return ".jpg";
        }
        if (*textureImage.mimeType == "image/png")
        {
          return ".png";
        }
      }

      return ".bin";
    }

    bool WriteTextureFile(const fs::path& outputPath, const std::vector<std::uint8_t>& bytes)
    {
      std::ofstream output(outputPath, std::ios::binary);
      if (!output)
      {
        return false;
      }

      output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
      return output.good();
    }

    bool ExportPly(const fs::path& outputPath,
                   const std::vector<float>& vertices,
                   const std::vector<Open3SDCM::Triangle>& triangles,
                   const Open3SDCM::SurfaceData& surfaceData)
    {
      if (!EnsureParentDirectoryExists(outputPath))
      {
        return false;
      }

      std::ofstream output(outputPath);
      if (!output)
      {
        return false;
      }

      const bool hasColor = surfaceData.baseColor.has_value();
      output << "ply\n";
      output << "format ascii 1.0\n";
      output << "element vertex " << (vertices.size() / 3) << "\n";
      output << "property float x\n";
      output << "property float y\n";
      output << "property float z\n";
      if (hasColor)
      {
        output << "property uchar red\n";
        output << "property uchar green\n";
        output << "property uchar blue\n";
      }
      output << "element face " << triangles.size() << "\n";
      output << "property list uchar int vertex_indices\n";
      output << "end_header\n";

      for (std::size_t vertexIndex = 0; vertexIndex < vertices.size() / 3; ++vertexIndex)
      {
        output << vertices[vertexIndex * 3 + 0] << ' '
               << vertices[vertexIndex * 3 + 1] << ' '
               << vertices[vertexIndex * 3 + 2];
        if (hasColor)
        {
          output << ' ' << static_cast<unsigned int>(surfaceData.baseColor->r)
                 << ' ' << static_cast<unsigned int>(surfaceData.baseColor->g)
                 << ' ' << static_cast<unsigned int>(surfaceData.baseColor->b);
        }
        output << '\n';
      }

      for (const auto& triangle : triangles)
      {
        output << "3 " << triangle.v1 << ' ' << triangle.v2 << ' ' << triangle.v3 << '\n';
      }

      return output.good();
    }

    bool ExportObj(const fs::path& outputPath,
                   const std::vector<float>& vertices,
                   const std::vector<Open3SDCM::Triangle>& triangles,
                   const Open3SDCM::SurfaceData& surfaceData)
    {
      if (!EnsureParentDirectoryExists(outputPath))
      {
        return false;
      }

      const ExportTextureBinding textureBinding = FindTextureBinding(surfaceData);
      const bool hasTextureCoordinates = textureBinding.coordinates != nullptr &&
        textureBinding.coordinates->cornerCoordinates.size() == triangles.size() * 3;
      const bool hasTextureImage = textureBinding.image != nullptr && !textureBinding.image->imageBytes.empty();
      const bool writeMaterial = surfaceData.baseColor.has_value() || hasTextureImage;

      const fs::path materialPath = outputPath.parent_path() / (outputPath.stem().string() + ".mtl");
      fs::path texturePath;
      if (hasTextureImage)
      {
        texturePath = outputPath.parent_path() /
          (outputPath.stem().string() + "_texture0" + GuessTextureExtension(*textureBinding.image));
        if (!WriteTextureFile(texturePath, textureBinding.image->imageBytes))
        {
          return false;
        }
      }

      if (writeMaterial)
      {
        std::ofstream materialOutput(materialPath);
        if (!materialOutput)
        {
          return false;
        }

        materialOutput << "newmtl material0\n";
        if (surfaceData.baseColor.has_value())
        {
          materialOutput << "Kd "
                         << NormalizeColorChannel(surfaceData.baseColor->r) << ' '
                         << NormalizeColorChannel(surfaceData.baseColor->g) << ' '
                         << NormalizeColorChannel(surfaceData.baseColor->b) << "\n";
        }
        else
        {
          materialOutput << "Kd 0.800000 0.800000 0.800000\n";
        }
        materialOutput << "illum 2\n";
        if (hasTextureImage)
        {
          materialOutput << "map_Kd " << texturePath.filename().string() << "\n";
        }
        if (!materialOutput.good())
        {
          return false;
        }
      }

      std::ofstream output(outputPath);
      if (!output)
      {
        return false;
      }

      if (writeMaterial)
      {
        output << "mtllib " << materialPath.filename().string() << "\n";
      }
      output << "o mesh\n";
      if (writeMaterial)
      {
        output << "usemtl material0\n";
      }

      for (std::size_t vertexIndex = 0; vertexIndex < vertices.size() / 3; ++vertexIndex)
      {
        output << "v "
               << vertices[vertexIndex * 3 + 0] << ' '
               << vertices[vertexIndex * 3 + 1] << ' '
               << vertices[vertexIndex * 3 + 2] << "\n";
      }

      if (hasTextureCoordinates)
      {
        for (const auto& cornerCoordinate : textureBinding.coordinates->cornerCoordinates)
        {
          const auto resolvedCoordinate = cornerCoordinate.value_or(Open3SDCM::TextureCoordinate{});
          // The decoded CE texture coordinates use the image's top-left origin,
          // while OBJ consumers expect V to be measured from the bottom edge.
          const float exportedV = cornerCoordinate.has_value() ? 1.0F - resolvedCoordinate.v : resolvedCoordinate.v;
          output << "vt " << resolvedCoordinate.u << ' ' << exportedV << "\n";
        }
      }

      for (std::size_t faceIndex = 0; faceIndex < triangles.size(); ++faceIndex)
      {
        const auto& triangle = triangles[faceIndex];
        if (hasTextureCoordinates)
        {
          output << "f "
                 << (triangle.v1 + 1) << '/' << (faceIndex * 3 + 1) << ' '
                 << (triangle.v2 + 1) << '/' << (faceIndex * 3 + 2) << ' '
                 << (triangle.v3 + 1) << '/' << (faceIndex * 3 + 3) << "\n";
        }
        else
        {
          output << "f "
                 << (triangle.v1 + 1) << ' '
                 << (triangle.v2 + 1) << ' '
                 << (triangle.v3 + 1) << "\n";
        }
      }

      return output.good();
    }

  }// namespace detail

  void DCMParser::ParseDCM(const fs::path& filePath)
  {
    m_Vertices.clear();
    m_Triangles.clear();
    m_SurfaceData = {};

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

      detail::ParseSurfaceData(document, schema, properties, m_Vertices.size() / 3, m_Triangles, m_SurfaceData);
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

      m_SurfaceData.baseColor = detail::ParseFacetBaseColor(BinaryNodes);

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

    if (format == "ply")
    {
      const bool exported = detail::ExportPly(outputPath, m_Vertices, m_Triangles, m_SurfaceData);
      if (!exported)
      {
        fmt::print("Error: Failed to export mesh to PLY\n");
        return false;
      }

      fmt::print("Successfully exported mesh to: {}\n", outputPath.string());
      return true;
    }

    if (format == "obj")
    {
      const bool exported = detail::ExportObj(outputPath, m_Vertices, m_Triangles, m_SurfaceData);
      if (!exported)
      {
        fmt::print("Error: Failed to export mesh to OBJ\n");
        return false;
      }

      fmt::print("Successfully exported mesh to: {}\n", outputPath.string());
      return true;
    }

    aiScene* scene = new aiScene();
    scene->mRootNode = new aiNode();

    scene->mNumMeshes = 1;
    scene->mMeshes = new aiMesh*[1];
    aiMesh* mesh = new aiMesh();
    scene->mMeshes[0] = mesh;
    scene->mRootNode->mNumMeshes = 1;
    scene->mRootNode->mMeshes = new unsigned int[1];
    scene->mRootNode->mMeshes[0] = 0;

    mesh->mNumVertices = numVertices;
    mesh->mVertices = new aiVector3D[mesh->mNumVertices];
    for (size_t i = 0; i < mesh->mNumVertices; ++i)
    {
      mesh->mVertices[i].x = m_Vertices[i * 3 + 0];
      mesh->mVertices[i].y = m_Vertices[i * 3 + 1];
      mesh->mVertices[i].z = m_Vertices[i * 3 + 2];
    }

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

    scene->mNumMaterials = 1;
    scene->mMaterials = new aiMaterial*[1];
    scene->mMaterials[0] = new aiMaterial();
    mesh->mMaterialIndex = 0;

    std::string formatId = format;
    if (format == "stl") formatId = "stl";
    else if (format == "stlb") formatId = "stlb";

    Assimp::Exporter exporter;
    aiReturn result = exporter.Export(scene, formatId, outputPath.string(), 0);

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
