// Python bindings for Open3SDCMLib.
//
// Design notes:
//  * `vertices` and `faces` are zero-copy views straight into the parser's
//    std::vectors; the Mesh object is handed to numpy as the array owner so
//    the buffers cannot outlive it.
//  * `uv` cannot be zero-copy: the library stores per-corner coordinates as
//    std::optional<TextureCoordinate> (12 bytes: 8 payload + flag + padding),
//    so it is repacked into a dense array with NaN marking undecoded corners.
//  * `color` is deliberately a single RGB triple, not a per-vertex buffer.
//    The DCM format carries exactly one `color` attribute on <Facets> for the
//    whole mesh (commonly 0x808080, a placeholder). Real per-vertex colour,
//    when present, must be sampled from `texture` using `uv`.

#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/tuple.h>
#include <nanobind/stl/unique_ptr.h>

#include "ParseDcm.h"
#include "definitions.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace nb = nanobind;
using namespace nb::literals;

namespace
{
  namespace fs = std::filesystem;

  // The zero-copy face view reinterprets Triangle[] as a tightly packed
  // uint64[N][3]. Guarantee that layout rather than assuming it.
  static_assert(std::is_standard_layout_v<Open3SDCM::Triangle>);
  static_assert(sizeof(Open3SDCM::Triangle) == 3 * sizeof(std::size_t));
  static_assert(offsetof(Open3SDCM::Triangle, v1) == 0 * sizeof(std::size_t));
  static_assert(offsetof(Open3SDCM::Triangle, v2) == 1 * sizeof(std::size_t));
  static_assert(offsetof(Open3SDCM::Triangle, v3) == 2 * sizeof(std::size_t));
  static_assert(sizeof(std::size_t) == sizeof(std::uint64_t));

  struct Mesh
  {
    Open3SDCM::DCMParser parser;
    fs::path source;
  };

  using RowsOf3F = nb::ndarray<nb::numpy, const float, nb::shape<-1, 3>>;
  using RowsOf3U = nb::ndarray<nb::numpy, const std::uint64_t, nb::shape<-1, 3>>;
  using RowsOf2F = nb::ndarray<nb::numpy, const float, nb::shape<-1, 2>>;

  // Hands a heap-allocated vector to numpy, freeing it when the array dies.
  RowsOf2F AdoptAsRowsOf2(std::unique_ptr<std::vector<float>> values)
  {
    const std::size_t rows = values->size() / 2;
    float* const data = values->data();
    nb::capsule owner(values.get(), [](void* p) noexcept {
      delete static_cast<std::vector<float>*>(p);
    });
    values.release();
    return RowsOf2F(data, {rows, 2}, std::move(owner));
  }

  const Open3SDCM::TextureCoordinateData* FindDecodedCoordinates(const Open3SDCM::SurfaceData& surface)
  {
    for (const auto& candidate : surface.textureCoordinates)
    {
      if (candidate.HasDecodedCoordinates())
      {
        return &candidate;
      }
    }
    return nullptr;
  }

  const Open3SDCM::EmbeddedTextureImage* FindTextureImage(const Open3SDCM::SurfaceData& surface)
  {
    for (const auto& candidate : surface.textureImages)
    {
      if (!candidate.imageBytes.empty())
      {
        return &candidate;
      }
    }
    return nullptr;
  }
}// namespace

