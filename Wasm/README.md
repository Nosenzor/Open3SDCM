# Open3SDCM WebAssembly build

Cross-compiles `Open3SDCMLib` to WebAssembly and wraps it in a TypeScript-friendly
ES6 module via Emscripten's embind. The unmodified C++ parser runs in the
browser/worker: DCM bytes go in, mesh arrays and STL/PLY/OBJ bytes come out.

An interactive demo page (upload a DCM, view it in 3D, export STL/PLY/OBJ)
lives in [`web/`](../web/) and is deployed to GitHub Pages by
[`.github/workflows/pages.yml`](../.github/workflows/pages.yml). A Node test
for the module API used by that page is
[`test/web-demo-test.mjs`](test/web-demo-test.mjs).

## Build

Prerequisites: [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
(activate it so `emcc` is on `PATH` and `EMSDK` is set), Ninja, and the usual
vcpkg bootstrap used by this project.

```bash
source <your-emsdk>/emsdk_env.sh
cmake --preset wasm-release        # configures; vcpkg installs poco/openssl/boost
cmake --build --preset wasm-release
```

The build uses vcpkg's `wasm32-emscripten` community triplet, so the desktop
toolchain and its CLI-only dependencies (assimp, spdlog, boost-program-options)
are not part of it; only what the library needs (poco, openssl, boost) is
compiled to WebAssembly. The desktop CLI is not built in this configuration.

Artifacts land in `builds/wasm-release/bin/`:

| File               | Purpose                                        |
|--------------------|------------------------------------------------|
| `open3sdcm.js`     | ES6 module loader (default export = factory)   |
| `open3sdcm.wasm`   | compiled library + bindings                    |
| `types/open3sdcm.d.ts` | TypeScript declarations for the module      |

The module is built for `web`, `worker` and `node` environments, so the same
artifacts can be smoke-tested under Node. To verify a build against the
desktop parser (all vertex coordinates must come out bit-identical):

```bash
node Wasm/test/smoke-test.mjs TestData/real-world/scan_012.dcm
```

## Usage (JavaScript / TypeScript)

```js
import createOpen3SDCM from "./builds/wasm-release/bin/open3sdcm.js";

const Open3SDCM = await createOpen3SDCM();
const parser = new Open3SDCM.DCMParser();

parser.parseBytes(new Uint8Array(await (await fetch("scan.dcm")).arrayBuffer()));

if (parser.hasMesh()) {
  // Views into WASM memory - .slice() to keep them
  const vertices  = parser.vertices().slice();   // Float32Array, (N, 3) flat
  const triangles = parser.triangles().slice();  // Uint32Array,  (M, 3) flat
  const color     = parser.baseColor();          // 0xRRGGBB or -1

  // Or export with the library's own writers
  const stl = parser.exportMeshBytes("stl", "mesh").slice(); // Uint8Array
}
```

Data model:

- `vertices()`: flat `(N, 3)` `Float32Array`; `vertexCount() = N`.
- `triangles()`: flat `(M, 3)` `Uint32Array` of 0-based indices; `triangleCount() = M`.
- `uv()`: per-corner UV pairs, `NaN` where a corner was not decoded (empty when absent).
- `textureImage()`: raw bytes of the first embedded texture (typically JPEG).
- `exportMeshBytes(format, baseName)`: `format` is `"stl"` (alias `"stlb"`),
  `"ply"` or `"obj"`. For `"obj"` only the `.obj` bytes are returned; the
  companion `.mtl`/texture are written next to it in the virtual filesystem
  and can be fetched with `readFile("/open3sdcm-output/<baseName>.mtl")`.
- `exportMeshTo(path, format)` / `readFile(path)`: lower-level access to the
  Emscripten virtual filesystem (e.g. for IDBFS mounts via the `FS` runtime
  method).

All buffer-returning methods are zero-copy views into WebAssembly memory.
A view is valid only until the next call on the same parser: call `.slice()`
on it synchronously if you need to keep the data.

## How it works

The library's parser and mesh writers are file-based (`DCMParser::ParseDCM`,
`DCMParser::ExportMesh`). The bindings therefore round-trip through
Emscripten's in-memory filesystem (MEMFS): `parseBytes()` writes the input to
a MEMFS file, parses it with the unmodified library, and deletes it;
`exportMeshBytes()` writes with the library's own writers into MEMFS and
reads the bytes back.

C++ exception catching (`-fexceptions`) is enabled for the library and the
final link: Emscripten disables it by default, which would strip the
`try/catch` around the Poco calls and turn any malformed DCM into a WASM
abort instead of an empty mesh.
