#pragma once

#include <filesystem>
#include <vector>
#include <array>
#include <string>
#include <set>
#include <cmath>

namespace Open3SDCM::Test
{
    struct Vertex
    {
        float x, y, z;

        bool operator<(const Vertex& other) const;
        bool isClose(const Vertex& other, float epsilon = 1e-5f) const;
    };

    struct Face
    {
        size_t v1, v2, v3;

        // Normalize face to canonical form (smallest index first)
        Face normalize() const;
        bool operator<(const Face& other) const;
        bool operator==(const Face& other) const;
    };

    struct MeshData
    {
        std::vector<Vertex> vertices;
        std::vector<Face> faces;
    };

    class MeshComparator
    {
    public:
        struct ComparisonResult
        {
            bool verticesMatch = false;
            bool facesMatch = false;
            size_t expectedVertexCount = 0;
            size_t actualVertexCount = 0;
            size_t expectedFaceCount = 0;
            size_t actualFaceCount = 0;
            size_t missingVertices = 0;
            size_t extraVertices = 0;
            size_t missingFaces = 0;
            size_t extraFaces = 0;

            bool isSuccess() const { return verticesMatch && facesMatch; }
        };

        // Load a mesh from file using Assimp
        static MeshData loadMesh(const std::filesystem::path& filePath);

        // Compare two meshes
        static ComparisonResult compareMeshes(const MeshData& reference, const MeshData& test, float epsilon = 1e-5f);

        // Print comparison result
        static void printResult(const ComparisonResult& result);
    };

} // namespace Open3SDCM::Test