NB_MODULE(_core, m)
{
  m.doc() = "Read 3Shape DCM dental scan files into numpy arrays.";

  nb::class_<Mesh>(m, "Mesh", "A parsed DCM scan. Unpacks as ``vertices, faces, color``.")

      .def_prop_ro(
          "vertices",
          [](Mesh& self) {
            const std::size_t count = self.parser.m_Vertices.size() / 3;
            return RowsOf3F(self.parser.m_Vertices.data(), {count, 3}, nb::find(&self));
          },
          "(N, 3) float32 vertex positions. Zero-copy view; read-only.")

      .def_prop_ro(
          "faces",
          [](Mesh& self) {
            const auto* data = reinterpret_cast<const std::uint64_t*>(self.parser.m_Triangles.data());
            return RowsOf3U(data, {self.parser.m_Triangles.size(), 3}, nb::find(&self));
          },
          "(M, 3) uint64 triangle vertex indices, 0-based. Zero-copy view; read-only.")

      .def_prop_ro(
          "color",
          [](const Mesh& self) -> std::optional<std::tuple<int, int, int>> {
            const auto& base = self.parser.m_SurfaceData.baseColor;
            if (!base.has_value())
            {
              return std::nullopt;
            }
            return std::make_tuple(static_cast<int>(base->r),
                                   static_cast<int>(base->g),
                                   static_cast<int>(base->b));
          },
          "Single (r, g, b) 0-255 tint for the whole mesh, or None.\n"
          "This is NOT per-vertex or per-face data: the DCM format stores one\n"
          "colour attribute for the entire mesh, and it is frequently the\n"
          "placeholder (128, 128, 128). For true per-vertex colour, sample\n"
          "`texture` at `uv`.")

      .def_prop_ro(
          "uv",
          [](const Mesh& self) -> std::optional<RowsOf2F> {
            const auto* coords = FindDecodedCoordinates(self.parser.m_SurfaceData);
            if (coords == nullptr)
            {
              return std::nullopt;
            }

            constexpr float kMissing = std::numeric_limits<float>::quiet_NaN();
            auto packed = std::make_unique<std::vector<float>>(coords->cornerCoordinates.size() * 2, kMissing);
            for (std::size_t corner = 0; corner < coords->cornerCoordinates.size(); ++corner)
            {
              if (const auto& value = coords->cornerCoordinates[corner]; value.has_value())
              {
                (*packed)[corner * 2 + 0] = value->u;
                (*packed)[corner * 2 + 1] = value->v;
              }
            }
            return AdoptAsRowsOf2(std::move(packed));
          },
          // The array carries its own capsule owner, so nanobind must not also
          // apply a property's default reference_internal policy.
          nb::rv_policy::reference,
          "(M*3, 2) float32 texture coordinates, or None.\n"
          "One row per triangle corner (not per vertex), ordered to match\n"
          "`faces` flattened. Undecoded corners are NaN.")

      .def_prop_ro(
          "texture",
          [](const Mesh& self) -> std::optional<nb::bytes> {
            const auto* image = FindTextureImage(self.parser.m_SurfaceData);
            if (image == nullptr)
            {
              return std::nullopt;
            }
            return nb::bytes(reinterpret_cast<const char*>(image->imageBytes.data()),
                             image->imageBytes.size());
          },
          "Embedded texture as encoded image bytes (JPEG), or None.")

      .def_prop_ro(
          "texture_size",
          [](const Mesh& self) -> std::optional<std::tuple<std::size_t, std::size_t>> {
            const auto* image = FindTextureImage(self.parser.m_SurfaceData);
            if (image == nullptr)
            {
              return std::nullopt;
            }
            return std::make_tuple(image->width, image->height);
          },
          "(width, height) of `texture` in pixels, or None.")

      .def_prop_ro(
          "path",
          [](const Mesh& self) { return self.source; },
          "Path this mesh was loaded from.")

      .def(
          "__iter__",
          [](nb::object self) {
            return nb::iter(nb::make_tuple(self.attr("vertices"), self.attr("faces"), self.attr("color")));
          },
          "Yields vertices, faces, color so the mesh can be tuple-unpacked.")

      .def("__repr__", [](const Mesh& self) {
        return "<open3sdcm.Mesh vertices=" + std::to_string(self.parser.m_Vertices.size() / 3) +
               " faces=" + std::to_string(self.parser.m_Triangles.size()) +
               (self.parser.m_SurfaceData.baseColor.has_value() ? " color=yes" : " color=no") +
               (FindTextureImage(self.parser.m_SurfaceData) != nullptr ? " texture=yes" : " texture=no") + ">";
      });

  m.def(
      "load",
      [](const fs::path& path) {
        std::error_code ec;
        if (!fs::exists(path, ec) || ec)
        {
          nb::chain_error(PyExc_FileNotFoundError, "no such DCM file: %s", path.string().c_str());
          throw nb::python_error();
        }
        if (fs::is_directory(path, ec))
        {
          throw std::invalid_argument("expected a DCM file, got a directory: " + path.string());
        }

        auto mesh = std::make_unique<Mesh>();
        mesh->source = path;
        {
          // Parsing a large scan takes on the order of a second and touches no
          // Python state, so hold the GIL only for the wrapping, not the work.
          nb::gil_scoped_release unlocked;
          mesh->parser.ParseDCM(path);
        }

        if (mesh->parser.m_Vertices.empty() && mesh->parser.m_Triangles.empty())
        {
          throw std::runtime_error("no mesh data could be parsed from " + path.string() +
                                   " (unsupported schema, or the file is not a DCM scan)");
        }
        return mesh;
      },
      "path"_a,
      "Load a DCM scan.\n\n"
      "Returns a Mesh, which also unpacks directly::\n\n"
      "    vertices, faces, color = open3sdcm.load(\"scan.dcm\")\n");
}
