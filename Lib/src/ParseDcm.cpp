//
// Created by Romain Nosenzo on 15/02/2025.
//

#include "ParseDcm.h"
#include "definitions.h"

#include "boost/dynamic_bitset.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

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
#include <assimp/postprocess.h>
#include <assimp/scene.h>

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
      while (std::getline(ss, item, ';'))
      {
        if (!item.empty()) items.insert(item);
      }

      if (items.empty()) return "";

      std::string canonical;
      for (const auto& i: items)
      {
        canonical += i + ";";
      }

      unsigned char digest[MD5_DIGEST_LENGTH];
      MD5((unsigned char*)canonical.c_str(), canonical.length(), digest);

      std::stringstream hex;
      hex << std::hex << std::uppercase;
      for (int i = 0; i < MD5_DIGEST_LENGTH; ++i)
        hex << std::setw(2) << std::setfill('0') << (int)digest[i];

      return hex.str();
    }

    void SwapEndianness(std::vector<char>& data)
    {
      for (size_t i = 0; i + 8 <= data.size(); i += 8)
      {
        // Swap two 32-bit integers from LE to BE (or vice versa)
        // [0 1 2 3] [4 5 6 7] -> [3 2 1 0] [7 6 5 4]
        std::swap(data[i + 0], data[i + 3]);
        std::swap(data[i + 1], data[i + 2]);

        std::swap(data[i + 4], data[i + 7]);
        std::swap(data[i + 5], data[i + 6]);
      }
    }

    std::vector<char> DecryptBuffer(std::vector<char>& data, const std::string& schema, const std::map<std::string, std::string>& props, const std::vector<unsigned char>& customKey = {})
    {
      if (schema != "CE") return data;

      // Use custom key if provided, otherwise use hardcoded Blowfish key: 0123456789abcdef (16 bytes / 128 bits)
      std::vector<unsigned char> key;
      if (!customKey.empty())
      {
        key = customKey;
      }
      else
      {
        key = {
          0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,// "01234567"
          0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66 // "89abcdef"
        };
      }

      // Initialize Blowfish key
      BF_KEY bfKey;
      BF_set_key(&bfKey, key.size(), key.data());

      // Pad to 8 bytes for ECB mode
      size_t originalSize = data.size();
      size_t padding = 0;
      if (data.size() % 8 != 0)
      {
        padding = 8 - (data.size() % 8);
        data.resize(data.size() + padding, 0);
      }

      // Decrypt using Blowfish in ECB mode
      std::vector<char> decrypted(data.size());
      for (size_t i = 0; i < data.size(); i += 8)
      {
        BF_ecb_encrypt((unsigned char*)&data[i], (unsigned char*)&decrypted[i], &bfKey, BF_DECRYPT);
      }

      // Remove padding if it was added
      if (padding > 0)
      {
        decrypted.resize(originalSize);
      }

      return decrypted;
    }

    // Function to derive keys based on file properties
    std::vector<std::vector<unsigned char>> DeriveBlowfishKeys(const std::map<std::string, std::string>& props)
    {
      std::vector<std::vector<unsigned char>> derivedKeys;

      // Base key from executable findings
      std::vector<unsigned char> baseKey = {
        0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,// "01234567"
        0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66 // "89abcdef"
      };

      // Get EKID from properties (default to 1)
      uint32_t ekid = 1;
      if (props.find("EKID") != props.end())
      {
        auto [ptr, ec] = std::from_chars(props.at("EKID").data(), props.at("EKID").data() + props.at("EKID").size(), ekid);
      }

      // Get PackageLockList hash
      std::string packageLockHash = "";
      if (props.find("PackageLockList") != props.end())
      {
        packageLockHash = ComputePackageLockHash(props);
      }

      // Derive keys using different patterns based on schemaCE_findings.md

      // 1. Base key (original)
      derivedKeys.push_back(baseKey);

      // 2. EKID-based key derivation: base_key XOR ekid_pattern
      {
        std::vector<unsigned char> derivedKey = baseKey;
        for (size_t i = 0; i < derivedKey.size(); ++i)
        {
          derivedKey[i] ^= static_cast<unsigned char>(ekid ^ (i % 256));
        }
        derivedKeys.push_back(derivedKey);
      }

      // 3. EKID-based key derivation: base_key with EKID in first byte
      {
        std::vector<unsigned char> derivedKey = baseKey;
        derivedKey[0] = static_cast<unsigned char>(ekid);
        derivedKeys.push_back(derivedKey);
      }

      // 4. PackageLockList hash-based derivation
      if (!packageLockHash.empty())
      {
        std::vector<unsigned char> derivedKey = baseKey;
        for (size_t i = 0; i < derivedKey.size(); ++i)
        {
          size_t hashIndex = i % packageLockHash.size();
          uint8_t hashByte = static_cast<uint8_t>(packageLockHash[hashIndex]);
          derivedKey[i] ^= hashByte;
        }
        derivedKeys.push_back(derivedKey);
      }

      // 5. Combined EKID and hash derivation
      if (!packageLockHash.empty())
      {
        std::vector<unsigned char> derivedKey = baseKey;
        for (size_t i = 0; i < derivedKey.size(); ++i)
        {
          size_t hashIndex = i % packageLockHash.size();
          uint8_t hashByte = static_cast<uint8_t>(packageLockHash[hashIndex]);
          uint8_t ekidByte = static_cast<uint8_t>(ekid ^ (i % 256));
          derivedKey[i] ^= (hashByte ^ ekidByte);
        }
        derivedKeys.push_back(derivedKey);
      }

      // 6. Key with EKID (1) incorporated (from original patterns)
      derivedKeys.push_back({0x31, 0x30, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66});

      // 7. Key based on file properties (EKID=1) (from original patterns)
      derivedKeys.push_back({0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00});

      return derivedKeys;
    }

    // Function to try different key patterns and find the correct one
    std::vector<unsigned char> FindCorrectBlowfishKey(const std::vector<char>& encryptedData, const std::map<std::string, std::string>& props, uint32_t expectedChecksum)
    {
      // First try derived keys based on file properties
      auto derivedKeys = DeriveBlowfishKeys(props);

      for (const auto& key: derivedKeys)
      {
        // Test this key
        std::vector<char> testData = encryptedData;
        BF_KEY bfKey;
        BF_set_key(&bfKey, key.size(), key.data());

        // Pad to 8 bytes for ECB mode
        size_t padding = 0;
        if (testData.size() % 8 != 0)
        {
          padding = 8 - (testData.size() % 8);
          testData.resize(testData.size() + padding, 0);
        }

        // Decrypt
        std::vector<char> decrypted(testData.size());
        for (size_t i = 0; i < testData.size(); i += 8)
        {
          BF_ecb_encrypt((unsigned char*)&testData[i], (unsigned char*)&decrypted[i], &bfKey, BF_DECRYPT);
        }

        if (padding > 0)
        {
          decrypted.resize(testData.size() - padding);
        }

        // Check if this produces the expected checksum
        Poco::Checksum checksum(Poco::Checksum::TYPE_ADLER32);
        checksum.update(decrypted.data(), decrypted.size());
        uint32_t adler = checksum.checksum();
        uint32_t swappedAdler = ((adler & 0xFF000000) >> 24) | ((adler & 0x00FF0000) >> 8) | ((adler & 0x0000FF00) << 8) | ((adler & 0x000000FF) << 24);

        if (swappedAdler == expectedChecksum)
        {
          fmt::print("Found correct derived key! Checksum matches.");
          return key;
        }
      }

      // If derived keys don't work, try the original hardcoded patterns
      std::vector<std::vector<unsigned char>> keyPatterns = {
        // Original key from findings
        {0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66},
        // Sequential pattern
        {0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F},
        // All zeros
        {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
        // All ones
        {0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01},
        // Alternating pattern
        {0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA},
        // Reverse of original key
        {0x66, 0x65, 0x64, 0x63, 0x62, 0x61, 0x39, 0x38, 0x37, 0x36, 0x35, 0x34, 0x33, 0x32, 0x31, 0x30},
        // All 0x1C (from original code)
        {0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C, 0x1C},
        // Pattern from original code
        {0x1C, 0x8D, 0x10, 0xB1, 0xF7, 0xF5, 0xB8, 0xFE, 0x89, 0x01, 0x60, 0xFB, 0xE4, 0x53, 0x60, 0xAC},
        // Simple ascending
        {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10},
        // XOR pattern
        {0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55},
        // Key based on file properties (EKID=1)
        {0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}};

      for (const auto& key: keyPatterns)
      {
        // Test this key
        std::vector<char> testData = encryptedData;
        BF_KEY bfKey;
        BF_set_key(&bfKey, key.size(), key.data());

        // Pad to 8 bytes for ECB mode
        size_t padding = 0;
        if (testData.size() % 8 != 0)
        {
          padding = 8 - (testData.size() % 8);
          testData.resize(testData.size() + padding, 0);
        }

        // Decrypt
        std::vector<char> decrypted(testData.size());
        for (size_t i = 0; i < testData.size(); i += 8)
        {
          BF_ecb_encrypt((unsigned char*)&testData[i], (unsigned char*)&decrypted[i], &bfKey, BF_DECRYPT);
        }

        if (padding > 0)
        {
          decrypted.resize(testData.size() - padding);
        }

        // Check if this produces the expected checksum
        Poco::Checksum checksum(Poco::Checksum::TYPE_ADLER32);
        checksum.update(decrypted.data(), decrypted.size());
        uint32_t adler = checksum.checksum();
        uint32_t swappedAdler = ((adler & 0xFF000000) >> 24) | ((adler & 0x00FF0000) >> 8) | ((adler & 0x0000FF00) << 8) | ((adler & 0x000000FF) << 24);

        if (swappedAdler == expectedChecksum)
        {
          fmt::print("Found correct key! Checksum matches.");
          return key;
        }
      }

      fmt::print("No matching key found in common patterns.");
      return {};// Return empty vector if no key found
    }


  std::vector<float> ParseVertices(Poco::AutoPtr<Poco::XML::NodeList> BinaryNodes, const std::string& schema, const std::map<std::string, std::string>& props, const std::vector<unsigned char>& customKey = {}, bool keyDiscoveryMode = false)
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

            // If key discovery mode is enabled, try to find the correct key
            if (keyDiscoveryMode && schema == "CE")
            {
              fmt::print("Key discovery mode enabled. Searching for correct Blowfish key...\n");
              auto foundKey = detail::FindCorrectBlowfishKey(rawData, props, 330137282);// Expected checksum for this file
              if (!foundKey.empty())
              {
                rawData = DecryptBuffer(rawData, schema, props, foundKey);
                fmt::print("Using discovered key for decryption.\n");
              }
              else
              {
                rawData = DecryptBuffer(rawData, schema, props, customKey);
                fmt::print("No key found, using default/custom key.\n");
              }
            }
            else
            {
              rawData = DecryptBuffer(rawData, schema, props, customKey);
            }

            auto vertexCount = GetElemCount(BinaryNodes, "Vertices");
            std::size_t expectedSize = vertexCount * 3 * sizeof(float);

            // Truncate to expected size
            if (rawData.size() > expectedSize)
            {
              rawData.resize(expectedSize);
            }

            // Verify checksum for CE schema
            if (schema == "CE" && caElement->hasAttribute("check_value"))
            {
              std::string checkValueStr = caElement->getAttribute("check_value");
              uint32_t checkValue = 0;
              auto [ptr, ec] = std::from_chars(checkValueStr.data(), checkValueStr.data() + checkValueStr.size(), checkValue);

              if (ec == std::errc())
              {
                Poco::Checksum checksum(Poco::Checksum::TYPE_ADLER32);
                checksum.update(rawData.data(), rawData.size());
                uint32_t adler = checksum.checksum();

                // Swap endianness to match reference implementation
                uint32_t swappedAdler = ((adler & 0xFF000000) >> 24) | ((adler & 0x00FF0000) >> 8) | ((adler & 0x0000FF00) << 8) | ((adler & 0x000000FF) << 24);

                if (swappedAdler != checkValue)
                {
                  fmt::print("Error: Checksum mismatch! Expected: {}, Calculated: {} (Swapped: {})\n", checkValue, adler, swappedAdler);
                  fmt::print("Error: Decryption key might be incorrect.\n");
                }
                else
                {
                  fmt::print("Checksum verified. Key is correct.\n");
                }
              }
            }

            // Reinterpret the raw bytes as floats
            auto floatPtr = reinterpret_cast<const float*>(rawData.data());
            std::size_t floatCount = vertexCount * 3;

            // Ensure we don't read past buffer
            if (floatCount * sizeof(float) > rawData.size())
            {
              std::cerr << "Error: Decrypted buffer too small for vertex count" << std::endl;
              return {};
            }

            std::vector<float> floatData(floatPtr, floatPtr + floatCount);

            // Debug: Print first few vertices
            if (floatData.size() >= 9)
            {
              std::cout << "First 3 vertices:" << std::endl;
              for (int i = 0; i < 3; ++i)
              {
                std::cout << "  v" << i << ": (" << floatData[i * 3] << ", " << floatData[i * 3 + 1] << ", " << floatData[i * 3 + 2] << ")" << std::endl;
              }
            }

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

    const auto requireBytes = [&](size_t bytes) -> bool {
      if (commandOffset + bytes > rawData.size())
      {
        std::cerr << "Warning: Facet buffer underrun while reading geometry data" << std::endl;
        return false;
      }
      return true;
    };

    // Helper lambda to append a face
    auto appendFace = [&Triangles](size_t v1, size_t v2, size_t v3) {
      Triangles.push_back({v1, v2, v3});
      // Debug: log first few faces
      if (Triangles.size() <= 10)
      {
        std::cout << "Face " << (Triangles.size() - 1) << ": (" << v1 << ", " << v2 << ", " << v3 << ")" << std::endl;
      }
    };

    // Helper to read command
    auto readOp = [&rawData, &commandOffset, &requireBytes]() -> uint8_t {
      if (!requireBytes(1))
      {
        return 0;
      }
      uint8_t result = static_cast<uint8_t>(rawData[commandOffset]) & FACET_COMMAND_MASK;
      commandOffset++;
      return result;
    };

    // Helper to read int16 (note: uses 4-byte alignment/padding)
    // Returns absolute vertex index (handles negative indices as relative to vertex_offset)
    auto read16 = [&rawData, &commandOffset, &vertexOffset]() -> size_t {
      int16_t result;
      std::memcpy(&result, &rawData[commandOffset], sizeof(int16_t));
      commandOffset += 4;// 4-byte alignment (2 bytes data + 2 bytes padding)
      // Handle negative indices as relative to vertex_offset
      if (result < 0)
      {
        // Use signed arithmetic to avoid underflow: vertexOffset - abs(result)
        int64_t signedOffset = static_cast<int64_t>(vertexOffset);
        int64_t signedResult = static_cast<int64_t>(result);
        int64_t absoluteIndex = signedOffset + signedResult;// result is negative
        if (absoluteIndex < 0)
        {
          std::cerr << "ERROR: Negative vertex index computed: offset=" << vertexOffset
                    << ", result=" << result << ", absolute=" << absoluteIndex << std::endl;
          return 0;
        }
        return static_cast<size_t>(absoluteIndex);
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
      if (result < 0)
      {
        // Use signed arithmetic to avoid underflow: vertexOffset - abs(result)
        int64_t signedOffset = static_cast<int64_t>(vertexOffset);
        int64_t signedResult = static_cast<int64_t>(result);
        int64_t absoluteIndex = signedOffset + signedResult;// result is negative
        if (absoluteIndex < 0)
        {
          std::cerr << "ERROR: Negative vertex index computed: offset=" << vertexOffset
                    << ", result=" << result << ", absolute=" << absoluteIndex << std::endl;
          return 0;
        }
        return static_cast<size_t>(absoluteIndex);
      }
      return static_cast<size_t>(result);
    };

    // Helper for restart operation
    auto restart = [&](size_t vid0, size_t vid1, size_t vid2) {
      edgeQueue.clear();
      appendFace(vid0, vid1, vid2);
      edgeQueue.push_back({vid0, vid1});
      edgeQueue.push_back({vid1, vid2});
      edgeQueue.push_back({vid2, vid0});// Fixed: was (vid2, vid1), should close the triangle with vid0
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
    while (commandOffset < rawData.size())
    {
      uint8_t command = readOp();

      switch (command)
      {
        case 0: {// op0
          auto current = edgeQueue.front();
          edgeQueue.pop_front();
          appendFace(current.first, vertexOffset, current.second);
          edgeQueue.push_back({current.first, vertexOffset});
          edgeQueue.push_back({vertexOffset, current.second});
          vertexOffset++;
          break;
        }
        case 1: {// op1
          auto current = edgeQueue.front();
          edgeQueue.pop_front();
          auto previous = edgeQueue.back();
          edgeQueue.pop_back();
          appendFace(current.first, previous.first, current.second);
          edgeQueue.push_back({previous.first, current.second});
          break;
        }
        case 2: {// op2
          auto current = edgeQueue.front();
          edgeQueue.pop_front();
          auto next = edgeQueue.front();
          edgeQueue.pop_front();
          appendFace(current.first, next.second, current.second);
          edgeQueue.push_back({current.first, next.second});
          break;
        }
        case 3: {// op3 - rotate
          auto front = edgeQueue.front();
          edgeQueue.pop_front();
          edgeQueue.push_back(front);
          break;
        }
        case 4: {// op4
          restart(vertexOffset, vertexOffset + 1, vertexOffset + 2);
          vertexOffset += 3;
          break;
        }
        case 5: {// op5
          restart(read16(), read16(), read16());
          break;
        }
        case 6: {// op6
          restart(read32(), read32(), read32());
          break;
        }
        case 7: {// op7
          absolute(read16());
          break;
        }
        case 8: {// op8
          absolute(read32());
          break;
        }
        case 9: {// op9
          auto current = edgeQueue.front();
          edgeQueue.pop_front();
          if (edgeQueue.size() > 1)
          {
            if (edgeQueue.back().first == edgeQueue.front().first)
            {
              edgeQueue.pop_back();
            }
            else if (edgeQueue.back().second == current.first && edgeQueue.back().first == current.second)
            {
              edgeQueue.pop_back();
            }
            else
            {
              edgeQueue.back().second = edgeQueue.front().second;
            }
          }
          break;
        }
        case 10: {// op10
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
      std::cout << "Version: " << version << std::endl;
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
      std::cout << "Schema: " << schema << std::endl;
    }

    std::map<std::string, std::string> properties;
    if (Poco::AutoPtr<Poco::XML::NodeList> propertyNodes = document->getElementsByTagName("Property");
        propertyNodes->length() > 0)
    {
      for (unsigned long i = 0; i < propertyNodes->length(); ++i)
      {
        auto propertyElement = dynamic_cast<Poco::XML::Element*>(propertyNodes->item(i));
        if (propertyElement)
        {
          std::string name = propertyElement->getAttribute("name");
          std::string value = propertyElement->getAttribute("value");
          if (!name.empty())
          {
            properties[name] = value;
          }
        }
      }

      if (properties.count("SourceApp"))
      {
        std::cout << "SourceApp: " << properties["SourceApp"] << std::endl;
      }
    }

    if (Poco::AutoPtr<Poco::XML::NodeList> GeometryBinaryNodes = document->getElementsByTagName("Binary_data");
        GeometryBinaryNodes->length() > 0)
    {
      auto GeometryBinaryElement = dynamic_cast<Poco::XML::Element*>(GeometryBinaryNodes->item(0));
      std::string GeometryBinary = GeometryBinaryElement->getAttribute("value");
      std::cout << "GeometryBinary: " << GeometryBinary << std::endl;
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
    m_Vertices = detail::ParseVertices(BinaryNodes, schema, properties, m_CustomDecryptionKey, m_KeyDiscoveryMode);
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
    if (m_Triangles[i].v1 >= numVertices || m_Triangles[i].v2 >= numVertices || m_Triangles[i].v3 >= numVertices)
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
  else if (format == "obj")
    formatId = "obj";
  else if (format == "ply")
    formatId = "ply";
  else if (format == "stlb")
    formatId = "stlb";// STL binary

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
