// Edge-case tests for the Open3SDCM WebAssembly API (Node), complementing
// web-demo-test.mjs (happy paths) and smoke-test.mjs (native cross-check).
//
// Run from the repo root:
//   node Wasm/test/api-test.mjs <textured.dcm> <untextured.dcm>
//
// e.g. TestData/real-world/scan_012.dcm "TestData/Hole3x5/Hole 3x5.dcm".
// The untextured scan should be small: it is also fed through the slow
// plain-array input path. Covers: getters before a parse, malformed input
// (no throw, no mesh, no MEMFS leftovers), every accepted input type, parser
// reuse (no stale texture), export directory handling, delete(), and the
// STL comparison used by smoke-test.mjs. The WebAssembly module defaults to
// builds/wasm-release/bin/open3sdcm.js; set OPEN3SDCM_WASM_JS to override.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

import { compareStl } from "./stl-compare.mjs";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const moduleJs = path.resolve(
    process.env.OPEN3SDCM_WASM_JS || path.join(repoRoot, "builds/wasm-release/bin/open3sdcm.js"));
const INPUT_PATH = "/open3sdcm-input.dcm";
const OUTPUT_DIR = "/open3sdcm-output";

const failures = [];
function check(name, cond, detail = "") {
  console.log(`${cond ? "ok  " : "FAIL"} ${name}${detail ? " - " + detail : ""}`);
  if (!cond) failures.push(name);
}

const [texturedPath, untexturedPath, ...extra] = process.argv.slice(2);
if (!texturedPath || !untexturedPath || extra.length > 0) {
  console.error("FAIL: usage: node api-test.mjs <textured.dcm> <untextured.dcm>");
  process.exit(1);
}
if (!fs.existsSync(moduleJs)) {
  console.error(`FAIL: WebAssembly module not found: ${moduleJs} (set OPEN3SDCM_WASM_JS or build it first)`);
  process.exit(1);
}

const texturedBytes = new Uint8Array(fs.readFileSync(texturedPath));
const untexturedBytes = new Uint8Array(fs.readFileSync(untexturedPath));

// The library reports parse errors on stderr; count them instead of
// printing them, so expected failures do not clutter the output.
let libraryErrors = 0;
const { default: createOpen3SDCM } = await import(pathToFileURL(moduleJs).href);
const Open3SDCM = await createOpen3SDCM({ printErr: () => { libraryErrors++; } });

function sameArray(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) {
    if (!Object.is(a[i], b[i])) return false;
  }
  return true;
}

function outputFiles() {
  return Open3SDCM.FS.analyzePath(OUTPUT_DIR).exists
    ? Open3SDCM.FS.readdir(OUTPUT_DIR).filter((f) => f !== "." && f !== "..")
    : [];
}

function expectEmpty(label, parser) {
  check(`${label}: hasMesh() is false`, !parser.hasMesh());
  check(`${label}: counts are 0`, parser.vertexCount() === 0 && parser.triangleCount() === 0,
        `${parser.vertexCount()} / ${parser.triangleCount()}`);
  check(`${label}: all views are empty`,
        parser.vertices().length === 0 && parser.triangles().length === 0 &&
        parser.uv().length === 0 && parser.textureImage().length === 0);
  check(`${label}: baseColor() is -1`, parser.baseColor() === -1, `${parser.baseColor()}`);
  check(`${label}: exportMeshBytes() returns nothing`, parser.exportMeshBytes("stl", "x").length === 0);
}

function withParser(body) {
  const parser = new Open3SDCM.DCMParser();
  try {
    body(parser);
  } catch (e) {
    check("no unexpected exception", false, e instanceof Error ? (e.stack ?? e.message) : String(e));
  } finally {
    parser.delete();
  }
}

// --- 1. Getters before any parse -------------------------------------------
console.log("\n== before any parse");
withParser((parser) => expectEmpty("fresh parser", parser));

// --- 2. Malformed input: no throw, no mesh, nothing left in MEMFS ----------
console.log("\n== malformed input");
withParser((parser) => {
  let seed = 12345;
  const random = new Uint8Array(4096).map(() => (seed = (seed * 1103515245 + 12345) >>> 0) >>> 24);
  const cases = [
    ["empty Uint8Array", new Uint8Array(0)],
    ["random bytes", random],
    ["truncated DCM", texturedBytes.subarray(0, texturedBytes.length >> 1)],
    ["plain text", new TextEncoder().encode("not a dcm file")],
    ["null", null],
    ["undefined", undefined],
    ["plain object", {}],
  ];
  for (const [label, input] of cases) {
    let threw = null;
    try {
      parser.parseBytes(input);
    } catch (e) {
      threw = e;
    }
    check(`${label}: parseBytes() does not throw`, threw === null, threw ? String(threw) : "");
    expectEmpty(label, parser);
    check(`${label}: no input file left in MEMFS`, !Open3SDCM.FS.analyzePath(INPUT_PATH).exists);
  }
  check("the library reported the parse errors", libraryErrors > 0, `${libraryErrors} messages`);

  // A good parse on the same parser still works after all of the above.
  parser.parseBytes(texturedBytes);
  check("good parse after failures: hasMesh()", parser.hasMesh());
  check("good parse after failures: texture and UVs present",
        parser.textureImage().length > 0 && parser.uv().length === parser.triangleCount() * 6);
});

