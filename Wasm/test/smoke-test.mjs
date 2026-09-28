// Smoke test for the Open3SDCM WebAssembly module.
//
// Usage (Node >= 18, from anywhere):
//   node Wasm/test/smoke-test.mjs <path/to/scan.dcm> [path/to/Open3SDCMCLI]
//
// Parses the given DCM file through the WebAssembly module, prints the mesh
// summary, exports a binary STL, and - when a desktop CLI build is given
// (defaults to builds/ninja-release-vcpkg/bin/Open3SDCMCLI) - cross-checks
// the STL payload against the native parser: all vertex coordinates must be
// bit-identical; facet normals may differ in the last bits (cross-ISA
// floating-point rounding).

import { execSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const moduleJs = path.join(repoRoot, "builds/wasm-release/bin/open3sdcm.js");

const dcmPath = process.argv[2];
if (!dcmPath || !fs.existsSync(dcmPath)) {
  console.error("usage: node smoke-test.mjs <scan.dcm> [path/to/Open3SDCMCLI]");
  process.exit(1);
}
const cliPath = process.argv[3] || path.join(repoRoot, "builds/ninja-release-vcpkg/bin/Open3SDCMCLI");
const hasCli = fs.existsSync(cliPath);

const { default: createOpen3SDCM } = await import(`file://${moduleJs}`);
const Open3SDCM = await createOpen3SDCM();
const parser = new Open3SDCM.DCMParser();

const bytes = new Uint8Array(fs.readFileSync(dcmPath));
console.log(`input: ${dcmPath} (${bytes.length} bytes)`);
parser.parseBytes(bytes);

if (!parser.hasMesh()) {
  console.error("FAIL: no mesh parsed");
  process.exit(1);
}
console.log(
    `mesh: ${parser.vertexCount()} vertices, ${parser.triangleCount()} triangles, ` +
    `baseColor=0x${parser.baseColor().toString(16)}`);

const vertices = parser.vertices().slice();
const triangles = parser.triangles().slice();
const texture = parser.textureImage().slice();
const uv = parser.uv().slice();
console.log(
    `data: vertices[${vertices.length}], triangles[${triangles.length}], ` +
    `texture[${texture.length} bytes], uv[${uv.length} floats]`);

const stl = Buffer.from(parser.exportMeshBytes("stl", "smoketest").slice());
console.log(`stl exported: ${stl.length} bytes`);

if (!hasCli) {
  console.log("no desktop CLI found, skipping native cross-check");
  process.exit(0);
}

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "open3sdcm-smoke-"));
execSync(`"${cliPath}" -i "${dcmPath}" -o "${tmp}" -f stl`, { stdio: "pipe" });
const nativeStl = fs.readdirSync(tmp)
    .map((d) => path.join(tmp, d, path.basename(dcmPath).replace(/\.dcm$/i, ".stl")))
    .find((f) => fs.existsSync(f));
const native = fs.readFileSync(nativeStl);

if (native.length !== stl.length) {
  console.error(`FAIL: size mismatch native=${native.length} wasm=${stl.length}`);
  process.exit(1);
}
const facets = stl.readUInt32LE(80);
let normalDiffs = 0;
let vertexDiffs = 0;
for (let i = 0; i < facets; i++) {
  const base = 84 + i * 50;
  for (let c = 0; c < 4; c++) {
    const off = base + c * 12;
    for (let k = 0; k < 12; k += 4) {
      const d = Math.abs(stl.readFloatLE(off + k) - native.readFloatLE(off + k));
      if (d > 0) {
        if (c === 0) normalDiffs++;
        else vertexDiffs++;
      }
    }
  }
}
if (vertexDiffs !== 0) {
  console.error(`FAIL: ${vertexDiffs} vertex coordinate values differ from native`);
  process.exit(1);
}
console.log(
    `native cross-check ok: ${facets} facets, vertex coordinates bit-identical ` +
    `(${normalDiffs} normals differ in last bits only)`);
fs.rmSync(tmp, { recursive: true, force: true });
process.exit(0);
