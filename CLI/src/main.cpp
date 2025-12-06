

//Boost
#include "boost/algorithm/string.hpp"
#include "boost/program_options.hpp"

//FMT
#include "fmt/compile.h"
#include "fmt/format.h"
#include "fmt/std.h"
// STL
#include <filesystem>
#include <fstream>

// Spd Log
#include "spdlog/sinks/basic_file_sink.h"


// POCO
#include "Poco/Path.h"
#include "Poco/Zip/Decompress.h"
#include "Poco/Zip/ZipArchive.h"


#include "ParseDcm.h"

namespace po = boost::program_options;
namespace fs = std::filesystem;

constexpr std::array AcceptedDCMExtensions = {".dcm", ".DCM"};

namespace internal
{
  std::vector<std::filesystem::path> PopulateFiles(const std::filesystem::path& iDir)
  {
    std::vector<std::filesystem::path> AllInFiles;
    fmt::print("Looking for dir ...\n");
    for (const fs::directory_entry& dir_entry:
         fs::recursive_directory_iterator(iDir))
    {

      if (!dir_entry.path().filename().string().starts_with('.'))
      {
        fmt::print("Found file extension: {}\n", dir_entry.path().extension().string());
        if(fs::is_regular_file(dir_entry.path()) &&
          std::any_of(AcceptedDCMExtensions.begin(), AcceptedDCMExtensions.end(),
            [&](const auto & accepted_extension) {
              return accepted_extension == dir_entry.path().extension().string();
            }))
        {
            AllInFiles.push_back(dir_entry.path());
        }

      }
    }
    return AllInFiles;
  }


  std::filesystem::path UnzipFile(const std::filesystem::path& inputpath,
                                  const std::filesystem::path& outputpath)
  {

    std::ifstream inp(inputpath, std::ios::binary);

    Poco::Path Output(std::filesystem::absolute(outputpath).string(), Poco::Path::PATH_NATIVE);
    Poco::Zip::Decompress dec(inp, Output);

    std::string Filename;
    auto ZipInfo = dec.decompressAllFiles();
    //Get The largest file
    auto BiggestFile = std::max_element(ZipInfo.fileInfoBegin(), ZipInfo.fileInfoEnd(), [&outputpath](const std::pair<std::string, Poco::Zip::ZipFileInfo>& file1, const std::pair<std::string, Poco::Zip::ZipFileInfo>& file2) -> bool {
      std::filesystem::path tmpPath1 = outputpath / std::filesystem::path(file1.second.getFileName());
      std::filesystem::path tmpPath2 = outputpath / std::filesystem::path(file2.second.getFileName());
      return std::filesystem::file_size(tmpPath1) < std::filesystem::file_size(tmpPath2);
    });
    std::filesystem::path LargestFilePath = outputpath / std::filesystem::path(BiggestFile->second.getFileName());


    return LargestFilePath;
  }
} // namespace internal

int main(int argc, const char** argv)
{

  // Declare the supported options.
  po::options_description desc("Allowed options");
  desc.add_options()
    ("help", "produce help message")
        ("action", po::value<std::string>(), "what to do")
          ("input_dir,i", po::value<std::filesystem::path>(), "input directory")
            ("output_dir,o", po::value<std::filesystem::path>(), "output directory")
              ("format,f", po::value<std::string>(), "output format stl,ply,obj")
                  ;

  po::variables_map vm;
  po::store(po::parse_command_line(argc, argv, desc), vm);
  po::notify(vm);

  if (vm.count("help"))
  {
    std::stringstream ss;
    ss << desc;
    fmt::print("Help:\n{}\n", ss.str());
    return 1;
  }
  std::string OutputFormat("stl");
  if (vm.count("format"))
  {
    OutputFormat=vm["format"].as<std::string>();
  }
  fmt::print("Output Format Mode {}\n", OutputFormat);
  std::filesystem::path InputDir;
  std::vector<std::filesystem::path> AllInFiles;
  if (vm.count("input_dir"))
  {
    InputDir = vm["input_dir"].as<std::filesystem::path>();
    fmt::print("inputdir {}\n", InputDir.string());
    if (fs::exists(InputDir))
    {
      AllInFiles = internal::PopulateFiles(InputDir);
    }
    else
    {
      fmt::print("/!\\ CANNOT FIND inputdir {}\n", InputDir.string());
    }
    fmt::print("Found {} files \n", AllInFiles.size());
  }

  std::filesystem::path OutputDir;
  if (vm.count("output_dir"))
  {
    OutputDir = vm["output_dir"].as<std::filesystem::path>();

    auto timestamp = std::format("{:%Y-%m-%d-%H-%M-%S}", std::chrono::system_clock::now());
    OutputDir /= timestamp;

    bool CreateDir = fs::create_directories(OutputDir);
    if (CreateDir)
    {
      fmt::print("output_dir {} Succesfully created\n", OutputDir.string());
    }
    else
    {
      fmt::print("output_dir {}\n", OutputDir.string());
    }
  }

  for (const auto& inputFile : AllInFiles)
  {
    Open3SDCM::DCMParser Parser;
    Parser.ParseDCM(inputFile);

    fmt::print("Parsed {} vertices and {} triangles from {}\n",
               Parser.m_Vertices.size() / 3,
               Parser.m_Triangles.size(),
               inputFile.filename().string());

    // Generate output filename
    std::string outputFilename = inputFile.stem().string() + "." + OutputFormat;
    std::filesystem::path outputFilePath = OutputDir / outputFilename;

    // Export mesh
    if (Parser.ExportMesh(outputFilePath, OutputFormat))
    {
      fmt::print("✓ Successfully exported to: {}\n\n", outputFilePath.string());
    }
    else
    {
      fmt::print("✗ Failed to export {}\n\n", outputFilename);
    }
  }

  return 0;
}