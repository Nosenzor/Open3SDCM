#include "MeshComparator.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <fmt/core.h>
#include <algorithm>
#include <unordered_set>
#include <cmath>

namespace Open3SDCM::Test
{
    bool Vertex::operator<(const Vertex& other) const
    {
        if (std::abs(x - other.x) > 1e-9f) return x < other.x;
        if (std::abs(y - other.y) > 1e-9f) return y < other.y;
        return z < other.z;
    }

    bool Vertex::isClose(const Vertex& other, float epsilon) const
    {
        return std::abs(x - other.x) < epsilon &&
               std::abs(y - other.y) < epsilon &&
               std::abs(z - other.z) < epsilon;
    }

    Face Face::normalize() const
    {
        // Find the smallest vertex index
        size_t minIdx = std::min({v1, v2, v3});

        if (minIdx == v1) {
            return {v1, v2, v3};
        } else if (minIdx == v2) {
            return {v2, v3, v1};
        } else {
            return {v3, v1, v2};
        }
    }

    bool Face::operator<(const Face& other) const
    {
        if (v1 != other.v1) return v1 < other.v1;
        if (v2 != other.v2) return v2 < other.v2;
        return v3 < other.v3;
    }

    bool Face::operator==(const Face& other) const
    {
        return v1 == other.v1 && v2 == other.v2 && v3 == other.v3;
    }

    MeshData MeshComparator::loadMesh(const std::filesystem::path& filePath)
    {
        MeshData data;

        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(filePath.string(),
            aiProcess_Triangulate |
            aiProcess_JoinIdenticalVertices);

        if (!scene || !scene->HasMeshes())
        {
            throw std::runtime_error(fmt::format("Failed to load mesh from: {}", filePath.string()));
        }

        // Load all vertices and faces from all meshes
        size_t vertexOffset = 0;

        for (unsigned int meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx)
        {
            const aiMesh* mesh = scene->mMeshes[meshIdx];

            // Load vertices
            for (unsigned int i = 0; i < mesh->mNumVertices; ++i)
            {
                const aiVector3D& v = mesh->mVertices[i];
                data.vertices.push_back({v.x, v.y, v.z});
            }

            // Load faces
            for (unsigned int i = 0; i < mesh->mNumFaces; ++i)
            {
                const aiFace& face = mesh->mFaces[i];
                if (face.mNumIndices == 3)
                {
                    data.faces.push_back({
                        vertexOffset + face.mIndices[0],
                        vertexOffset + face.mIndices[1],
                        vertexOffset + face.mIndices[2]
                    });
                }
            }

            vertexOffset += mesh->mNumVertices;
        }

        return data;
    }

    MeshComparator::ComparisonResult MeshComparator::compareMeshes(
        const MeshData& reference,
        const MeshData& test,
        float epsilon)
    {
        ComparisonResult result;
        result.expectedVertexCount = reference.vertices.size();
        result.actualVertexCount = test.vertices.size();
        result.expectedFaceCount = reference.faces.size();
        result.actualFaceCount = test.faces.size();

        // Step 1: Build a mapping from reference vertices to test vertices
        std::vector<size_t> refToTestMap(reference.vertices.size(), SIZE_MAX);
        std::vector<bool> testVertexUsed(test.vertices.size(), false);

        for (size_t refIdx = 0; refIdx < reference.vertices.size(); ++refIdx)
        {
            const Vertex& refVertex = reference.vertices[refIdx];

            // Find matching vertex in test mesh
            for (size_t testIdx = 0; testIdx < test.vertices.size(); ++testIdx)
            {
                if (!testVertexUsed[testIdx] && refVertex.isClose(test.vertices[testIdx], epsilon))
                {
                    refToTestMap[refIdx] = testIdx;
                    testVertexUsed[testIdx] = true;
                    break;
                }
            }

            if (refToTestMap[refIdx] == SIZE_MAX)
            {
                result.missingVertices++;
            }
        }

        // Count extra vertices in test mesh
        for (bool used : testVertexUsed)
        {
            if (!used)
            {
                result.extraVertices++;
            }
        }

        result.verticesMatch = (result.missingVertices == 0 && result.extraVertices == 0);

        // Step 2: Compare faces (with permutation handling)
        // Build a set of normalized faces from reference
        std::set<std::array<size_t, 3>> referenceFacesSet;

        for (const Face& face : reference.faces)
        {
            // Map reference face indices to test mesh vertex indices
            if (refToTestMap[face.v1] != SIZE_MAX &&
                refToTestMap[face.v2] != SIZE_MAX &&
                refToTestMap[face.v3] != SIZE_MAX)
            {
                std::array<size_t, 3> vertices = {
                    refToTestMap[face.v1],
                    refToTestMap[face.v2],
                    refToTestMap[face.v3]
                };

                // Sort to create canonical representation
                std::sort(vertices.begin(), vertices.end());
                referenceFacesSet.insert(vertices);
            }
        }

        // Build a set of normalized faces from test
        std::set<std::array<size_t, 3>> testFacesSet;

        for (const Face& face : test.faces)
        {
            std::array<size_t, 3> vertices = {face.v1, face.v2, face.v3};
            std::sort(vertices.begin(), vertices.end());
            testFacesSet.insert(vertices);
        }

        // Compare face sets
        for (const auto& refFace : referenceFacesSet)
        {
            if (testFacesSet.find(refFace) == testFacesSet.end())
            {
                result.missingFaces++;
            }
        }

        for (const auto& testFace : testFacesSet)
        {
            if (referenceFacesSet.find(testFace) == referenceFacesSet.end())
            {
                result.extraFaces++;
            }
        }

        result.facesMatch = (result.missingFaces == 0 && result.extraFaces == 0);

        return result;
    }

    void MeshComparator::printResult(const ComparisonResult& result)
    {
        fmt::print("\n=== Mesh Comparison Results ===\n\n");

        // Vertex comparison
        fmt::print("Vertices:\n");
        fmt::print("  Expected: {}\n", result.expectedVertexCount);
        fmt::print("  Actual:   {}\n", result.actualVertexCount);
        fmt::print("  Missing:  {}\n", result.missingVertices);
        fmt::print("  Extra:    {}\n", result.extraVertices);
        fmt::print("  Status:   {}\n\n", result.verticesMatch ? "✓ PASS" : "✗ FAIL");

        // Face comparison
        fmt::print("Faces:\n");
        fmt::print("  Expected: {}\n", result.expectedFaceCount);
        fmt::print("  Actual:   {}\n", result.actualFaceCount);
        fmt::print("  Missing:  {}\n", result.missingFaces);
        fmt::print("  Extra:    {}\n", result.extraFaces);
        fmt::print("  Status:   {}\n\n", result.facesMatch ? "✓ PASS" : "✗ FAIL");

        // Overall result
        fmt::print("Overall: {}\n\n", result.isSuccess() ? "✓ TEST PASSED" : "✗ TEST FAILED");
    }

} // namespace Open3SDCM::Test

