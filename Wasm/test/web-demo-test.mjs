// Node integration test for the web demo's WASM wiring (the part of web/app.js
// that touches the WASM module: parse -> views -> uv -> exports -> OBJ
// companions via FS.readdir). Rendering (three.js/DOM, including the v flip)
// is not covered.
//
// Run from the repo root:
//   node Wasm/test/web-demo-test.mjs <scan.dcm> [<scan.dcm> ...]
//
// General invariants are checked for every scan. Scans listed in
// Wasm/test/fixtures.json (keyed by repo-relative POSIX path) additionally
// get exact expectations: vertex/triangle counts, base colour, texture size
// and UV presence. The WebAssembly module defaults to
// builds/wasm-release/bin/open3sdcm.js; set OPEN3SDCM_WASM_JS to override.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(testDir, "..", "..");
const moduleJs = path.resolve(
    process.env.OPEN3SDCM_WASM_JS || path.join(repoRoot, "builds/wasm-release/bin/open3sdcm.js"));
const OUTPUT_DIR = "/open3sdcm-output";

const failures = [];
function check(name, cond, detail = "") {
  console.log(`${cond ? "ok  " : "FAIL"} ${name}${detail ? " - " + detail : ""}`);
  if (!cond) failures.push(name);
}

function hex(color) {
  return color < 0 ? "none" : `0x${color.toString(16).padStart(6, "0")}`;
}

function sameBytes(a, b) {
  return Buffer.from(a.buffer, a.byteOffset, a.byteLength)
      .equals(Buffer.from(b.buffer, b.byteOffset, b.byteLength));
}

// Number of lines of `text` that start with `prefix`.
function countLines(text, prefix) {
  let count = 0;
  let pos = 0;
  while (pos < text.length) {
    if (text.startsWith(prefix, pos)) count++;
    const eol = text.indexOf("\n", pos);
    if (eol < 0) break;
    pos = eol + 1;
  }
  return count;
}

// Repo-relative POSIX path of a CLI argument (resolved against the cwd), the
// key format of fixtures.json.
function fixtureKey(arg) {
  return path.relative(repoRoot, path.resolve(arg)).split(path.sep).join("/");
}

const scans = process.argv.slice(2);
if (scans.length === 0) {
  console.error("FAIL: no DCM fixtures given - usage: node web-demo-test.mjs <scan.dcm> [...]");
  process.exit(1);
}
if (!fs.existsSync(moduleJs)) {
  console.error(`FAIL: WebAssembly module not found: ${moduleJs} (set OPEN3SDCM_WASM_JS or build it first)`);
  process.exit(1);
}

const expectations = JSON.parse(fs.readFileSync(path.join(testDir, "fixtures.json"), "utf8"));

const { default: createOpen3SDCM } = await import(pathToFileURL(moduleJs).href);
const Open3SDCM = await createOpen3SDCM();
check("module loads with FS runtime method exported", typeof Open3SDCM.FS?.readdir === "function");

