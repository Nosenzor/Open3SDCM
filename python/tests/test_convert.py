"""Conversion tests: the arrays the bindings hand back must describe a usable
mesh, and must agree with what the C++ CLI produces from the same file.
"""

from __future__ import annotations

import os
import pathlib
import shutil
import subprocess

import numpy as np
import pytest

import open3sdcm

from test_load import SCANS, scan_path

# Anything the CLI writes goes through std::ostream's default 6-significant-digit
# float formatting, so a PLY round-trip is lossy relative to the float32 source.
PLY_FLOAT_RTOL = 1e-5


def find_cli() -> pathlib.Path | None:
    """Locate Open3SDCMCLI, if this checkout has one built."""
    override = os.environ.get("OPEN3SDCM_CLI")
    if override:
        return pathlib.Path(override) if pathlib.Path(override).exists() else None

    repo_root = pathlib.Path(__file__).resolve().parents[2]
    for name in ("Open3SDCMCLI", "Open3SDCMCLI.exe"):
        for candidate in repo_root.glob(f"builds/*/bin/{name}"):
            return candidate
    return shutil.which("Open3SDCMCLI") and pathlib.Path(shutil.which("Open3SDCMCLI"))


def read_ascii_ply(path: pathlib.Path) -> tuple[np.ndarray, np.ndarray]:
    """Minimal reader for the ASCII PLY the CLI emits."""
    lines = path.read_text().splitlines()

    n_vertices = n_faces = 0
    vertex_properties = 0
    current_element = None
    header_end = 0
    for index, line in enumerate(lines):
        parts = line.split()
        if not parts:
            continue
        if parts[0] == "element":
            current_element = parts[1]
            if current_element == "vertex":
                n_vertices = int(parts[2])
            elif current_element == "face":
                n_faces = int(parts[2])
        elif parts[0] == "property" and current_element == "vertex":
            vertex_properties += 1
        elif parts[0] == "end_header":
            header_end = index + 1
            break

    vertex_lines = lines[header_end : header_end + n_vertices]
    face_lines = lines[header_end + n_vertices : header_end + n_vertices + n_faces]

    vertices = np.array([[float(v) for v in line.split()[:3]] for line in vertex_lines], dtype=np.float64)
    # Each face row is "<count> i j k"; drop the leading count.
    faces = np.array([[int(v) for v in line.split()[1:4]] for line in face_lines], dtype=np.uint64)

    assert vertices.shape == (n_vertices, 3)
    assert faces.shape == (n_faces, 3)
    assert vertex_properties >= 3
    return vertices, faces


@pytest.mark.parametrize("relative,n_vertices,n_faces,_c,_t", SCANS)
def test_converted_mesh_is_structurally_valid(relative, n_vertices, n_faces, _c, _t):
    mesh = open3sdcm.load(scan_path(relative))
    vertices, faces = mesh.vertices, mesh.faces

    assert vertices.shape == (n_vertices, 3)
    assert faces.shape == (n_faces, 3)
    assert np.isfinite(vertices).all()
    assert faces.min() >= 0
    assert faces.max() < n_vertices, "face references a vertex that does not exist"
    # A triangle that names the same vertex twice has no area.
    degenerate = (faces[:, 0] == faces[:, 1]) | (faces[:, 1] == faces[:, 2]) | (faces[:, 0] == faces[:, 2])
    assert not degenerate.any(), f"{degenerate.sum()} degenerate triangles"


@pytest.mark.parametrize("relative,_v,_f,_c,_t", SCANS)
def test_converted_mesh_has_real_extent(relative, _v, _f, _c, _t):
    """A scan collapsed to a point or a plane means the decode went wrong."""
    vertices = open3sdcm.load(scan_path(relative)).vertices

    extent = vertices.max(axis=0) - vertices.min(axis=0)
    assert (extent > 0).all(), f"degenerate bounding box {extent}"


@pytest.mark.parametrize("relative,_v,_f,_c,_t", SCANS)
def test_every_vertex_is_used_by_some_face(relative, _v, _f, _c, _t):
    mesh = open3sdcm.load(scan_path(relative))

    referenced = np.unique(mesh.faces)
    assert referenced.size == mesh.vertices.shape[0], (
        f"{mesh.vertices.shape[0] - referenced.size} vertices are not referenced by any face"
    )


def test_conversion_round_trips_through_numpy(tmp_path):
    """Write the arrays out as PLY and read them back unchanged."""
    mesh = open3sdcm.load(scan_path("Hole3x5/Hole 3x5.dcm"))
    vertices, faces = mesh.vertices, mesh.faces

    out = tmp_path / "roundtrip.ply"
    with out.open("w") as handle:
        handle.write("ply\nformat ascii 1.0\n")
        handle.write(f"element vertex {vertices.shape[0]}\n")
        handle.write("property float x\nproperty float y\nproperty float z\n")
        handle.write(f"element face {faces.shape[0]}\n")
        handle.write("property list uchar int vertex_indices\nend_header\n")
        for x, y, z in vertices:
            # repr() of a numpy scalar is "np.float32(...)"; go through float.
            handle.write(f"{float(x)!r} {float(y)!r} {float(z)!r}\n")
        for i, j, k in faces:
            handle.write(f"3 {i} {j} {k}\n")

    read_vertices, read_faces = read_ascii_ply(out)
    assert np.allclose(read_vertices, vertices, rtol=0, atol=0)
    assert np.array_equal(read_faces, faces)


@pytest.mark.parametrize("relative,_v,_f,_c,_t", SCANS)
def test_matches_cpp_cli_conversion(relative, _v, _f, _c, _t, tmp_path):
    """Cross-validate the bindings against the reference C++ implementation."""
    cli = find_cli()
    if cli is None:
        pytest.skip("Open3SDCMCLI not built; set OPEN3SDCM_CLI to enable")

    source = scan_path(relative)
    result = subprocess.run(
        [str(cli), "-i", str(source), "-o", str(tmp_path), "-f", "ply"],
        capture_output=True,
        text=True,
        timeout=300,
    )
    assert result.returncode == 0, f"CLI failed: {result.stdout}\n{result.stderr}"

    # The CLI writes into a timestamped subdirectory.
    exported = list(tmp_path.glob(f"*/{source.stem}.ply"))
    assert len(exported) == 1, f"expected one exported PLY, found {exported}"

    cli_vertices, cli_faces = read_ascii_ply(exported[0])
    mesh = open3sdcm.load(source)

    assert cli_vertices.shape == mesh.vertices.shape
    assert np.array_equal(cli_faces, mesh.faces), "face connectivity differs from the CLI"
    assert np.allclose(cli_vertices, mesh.vertices, rtol=PLY_FLOAT_RTOL), (
        "vertex positions differ from the CLI beyond PLY text precision"
    )
