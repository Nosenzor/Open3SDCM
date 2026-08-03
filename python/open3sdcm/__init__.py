"""Read 3Shape DCM dental scan files into numpy arrays.

    >>> import open3sdcm
    >>> vertices, faces, color = open3sdcm.load("scan.dcm")
    >>> vertices.shape, vertices.dtype
    ((95497, 3), dtype('float32'))
    >>> faces.shape, faces.dtype
    ((190206, 3), dtype('uint64'))

``vertices`` and ``faces`` are zero-copy views into the underlying C++ buffers,
kept alive by the :class:`Mesh` that produced them.

A note on ``color``: the DCM format stores a *single* RGB value for the whole
mesh, not a per-vertex or per-face buffer, and in practice it is very often the
placeholder ``(128, 128, 128)``. Scans that carry real colour do so as an
embedded texture -- use :attr:`Mesh.texture` together with :attr:`Mesh.uv`::

    mesh = open3sdcm.load("scan.dcm")
    if mesh.texture is not None:
        width, height = mesh.texture_size
        uv = mesh.uv  # (len(faces) * 3, 2), one row per triangle corner
"""

from ._core import Mesh, load

__all__ = ["Mesh", "load", "__version__"]

# Read from the installed distribution rather than hardcoding, so this cannot
# drift from the version declared in pyproject.toml.
try:
    from importlib.metadata import PackageNotFoundError, version

    __version__ = version("open3sdcm")
except PackageNotFoundError:  # pragma: no cover - running from a source tree
    __version__ = "0.0.0.dev0"