function runScan(dcm, parser) {
  console.log(`\n== ${dcm}`);
  parser.parseBytes(new Uint8Array(fs.readFileSync(dcm)));

  check(`${dcm}: hasMesh`, parser.hasMesh(),
        `${parser.vertexCount()} verts / ${parser.triangleCount()} tris`);

  const vertices = parser.vertices().slice();
  const triangles = parser.triangles().slice();
  const rawUvs = parser.uv().slice();
  const triCount = parser.triangleCount();
  const vertexCount = parser.vertexCount();
  const baseColor = parser.baseColor();
  const texture = parser.textureImage().slice();
  console.log(`    baseColor: ${hex(baseColor)}, texture bytes: ${texture.length}, uv floats: ${rawUvs.length}`);

  check(`${dcm}: buffer lengths match the counts`,
        vertices.length === vertexCount * 3 && triangles.length === triCount * 3,
        `vertices ${vertices.length}, triangles ${triangles.length}`);

  // Real invariants: every triangle index must address the vertex buffer,
  // and UV corners (when present) must pair up with triangle corners.
  let indicesOk = true;
  for (let i = 0; i < triangles.length; i++) {
    if (triangles[i] * 3 + 2 >= vertices.length) indicesOk = false;
  }
  check(`${dcm}: all triangle indices address the vertex buffer`, indicesOk);
  check(`${dcm}: uv count matches triangle corners`,
        rawUvs.length === 0 || rawUvs.length === triCount * 6, `${rawUvs.length} floats`);

  // UV values: undecoded corners are NaN by contract; decoded ones must be
  // finite and inside the decoder's range. The DCM packs each component in
  // 15 bits plus an "outside" flag: in-range values decode to [0, 1], flagged
  // ones to [-256, 256) (ParseDcm.cpp, decodeComponent), so values outside
  // the unit square are legitimate.
  if (rawUvs.length > 0) {
    let uvValuesOk = true;
    for (let i = 0; i < rawUvs.length; i++) {
      const uv = rawUvs[i];
      if (Number.isNaN(uv)) continue;
      if (!Number.isFinite(uv) || uv < -256 || uv >= 256) uvValuesOk = false;
    }
    check(`${dcm}: decoded UV values are finite within [-256, 256)`, uvValuesOk);
  }

  // Exact per-fixture expectations (ground truth from the native parser).
  const key = fixtureKey(dcm);
  const expected = expectations[key];
  if (expected) {
    check(`${dcm}: fixture vertexCount`, vertexCount === expected.vertexCount,
          `${vertexCount}, expected ${expected.vertexCount}`);
    check(`${dcm}: fixture triangleCount`, triCount === expected.triangleCount,
          `${triCount}, expected ${expected.triangleCount}`);
    const expectedColor = Number(expected.baseColor);
    check(`${dcm}: fixture baseColor`, baseColor === expectedColor,
          `${hex(baseColor)}, expected ${hex(expectedColor)}`);
    check(`${dcm}: fixture texture bytes`, texture.length === expected.textureBytes,
          `${texture.length}, expected ${expected.textureBytes}`);
    const expectedUvFloats = expected.uv ? expected.triangleCount * 6 : 0;
    check(`${dcm}: fixture uv ${expected.uv ? "present" : "absent"}`, rawUvs.length === expectedUvFloats,
          `${rawUvs.length} floats, expected ${expectedUvFloats}`);
  } else {
    console.log(`    (no fixtures.json entry for ${key}: general checks only)`);
  }

  // Exports through the library writers, and OBJ companion discovery. Each
  // export empties the output directory first, so it only ever lists the
  // latest export's files (exactly what the demo downloads).
  for (const format of ["stl", "ply", "obj"]) {
    const name = "demo";
    const main = parser.exportMeshBytes(format, name).slice();
    check(`${dcm}: exportMeshBytes("${format}")`, main.length > 0, `${main.length} bytes`);
    const files = Open3SDCM.FS.analyzePath(OUTPUT_DIR).exists
      ? Open3SDCM.FS.readdir(OUTPUT_DIR).filter((f) => f !== "." && f !== "..")
      : [];
    if (format === "stl") {
      check(`${dcm}: stl size`, main.length === 84 + triCount * 50, `${main.length}`);
    }
    if (format !== "obj") {
      check(`${dcm}: ${format} export leaves only its own file`,
            files.length === 1 && files[0] === `${name}.${format}`, files.join(", "));
      continue;
    }

    const text = new TextDecoder().decode(main);
    if (rawUvs.length > 0) {
      const vtCount = countLines(text, "vt ");
      check(`${dcm}: obj has 3 vt lines per triangle`, vtCount === triCount * 3,
            `${vtCount} vt lines, expected ${triCount * 3}`);
    }
    if (baseColor >= 0 || texture.length > 0) {
      check(`${dcm}: obj companion demo.mtl present`, files.includes("demo.mtl"), files.join(", "));
      if (files.includes("demo.mtl")) {
        check(`${dcm}: demo.mtl readable`, parser.readFile(`${OUTPUT_DIR}/demo.mtl`).slice().length > 0);
      }
    }
    if (texture.length > 0) {
      const textures = files.filter((f) => f.startsWith("demo_texture0."));
      check(`${dcm}: exactly one demo_texture0.* companion`, textures.length === 1, files.join(", "));
      if (textures.length === 1) {
        const written = parser.readFile(`${OUTPUT_DIR}/${textures[0]}`).slice();
        check(`${dcm}: ${textures[0]} equals textureImage()`, sameBytes(written, texture),
              `${written.length} vs ${texture.length} bytes`);
      }
    }
  }
}

for (const dcm of scans) {
  const parser = new Open3SDCM.DCMParser();
  try {
    runScan(dcm, parser);
  } catch (e) {
    check(`${dcm}: no unexpected exception`, false, e instanceof Error ? (e.stack ?? e.message) : String(e));
  } finally {
    // Free the C++ object: without .delete() each iteration leaks the parsed
    // mesh on the wasm heap.
    parser.delete();
  }
}

console.log(failures.length ? `\n${failures.length} FAILURES` : "\nall checks passed");
process.exit(failures.length ? 1 : 0);
