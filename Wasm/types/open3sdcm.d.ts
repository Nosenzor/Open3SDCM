/**
 * TypeScript declarations for the Open3SDCM WebAssembly module
 * (bin/open3sdcm.js + bin/open3sdcm.wasm).
 *
 * The build is an ES6 module (MODULARIZE + EXPORT_ES6): import the factory,
 * await it once, then create parsers.
 *
 *   import createOpen3SDCM from "./open3sdcm.js";
 *   const Open3SDCM = await createOpen3SDCM();
 *   const parser = new Open3SDCM.DCMParser();
 *   parser.parseBytes(new Uint8Array(await (await fetch("scan.dcm")).arrayBuffer()));
 *   if (parser.hasMesh()) {
 *     const vertices: Float32Array = parser.vertices().slice();
 *     const triangles: Uint32Array = parser.triangles().slice();
 *   }
 *   parser.delete();   // frees the C++ object and its mesh
 *
 * Mesh-returning methods hand out typed-array VIEWS into WebAssembly
 * memory. A view is only valid until the next call into the module: besides
 * the next call on the same parser, any allocation (another parser, an
 * export) may grow the WebAssembly memory, which detaches every outstanding
 * view (it then reads as length 0). slice() a view synchronously to take
 * ownership.
 *
 * Parsers are C++ objects: call delete() when done with one, or its decoded
 * mesh stays on the WebAssembly heap.
 */

export interface DCMParser {
  /**
   * Parses a DCM file held in memory. Accepts a Uint8Array (recommended),
   * any other ArrayBuffer view (its raw bytes are used, whatever the element
   * type), a raw ArrayBuffer, or a plain array of byte values. Any
   * previously parsed state is cleared first.
   *
   * Does not throw for unreadable input: afterwards hasMesh() is false and
   * the library logs the reason to the console (stderr).
   */
  parseBytes(data: ArrayBufferView | ArrayBuffer | ArrayLike<number>): void;

  /** True when the last parse produced a non-empty mesh. */
  hasMesh(): boolean;

  /** Number of vertices of the parsed mesh (vertices().length / 3). */
  vertexCount(): number;

  /** Number of triangles of the parsed mesh. */
  triangleCount(): number;

  /**
   * Flat (N, 3) float32 vertex positions. VIEW (see the module notes):
   * call .slice() to keep the data.
   */
  vertices(): Float32Array;

  /**
   * Flat (M, 3) uint32 triangle indices (0-based, into vertices()).
   * VIEW (see the module notes): call .slice() to keep the data.
   */
  triangles(): Uint32Array;

  /** Mesh-wide base colour as 0xRRGGBB, or -1 when the scan carries none. */
  baseColor(): number;

  /**
   * Raw bytes of the embedded texture image (typically JPEG) that the
   * library's OBJ writer exports; uv() is the UV set paired with it (the
   * pairing follows the texture/coordinate IDs, so on multi-texture scans
   * it is not necessarily the first image). Empty when the scan has none.
   * VIEW: call .slice() to keep.
   */
  textureImage(): Uint8Array;

  /**
   * Per-corner UV pairs (u0, v0, u1, v1, ...) for textureImage(), in the
   * DCM's top-left image origin convention; NaN marks a corner whose
   * coordinate could not be decoded. Values normally lie in [0, 1] but the
   * format can encode [-256, 256). Empty when the scan has none, or when
   * the set does not have exactly one entry per triangle corner.
   * VIEW: call .slice() to keep.
   */
  uv(): Float32Array;

  /**
   * Exports the parsed mesh to "stl", "ply" or "obj" and returns the
   * bytes of the main file. Each call first empties the output directory
   * "/open3sdcm-output", so it only ever holds the latest export. For
   * "obj" only the .obj bytes are returned; the companion .mtl and texture
   * image are written next to it, e.g. readFile("/open3sdcm-output/<baseName>.mtl").
   * VIEW: call .slice() to keep. Empty on failure, for an unknown format
   * or an empty mesh.
   *
   * @param format   "stl" (alias "stlb"), "ply" or "obj".
   * @param baseName File name without extension.
   */
  exportMeshBytes(format: string, baseName: string): Uint8Array;

  /**
   * Writes the mesh to any path of the Emscripten virtual filesystem (the
   * directory must exist, e.g. created with module.FS.mkdir()). Returns the
   * library's success flag.
   *
   * @param virtualPath Destination file path (e.g. "/out/mesh.stl").
   * @param format       "stl" (alias "stlb"), "ply" or "obj".
   */
  exportMeshTo(virtualPath: string, format: string): boolean;

  /**
   * Reads any file from the Emscripten virtual filesystem; empty when it
   * does not exist. VIEW: call .slice() to keep.
   */
  readFile(virtualPath: string): Uint8Array;

  /** Frees the C++ object and its decoded mesh. The parser is unusable afterwards. */
  delete(): void;

  /** True once delete() has been called. */
  isDeleted(): boolean;
}

/** The subset of Emscripten's FS runtime method that is useful with this module. */
export interface EmscriptenFS {
  readdir(path: string): string[];
  readFile(path: string): Uint8Array;
  writeFile(path: string, data: string | ArrayBufferView): void;
  unlink(path: string): void;
  mkdir(path: string): void;
  analyzePath(path: string): { exists: boolean };
}

export interface Open3SDCMModule {
  DCMParser: new () => DCMParser;
  /** Emscripten's virtual filesystem (MEMFS). */
  FS: EmscriptenFS;
  /** The WebAssembly heap as bytes; replaced when the memory grows. */
  HEAPU8: Uint8Array;
}

/**
 * Module factory. Returns a promise that resolves once the .wasm binary
 * is instantiated.
 *
 * @param init Optional overrides, e.g. { locateFile: (path) => ... } when
 *             the .wasm is not served next to the .js.
 */
declare function createOpen3SDCM(init?: object): Promise<Open3SDCMModule>;

export default createOpen3SDCM;
