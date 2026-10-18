// Smoke test for the Open3SDCM WebAssembly module.
//
// Usage (Node >= 18, from anywhere):
//   node Wasm/test/smoke-test.mjs <scan.dcm> [<path/to/Open3SDCMCLI> | <reference.stl>] [--allow-skip]
//
// Parses the given DCM file through the WebAssembly module, prints the mesh
// summary, exports a binary STL and cross-checks it against a reference:
//   - a path ending in .stl is used as the reference file directly (e.g. the
//     committed TestData/*/dcm2stlapp_*.stl files);
//   - anything else is the desktop CLI (default
//     builds/ninja-release-vcpkg/bin/Open3SDCMCLI), which is run to produce
//     the native STL.
// All vertex coordinates must be bit-identical; facet normals may differ in
// the last bits (cross-ISA floating-point rounding) but must stay within
// 1e-5, scaled up for sliver facets (see stl-compare.mjs).
//
// With neither a reference nor a CLI available the test FAILS, unless
// --allow-skip is given (then the cross-check is skipped with a notice).
//
// The WebAssembly module defaults to builds/wasm-release/bin/open3sdcm.js;
// set OPEN3SDCM_WASM_JS to test another build.

import { execFileSync } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

import { compareStl } from "./stl-compare.mjs";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const moduleJs = path.resolve(
    process.env.OPEN3SDCM_WASM_JS || path.join(repoRoot, "builds/wasm-release/bin/open3sdcm.js"));
const defaultCli = path.join(repoRoot, "builds/ninja-release-vcpkg/bin/Open3SDCMCLI");
const USAGE =
    "usage: node smoke-test.mjs <scan.dcm> [<path/to/Open3SDCMCLI> | <reference.stl>] [--allow-skip]";

const failures = [];
function check(name, cond, detail = "") {
  console.log(`${cond ? "ok  " : "FAIL"} ${name}${detail ? " - " + detail : ""}`);
  if (!cond) failures.push(name);
}

const args = process.argv.slice(2);
const flags = args.filter((a) => a.startsWith("--"));
const positional = args.filter((a) => !a.startsWith("--"));
const unknownFlags = flags.filter((f) => f !== "--allow-skip");
if (unknownFlags.length > 0 || positional.length < 1 || positional.length > 2) {
  console.error(`FAIL: ${unknownFlags.length ? "unknown option " + unknownFlags[0] : "bad arguments"}\n${USAGE}`);
  process.exit(1);
}
const allowSkip = flags.includes("--allow-skip");
const dcmPath = positional[0];
if (!fs.existsSync(dcmPath)) {
  console.error(`FAIL: input scan not found: ${dcmPath}\n${USAGE}`);
  process.exit(1);
}
const refArg = positional[1];
const referenceStlPath = refArg && /\.stl$/i.test(refArg) ? refArg : undefined;
const cliPath = referenceStlPath ? undefined : (refArg || defaultCli);

if (!fs.existsSync(moduleJs)) {
  console.error(`FAIL: WebAssembly module not found: ${moduleJs} (set OPEN3SDCM_WASM_JS or build it first)`);
  process.exit(1);
}

// Resolves the reference STL: either the given .stl file or the native CLI's
// output. Returns { bytes, label }, or { unavailable: "<why>" } when there is
// nothing to compare against; throws when a CLI is present but fails.
function loadReference() {
  if (referenceStlPath) {
    if (!fs.existsSync(referenceStlPath)) return { unavailable: `reference STL not found: ${referenceStlPath}` };
    return { bytes: fs.readFileSync(referenceStlPath), label: `reference ${referenceStlPath}` };
  }
  if (!fs.existsSync(cliPath)) return { unavailable: `desktop CLI not found: ${cliPath}` };

  const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "open3sdcm-smoke-"));
  try {
    // execFileSync: the paths are passed as argv, never through a shell.
    execFileSync(cliPath, ["-i", dcmPath, "-o", tmp, "-f", "stl"], { stdio: "pipe" });
    // The CLI writes <tmp>/<timestamp>/<scan>.stl.
    const stlName = path.basename(dcmPath).replace(/\.dcm$/i, ".stl");
    const nativeStl = fs.readdirSync(tmp)
        .map((d) => path.join(tmp, d, stlName))
        .find((f) => fs.existsSync(f));
    if (!nativeStl) throw new Error(`desktop CLI produced no ${stlName} in ${tmp}`);
    return { bytes: fs.readFileSync(nativeStl), label: `native CLI ${cliPath}` };
  } finally {
    fs.rmSync(tmp, { recursive: true, force: true });
  }
}

function runChecks(parser) {
  const bytes = new Uint8Array(fs.readFileSync(dcmPath));
  console.log(`input: ${dcmPath} (${bytes.length} bytes)`);
  parser.parseBytes(bytes);

  check("hasMesh", parser.hasMesh());
  if (!parser.hasMesh()) return;
  const vertexCount = parser.vertexCount();
  const triangleCount = parser.triangleCount();
  const baseColor = parser.baseColor();
  console.log(
      `mesh: ${vertexCount} vertices, ${triangleCount} triangles, ` +
      `baseColor=${baseColor < 0 ? "none" : "0x" + baseColor.toString(16).padStart(6, "0")}`);

  const vertices = parser.vertices().slice();
  const triangles = parser.triangles().slice();
  const texture = parser.textureImage().slice();
  const uv = parser.uv().slice();
  console.log(
      `data: vertices[${vertices.length}], triangles[${triangles.length}], ` +
      `texture[${texture.length} bytes], uv[${uv.length} floats]`);
  check("vertices() / triangles() lengths match the counts",
        vertices.length === vertexCount * 3 && triangles.length === triangleCount * 3);

  const stl = Buffer.from(parser.exportMeshBytes("stl", "smoketest").slice());
  console.log(`stl exported: ${stl.length} bytes`);
  check("wasm stl size is 84 + 50 * triangles", stl.length === 84 + 50 * triangleCount, `${stl.length}`);

  const reference = loadReference();
  if (reference.unavailable) {
    if (allowSkip) {
      console.log(`SKIP reference cross-check (--allow-skip): ${reference.unavailable}`);
    } else {
      check("reference available for the cross-check", false,
            `${reference.unavailable}; pass a reference .stl or a CLI path, or use --allow-skip`);
    }
    return;
  }
  const cmp = compareStl(stl, reference.bytes);
  for (const message of cmp.errors) console.error(`    ${message}`);
  check(`stl matches ${reference.label}`, cmp.ok,
        `${cmp.facets} facets, ${cmp.vertexDiffs} vertex coordinate diffs, ` +
        `${cmp.normalDiffs} normals differ, max normal delta ${cmp.maxNormalDelta.toExponential(2)} ` +
        `(limit 1e-5 x facet conditioning)`);
}

const { default: createOpen3SDCM } = await import(pathToFileURL(moduleJs).href);
const Open3SDCM = await createOpen3SDCM();
const parser = new Open3SDCM.DCMParser();
try {
  runChecks(parser);
} catch (e) {
  check("no unexpected exception", false, e instanceof Error ? (e.stack ?? e.message) : String(e));
} finally {
  parser.delete();
}

console.log(failures.length ? `\n${failures.length} FAILURES` : "\nall checks passed");
process.exit(failures.length ? 1 : 0);
