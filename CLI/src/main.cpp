

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


namespace po = boost::program_options;
namespace fs = std::filesystem;


constexpr auto SubdirSize = 1000U;
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
        if(fs::is_regular_file(dir_entry.path()))
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

    auto now = std::chrono::system_clock::now();
    auto now_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream Date;
    Date << std::put_time(std::localtime(&now_t), "%Y-%m-%d-%H-%M-%S");
    OutputDir /= Date.str();
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


  return 0;
}