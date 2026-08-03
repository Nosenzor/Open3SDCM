"""Tests for the open3sdcm Python bindings.

Expected counts and colours below were read directly out of the DCM XML
(``vertex_count`` / ``facet_count`` / ``color`` attributes), so these assert
the bindings against the source data rather than against themselves.
"""

from __future__ import annotations

import gc
import pathlib
import threading

import numpy as np
import pytest

import open3sdcm

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]
TEST_DATA = REPO_ROOT / "TestData"

# path, vertices, faces, color, has_texture
SCANS = [
    ("real-world/scan_019.dcm", 95497, 190206, (128, 128, 128), True),
    ("Handle/HandleAngledLarge.dcm", 3776, 7548, (128, 101, 108), False),
    ("Scan-01/Scan.dcm", 59091, 118178, (147, 198, 219), False),
    ("Hole3x5/Hole 3x5.dcm", 376, 748, (128, 128, 128), False),
]


def scan_path(relative: str) -> pathlib.Path:
    path = TEST_DATA / relative
    if not path.exists():
        pytest.skip(f"sample scan not available: {relative}")
    return path


@pytest.fixture(scope="module")
def mesh():
    return open3sdcm.load(scan_path("real-world/scan_019.dcm"))


@pytest.mark.parametrize("relative,n_vertices,n_faces,color,has_texture", SCANS)
def test_geometry_matches_source_xml(relative, n_vertices, n_faces, color, has_texture):
    mesh = open3sdcm.load(scan_path(relative))

    assert mesh.vertices.shape == (n_vertices, 3)
    assert mesh.vertices.dtype == np.float32
    assert mesh.faces.shape == (n_faces, 3)
    assert mesh.faces.dtype == np.uint64
    assert mesh.color == color
    assert (mesh.texture is not None) is has_texture


@pytest.mark.parametrize("relative,n_vertices,_f,_c,_t", SCANS)
def test_face_indices_address_real_vertices(relative, n_vertices, _f, _c, _t):
    mesh = open3sdcm.load(scan_path(relative))
    assert mesh.faces.max() < n_vertices


@pytest.mark.parametrize("relative,_v,_f,_c,_t", SCANS)
def test_vertices_are_finite(relative, _v, _f, _c, _t):
    mesh = open3sdcm.load(scan_path(relative))
    assert np.isfinite(mesh.vertices).all()


def test_tuple_unpacking(mesh):
    vertices, faces, color = mesh

    assert np.array_equal(vertices, mesh.vertices)
    assert np.array_equal(faces, mesh.faces)
    assert color == mesh.color


def test_arrays_are_zero_copy_views(mesh):
    vertices = mesh.vertices

    assert not vertices.flags.owndata
    assert vertices.base is not None
    assert vertices.flags.c_contiguous
    # Repeated access must alias the same buffer, i.e. no copy per call.
    first = mesh.vertices.__array_interface__["data"][0]
    second = mesh.vertices.__array_interface__["data"][0]
    assert first == second


def test_arrays_are_read_only(mesh):
    assert not mesh.vertices.flags.writeable
    with pytest.raises(ValueError, match="read-only"):
        mesh.vertices[0, 0] = 0.0


def test_arrays_outlive_the_mesh_that_owns_them():
    local = open3sdcm.load(scan_path("Hole3x5/Hole 3x5.dcm"))
    vertices, faces = local.vertices, local.faces
    expected_vertices = np.array(vertices)
    expected_faces = np.array(faces)

    del local
    gc.collect()

    # A broken owner/keep-alive would make these use-after-free reads.
    assert np.array_equal(vertices, expected_vertices)
    assert np.array_equal(faces, expected_faces)


def test_uv_is_per_face_corner_and_normalised(mesh):
    uv = mesh.uv

    assert uv is not None
    assert uv.shape == (mesh.faces.shape[0] * 3, 2)
    assert uv.dtype == np.float32
    decoded = uv[np.isfinite(uv).all(axis=1)]
    assert decoded.size > 0
    assert decoded.min() >= 0.0
    assert decoded.max() <= 1.0


def test_texture_is_jpeg_with_matching_size(mesh):
    texture, size = mesh.texture, mesh.texture_size

    assert isinstance(texture, bytes)
    assert size == (8403, 3084)
    assert texture.startswith(b"\xff\xd8\xff"), "expected a JPEG SOI marker"


def test_scan_without_texture_exposes_no_uv_or_texture():
    mesh = open3sdcm.load(scan_path("Scan-01/Scan.dcm"))

    assert mesh.uv is None
    assert mesh.texture is None
    assert mesh.texture_size is None


def test_accepts_str_and_path_alike():
    path = scan_path("Hole3x5/Hole 3x5.dcm")

    assert open3sdcm.load(path).vertices.shape == open3sdcm.load(str(path)).vertices.shape


def test_path_attribute_round_trips():
    path = scan_path("Hole3x5/Hole 3x5.dcm")

    assert pathlib.Path(open3sdcm.load(path).path) == path


def test_repr_reports_contents(mesh):
    text = repr(mesh)

    assert "vertices=95497" in text
    assert "faces=190206" in text
    assert "texture=yes" in text


def test_concurrent_loads_are_independent():
    """load() drops the GIL while parsing, so this genuinely runs in parallel."""
    path = scan_path("Scan-01/Scan.dcm")
    expected = open3sdcm.load(path)
    results: list = []

    def worker():
        mesh = open3sdcm.load(path)
        results.append((np.array(mesh.vertices), np.array(mesh.faces), mesh.color))

    threads = [threading.Thread(target=worker) for _ in range(4)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert len(results) == 4
    for vertices, faces, color in results:
        assert np.array_equal(vertices, expected.vertices)
        assert np.array_equal(faces, expected.faces)
        assert color == expected.color


def test_missing_file_raises_file_not_found(tmp_path):
    with pytest.raises(FileNotFoundError):
        open3sdcm.load(tmp_path / "nope.dcm")


def test_directory_raises_value_error(tmp_path):
    with pytest.raises(ValueError):
        open3sdcm.load(tmp_path)


def test_non_dcm_file_raises_rather_than_returning_empty(tmp_path):
    junk = tmp_path / "junk.dcm"
    junk.write_bytes(b"this is definitely not a DCM scan")

    with pytest.raises(RuntimeError, match="no mesh data"):
        open3sdcm.load(junk)
