//
// Created by Romain Nosenzo on 15/02/2025.
//

#pragma once
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

namespace fs = std::filesystem;

namespace Open3SDCM
{
    void ParseDCM(const fs::path& filePath)
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
          Poco::XML::Element* schemaElement = static_cast<Poco::XML::Element*>(schemaNodes->item(0));
          std::string schema = schemaElement->getAttribute("Schema");
          std::cout << "Schema: " << schema << std::endl;
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
}// namespace Open3SDCM
