#include "MeshComparator.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <fmt/core.h>
#include <algorithm>
#include <set>
#include <array>
#include <cmath>
#include <vector>
#include <map>

namespace Open3SDCM::Test
{
    bool Vertex::operator<(const Vertex& other) const
    {
        // Use exact comparison for sorting (no tolerance)
        // This ensures consistent sorting order
        if (x != other.x) return x < other.x;
        if (y != other.y) return y < other.y;
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
        // Load mesh without joining vertices to preserve original structure
        const aiScene* scene = importer.ReadFile(filePath.string(),
            aiProcess_Triangulate);

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

    // Helper structure for quantized vertex coordinates
    struct QuantizedVertex {
        int64_t x, y, z;
        bool operator<(const QuantizedVertex& other) const {
            if (x != other.x) return x < other.x;
            if (y != other.y) return y < other.y;
            return z < other.z;
        }
        bool operator==(const QuantizedVertex& other) const {
            return x == other.x && y == other.y && z == other.z;
        }
    };

    // Quantize a vertex using the given epsilon
    static QuantizedVertex quantizeVertex(const Vertex& v, float epsilon) {
        const double scale = 1.0 / epsilon;
        return {
            static_cast<int64_t>(std::round(v.x * scale)),
            static_cast<int64_t>(std::round(v.y * scale)),
            static_cast<int64_t>(std::round(v.z * scale))
        };
    }

    // Create a canonical representation of a triangle (sorted quantized vertices)
    static std::array<QuantizedVertex, 3> canonicalTriangle(const Vertex& v1, const Vertex& v2, const Vertex& v3, float epsilon) {
        std::array<QuantizedVertex, 3> qvs = {
            quantizeVertex(v1, epsilon),
            quantizeVertex(v2, epsilon),
            quantizeVertex(v3, epsilon)
        };
        std::sort(qvs.begin(), qvs.end());
        return qvs;
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

        // Build sets of canonical triangles (quantized and sorted)
        std::multiset<std::array<QuantizedVertex, 3>> refTriangles;
        for (const auto& face : reference.faces) {
            if (face.v1 < reference.vertices.size() &&
                face.v2 < reference.vertices.size() &&
                face.v3 < reference.vertices.size()) {
                refTriangles.insert(canonicalTriangle(
                    reference.vertices[face.v1],
                    reference.vertices[face.v2],
                    reference.vertices[face.v3],
                    epsilon
                ));
            }
        }

        std::multiset<std::array<QuantizedVertex, 3>> testTriangles;
        for (const auto& face : test.faces) {
            if (face.v1 < test.vertices.size() &&
                face.v2 < test.vertices.size() &&
                face.v3 < test.vertices.size()) {
                testTriangles.insert(canonicalTriangle(
                    test.vertices[face.v1],
                    test.vertices[face.v2],
                    test.vertices[face.v3],
                    epsilon
                ));
            }
        }

        result.expectedFaceCount = refTriangles.size();
        result.actualFaceCount = testTriangles.size();

        // Compare multisets
        std::vector<std::array<QuantizedVertex, 3>> missing;
        std::set_difference(refTriangles.begin(), refTriangles.end(),
                            testTriangles.begin(), testTriangles.end(),
                            std::back_inserter(missing));

        std::vector<std::array<QuantizedVertex, 3>> extra;
        std::set_difference(testTriangles.begin(), testTriangles.end(),
                            refTriangles.begin(), refTriangles.end(),
                            std::back_inserter(extra));

        result.missingFaces = missing.size();
        result.extraFaces = extra.size();
        result.facesMatch = (result.missingFaces == 0 && result.extraFaces == 0);

        // Vertex comparison
        result.verticesMatch = (result.expectedVertexCount == result.actualVertexCount);
        result.missingVertices = 0;
        result.extraVertices = 0;

        if (!result.facesMatch) {
            fmt::print("\n--- Face Mismatch Details ---\n");
            if (!missing.empty()) {
                fmt::print("Missing faces in GENERATED ({}):\n", missing.size());
                for(size_t i = 0; i < std::min(missing.size(), (size_t)5); ++i) {
                    fmt::print("  - Quantized vertices: ({}, {}, {}), ({}, {}, {}), ({}, {}, {})\n",
                        missing[i][0].x, missing[i][0].y, missing[i][0].z,
                        missing[i][1].x, missing[i][1].y, missing[i][1].z,
                        missing[i][2].x, missing[i][2].y, missing[i][2].z);
                }
            }
            if (!extra.empty()) {
                fmt::print("Extra faces in GENERATED ({}):\n", extra.size());
                for(size_t i = 0; i < std::min(extra.size(), (size_t)5); ++i) {
                    fmt::print("  - Quantized vertices: ({}, {}, {}), ({}, {}, {}), ({}, {}, {})\n",
                        extra[i][0].x, extra[i][0].y, extra[i][0].z,
                        extra[i][1].x, extra[i][1].y, extra[i][1].z,
                        extra[i][2].x, extra[i][2].y, extra[i][2].z);
                }
            }
            fmt::print("---------------------------\n\n");
        }

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

