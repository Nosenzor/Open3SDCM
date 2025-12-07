#include "MeshComparator.h"
#include "ParseDcm.h"

#include <boost/program_options.hpp>
#include <fmt/core.h>
#include <filesystem>
#include <iostream>
#include <chrono>

namespace po = boost::program_options;
namespace fs = std::filesystem;

int main(int argc, char** argv)
{
    try
    {
        // Parse command line arguments
        po::options_description desc("Mesh Comparison Test Tool");
        desc.add_options()
            ("help,h", "Show help message")
            ("dcm,d", po::value<fs::path>()->required(), "Input DCM file")
            ("reference,r", po::value<fs::path>()->required(), "Reference mesh file (STL, OBJ, PLY, etc.)")
            ("epsilon,e", po::value<float>()->default_value(1e-5f), "Tolerance for vertex comparison")
            ("output,o", po::value<fs::path>(), "Optional: output directory for generated mesh");

        po::variables_map vm;
        po::store(po::parse_command_line(argc, argv, desc), vm);

        if (vm.count("help"))
        {
            std::cout << desc << "\n";
            return 0;
        }

        po::notify(vm);

        fs::path dcmFile = vm["dcm"].as<fs::path>();
        fs::path referenceFile = vm["reference"].as<fs::path>();
        float epsilon = vm["epsilon"].as<float>();

        // Validate input files
        if (!fs::exists(dcmFile))
        {
            fmt::print(stderr, "Error: DCM file not found: {}\n", dcmFile.string());
            return 1;
        }

        if (!fs::exists(referenceFile))
        {
            fmt::print(stderr, "Error: Reference file not found: {}\n", referenceFile.string());
            return 1;
        }

        // Determine output format from reference file extension
        std::string refExtension = referenceFile.extension().string();
        if (refExtension.empty())
        {
            fmt::print(stderr, "Error: Reference file has no extension\n");
            return 1;
        }

        // Remove leading dot
        if (refExtension[0] == '.')
        {
            refExtension = refExtension.substr(1);
        }

        fmt::print("=== Open3SDCM Mesh Comparison Test ===\n\n");
        fmt::print("DCM File:       {}\n", dcmFile.string());
        fmt::print("Reference File: {}\n", referenceFile.string());
        fmt::print("Output Format:  {}\n", refExtension);
        fmt::print("Epsilon:        {}\n\n", epsilon);

        // Step 1: Parse DCM file
        fmt::print("Step 1: Parsing DCM file...\n");
        auto parseStart = std::chrono::high_resolution_clock::now();

        Open3SDCM::DCMParser parser;
        parser.ParseDCM(dcmFile);

        auto parseEnd = std::chrono::high_resolution_clock::now();
        auto parseDuration = std::chrono::duration_cast<std::chrono::milliseconds>(parseEnd - parseStart);

        fmt::print("  Parsed {} vertices and {} triangles in {} ms\n\n",
                   parser.m_Vertices.size() / 3,
                   parser.m_Triangles.size(),
                   parseDuration.count());

        // Step 2: Export to temporary file or specified directory
        fmt::print("Step 2: Exporting to {} format...\n", refExtension);

        fs::path outputDir;
        if (vm.count("output"))
        {
            outputDir = vm["output"].as<fs::path>();
        }
        else
        {
            outputDir = fs::temp_directory_path() / "Open3SDCM_test";
        }

        fs::create_directories(outputDir);

        fs::path generatedFile = outputDir / (dcmFile.stem().string() + "." + refExtension);

        auto exportStart = std::chrono::high_resolution_clock::now();

        if (!parser.ExportMesh(generatedFile, refExtension))
        {
            fmt::print(stderr, "Error: Failed to export mesh\n");
            return 1;
        }

        auto exportEnd = std::chrono::high_resolution_clock::now();
        auto exportDuration = std::chrono::duration_cast<std::chrono::milliseconds>(exportEnd - exportStart);

        fmt::print("  Exported to: {}\n", generatedFile.string());
        fmt::print("  Export time: {} ms\n\n", exportDuration.count());

        // Step 3: Load reference mesh
        fmt::print("Step 3: Loading reference mesh...\n");
        auto loadRefStart = std::chrono::high_resolution_clock::now();

        auto referenceMesh = Open3SDCM::Test::MeshComparator::loadMesh(referenceFile);

        auto loadRefEnd = std::chrono::high_resolution_clock::now();
        auto loadRefDuration = std::chrono::duration_cast<std::chrono::milliseconds>(loadRefEnd - loadRefStart);

        fmt::print("  Loaded {} vertices and {} faces in {} ms\n\n",
                   referenceMesh.vertices.size(),
                   referenceMesh.faces.size(),
                   loadRefDuration.count());

        // Step 4: Load generated mesh
        fmt::print("Step 4: Loading generated mesh...\n");
        auto loadGenStart = std::chrono::high_resolution_clock::now();

        auto generatedMesh = Open3SDCM::Test::MeshComparator::loadMesh(generatedFile);

        auto loadGenEnd = std::chrono::high_resolution_clock::now();
        auto loadGenDuration = std::chrono::duration_cast<std::chrono::milliseconds>(loadGenEnd - loadGenStart);

        fmt::print("  Loaded {} vertices and {} faces in {} ms\n\n",
                   generatedMesh.vertices.size(),
                   generatedMesh.faces.size(),
                   loadGenDuration.count());

        // Step 5: Compare meshes
        fmt::print("Step 5: Comparing meshes...\n");
        auto compareStart = std::chrono::high_resolution_clock::now();

        auto result = Open3SDCM::Test::MeshComparator::compareMeshes(
            referenceMesh,
            generatedMesh,
            epsilon);

        auto compareEnd = std::chrono::high_resolution_clock::now();
        auto compareDuration = std::chrono::duration_cast<std::chrono::milliseconds>(compareEnd - compareStart);

        fmt::print("  Comparison time: {} ms\n", compareDuration.count());

        // Print results
        Open3SDCM::Test::MeshComparator::printResult(result);

        // Calculate total time
        auto totalDuration = parseDuration + exportDuration + loadRefDuration + loadGenDuration + compareDuration;
        fmt::print("Total time: {} ms\n\n", totalDuration.count());

        return result.isSuccess() ? 0 : 1;
    }
    catch (const po::error& e)
    {
        fmt::print(stderr, "Command line error: {}\n", e.what());
        fmt::print(stderr, "Use --help for usage information\n");
        return 1;
    }
    catch (const std::exception& e)
    {
        fmt::print(stderr, "Error: {}\n", e.what());
        return 1;
    }
}

