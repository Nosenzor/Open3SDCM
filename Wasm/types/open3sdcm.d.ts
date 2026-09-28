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
 *
 * Mesh-returning methods hand out typed-array VIEWS into WebAssembly
 * memory. A view is only valid until the next call on the parser; slice()
 * it to take ownership.
 */

export interface DCMParser {
  /**
   * Parses a DCM archive held in memory. Accepts a Uint8Array
   * (recommended, works for both external ArrayBuffers and WASM-heap
   * views) or a plain array of byte values. Any previously parsed state
   * is cleared first.
   */
  parseBytes(data: Uint8Array | ArrayLike<number>): void;

  /** True when the last parse produced a non-empty mesh. */
  hasMesh(): boolean;

  /** Number of vertices of the parsed mesh (vertices().length / 3). */
  vertexCount(): number;

  /** Number of triangles of the parsed mesh. */
  triangleCount(): number;

  /**
   * Flat (N, 3) float32 vertex positions. VIEW: valid until the next
   * call on this parser; call .slice() to keep the data.
   */
  vertices(): Float32Array;

  /**
   * Flat (M, 3) uint32 triangle indices (0-based, into vertices()).
   * VIEW: valid until the next call on this parser; call .slice() to
   * keep the data.
   */
  triangles(): Uint32Array;

  /** Mesh-wide base colour as 0xRRGGBB, or -1 when the scan carries none. */
  baseColor(): number;

  /**
   * Raw bytes of the first embedded texture image (typically JPEG),
   * empty when the scan has none. VIEW: call .slice() to keep.
   */
  textureImage(): Uint8Array;

  /**
   * Per-corner UV pairs (u0, v0, u1, v1, ...); NaN marks a corner whose
   * coordinate could not be decoded. Empty when the scan has none.
   * VIEW: call .slice() to keep.
   */
  uv(): Float32Array;

  /**
   * Exports the parsed mesh to "stl", "ply" or "obj" and returns the
   * bytes of the main file. For "obj" only the .obj bytes are returned;
   * use exportMeshTo()/readFile() to fetch the companion .mtl/texture.
   * VIEW: call .slice() to keep. Empty on failure or an empty mesh.
   *
   * @param format   "stl" (alias "stlb"), "ply" or "obj". Defaults to "stl".
   * @param baseName File name without extension. Defaults to "mesh".
   */
  exportMeshBytes(format?: string, baseName?: string): Uint8Array;

  /**
   * Writes the mesh to any path of the Emscripten virtual filesystem
   * (e.g. an IDBFS mount set up through the FS runtime method). Returns
   * the library's success flag.
   */
  exportMeshTo(virtualPath: string, format?: string): boolean;

  /**
   * Reads any file from the Emscripten virtual filesystem.
   * VIEW: call .slice() to keep.
   */
  readFile(virtualPath: string): Uint8Array;
}

export interface Open3SDCMModule {
  DCMParser: new () => DCMParser;
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