// --- 3. Every accepted input type parses to the same mesh ------------------
console.log("\n== input types");
withParser((parser) => {
  parser.parseBytes(untexturedBytes);
  const reference = parser.vertices().slice();
  check("Uint8Array: hasMesh()", parser.hasMesh(), `${parser.vertexCount()} vertices`);

  // A heap view: readFile() returns a view straight onto WebAssembly memory.
  Open3SDCM.FS.writeFile("/api-test-heap.dcm", untexturedBytes);
  const heapView = parser.readFile("/api-test-heap.dcm");
  check("readFile() returns a view onto the WebAssembly heap",
        heapView.buffer === Open3SDCM.HEAPU8.buffer && heapView.length === untexturedBytes.length);

  const evenLength = untexturedBytes.length & ~1;
  const inputs = [
    ["ArrayBuffer", untexturedBytes.slice().buffer],
    ["plain Array", Array.from(untexturedBytes)],
    ["DataView", new DataView(untexturedBytes.slice().buffer)],
    ["Uint8Array subarray (non-zero byteOffset)", (() => {
      const padded = new Uint8Array(untexturedBytes.length + 7);
      padded.set(untexturedBytes, 7);
      return padded.subarray(7);
    })()],
    ["WebAssembly heap view", heapView],
  ];
  for (const [label, input] of inputs) {
    parser.parseBytes(input);
    check(`${label}: same vertices as Uint8Array`, sameArray(parser.vertices().slice(), reference),
          `${parser.vertexCount()} vertices`);
  }
  // A non-Uint8 typed array contributes its raw bytes, whatever the element
  // type (only checked for an even-length prefix: the parse of a truncated
  // file must simply not crash).
  const wide = new Uint16Array(untexturedBytes.slice(0, evenLength).buffer);
  parser.parseBytes(wide);
  check("Uint16Array: raw bytes are used", evenLength !== untexturedBytes.length || parser.hasMesh());
  Open3SDCM.FS.unlink("/api-test-heap.dcm");
});

// --- 4. Reusing one parser never leaks state between scans -----------------
console.log("\n== parser reuse");
withParser((parser) => {
  parser.parseBytes(texturedBytes);
  const texturedTriangles = parser.triangleCount();
  const textureLength = parser.textureImage().length;
  check("textured scan: texture present", textureLength > 0, `${textureLength} bytes`);

  parser.parseBytes(untexturedBytes);
  check("untextured scan after a textured one: no stale texture", parser.textureImage().length === 0);
  check("untextured scan after a textured one: no stale UVs", parser.uv().length === 0);

  parser.parseBytes(texturedBytes);
  check("textured scan again: texture and UVs back",
        parser.textureImage().length === textureLength &&
        parser.uv().length === texturedTriangles * 6);
});

// --- 5. Export directory handling -------------------------------------------
console.log("\n== exports");
withParser((parser) => {
  parser.parseBytes(texturedBytes);
  const obj = parser.exportMeshBytes("obj", "x").slice();
  const objFiles = outputFiles().sort();
  check("obj export writes the .obj, .mtl and texture", obj.length > 0 && objFiles.length === 3,
        objFiles.join(", "));
  const stl = parser.exportMeshBytes("stl", "x").slice();
  check("a later export replaces the previous one", stl.length > 0 &&
        sameArray(outputFiles(), ["x.stl"]), outputFiles().join(", "));
  check("unknown format: empty result", parser.exportMeshBytes("bogus", "x").length === 0);
  check("unknown format: leaves no files behind", outputFiles().length === 0, outputFiles().join(", "));
  check("stlb is an alias of stl", sameArray(parser.exportMeshBytes("stlb", "x").slice(), stl));
});

// --- 6. delete() -------------------------------------------------------------
console.log("\n== delete()");
{
  const parser = new Open3SDCM.DCMParser();
  parser.parseBytes(untexturedBytes);
  parser.delete();
  check("isDeleted() after delete()", parser.isDeleted());
  let threw = false;
  try {
    parser.hasMesh();
  } catch {
    threw = true;
  }
  check("calling a deleted parser throws", threw);
}

// --- 7. STL comparison used by smoke-test.mjs -------------------------------
console.log("\n== stl-compare");
{
  // One-facet binary STL: normal followed by three vertices.
  const facetStl = (normal, vertices) => {
    const bytes = new Uint8Array(84 + 50);
    const view = new DataView(bytes.buffer);
    view.setUint32(80, 1, true);
    [normal, ...vertices].flat().forEach((value, i) => view.setFloat32(84 + i * 4, value, true));
    return bytes;
  };
  const triangle = [[0, 0, 0], [1, 0, 0], [0, 1, 0]];
  const sliver = [[0, 0, 0], [0.4, 0, 0], [0.4, 2e-4, 0]];

  check("identical files match", compareStl(facetStl([0, 0, 1], triangle), facetStl([0, 0, 1], triangle)).ok);
  check("a 1e-3 normal difference on a well-shaped facet fails",
        !compareStl(facetStl([0, 1e-3, 1], triangle), facetStl([0, 0, 1], triangle)).ok);
  check("a 2.6e-5 normal difference on a sliver facet passes",
        compareStl(facetStl([0, 2.6e-5, 1], sliver), facetStl([0, 0, 1], sliver)).ok);
  check("a non-finite normal fails",
        !compareStl(facetStl([0, NaN, 1], triangle), facetStl([0, 0, 1], triangle)).ok);
  const moved = triangle.map((v) => v.slice());
  moved[1][0] = Math.fround(1 + 2 ** -23);
  check("a one-ulp vertex difference fails",
        !compareStl(facetStl([0, 0, 1], moved), facetStl([0, 0, 1], triangle)).ok);
  check("a truncated file fails",
        !compareStl(facetStl([0, 0, 1], triangle).subarray(0, 100), facetStl([0, 0, 1], triangle)).ok);
}

console.log(failures.length ? `\n${failures.length} FAILURES` : "\nall checks passed");
process.exit(failures.length ? 1 : 0);
