// Node integration test for the web demo's WASM wiring (the part of web/app.js
// that touches the WASM module: parse -> views -> uv flip -> exports ->
// OBJ companions via FS.readdir). Rendering (three.js/DOM) is not covered.
//
// Run from the repo root:
//   node Wasm/test/web-demo-test.mjs

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const repoRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..", "..");
const moduleJs = path.join(repoRoot, "builds/wasm-release/bin/open3sdcm.js");
const OUTPUT_DIR = "/open3sdcm-output";

const failures = [];
function check(name, cond, detail = "") {
  console.log(`${cond ? "ok  " : "FAIL"} ${name}${detail ? " - " + detail : ""}`);
  if (!cond) failures.push(name);
}

const { default: createOpen3SDCM } = await import(`file://${moduleJs}`);
const Open3SDCM = await createOpen3SDCM();
check("module loads with FS runtime method exported", typeof Open3SDCM.FS?.readdir === "function");

for (const dcm of process.argv.slice(2)) {
  console.log(`\n== ${dcm}`);
  const parser = new Open3SDCM.DCMParser();
  parser.parseBytes(new Uint8Array(fs.readFileSync(dcm)));

  check(`${dcm}: hasMesh`, parser.hasMesh(),
        `${parser.vertexCount()} verts / ${parser.triangleCount()} tris`);

  const vertices = parser.vertices().slice();
  const triangles = parser.triangles().slice();
  const rawUvs = parser.uv().slice();
  const hasUvs = rawUvs.length === parser.triangleCount() * 3 * 2;
  const texture = parser.textureImage().slice();
  console.log(`    texture bytes: ${texture.length}, uv floats: ${rawUvs.length}`);

  // Mimic the demo's geometry expansion (per-corner, with the v flip).
  const triCount = parser.triangleCount();
  let ok = true;
  for (let i = 0; i < triCount && i < 100; i++) {
    for (let c = 0; c < 3; c++) {
      const idx = triangles[i * 3 + c];
      if (idx * 3 + 2 >= vertices.length) ok = false;
      if (hasUvs && Number.isNaN(rawUvs[(i * 3 + c) * 2]) && idx < 0) ok = false;
    }
  }
  check(`${dcm}: triangles index into vertices`, ok);

  // Exports through the library writers, and OBJ companion discovery.
  for (const format of ["stl", "ply", "obj"]) {
    if (Open3SDCM.FS.analyzePath(OUTPUT_DIR).exists) {
      for (const stale of Open3SDCM.FS.readdir(OUTPUT_DIR)) {
        if (stale !== "." && stale !== "..") Open3SDCM.FS.unlink(`${OUTPUT_DIR}/${stale}`);
      }
    }
    const name = "demo";
    const main = parser.exportMeshBytes(format, name).slice();
    check(`${dcm}: exportMeshBytes("${format}")`, main.length > 0, `${main.length} bytes`);
    const files = Open3SDCM.FS.readdir(OUTPUT_DIR).filter((f) => f !== "." && f !== "..");
    if (format === "obj") {
      const companions = files.filter((f) => f === "demo.mtl" || f.startsWith("demo_texture"));
      check(`${dcm}: obj companions present`, companions.length > 0, files.join(", "));
      const readable = companions.map((f) => parser.readFile(`${OUTPUT_DIR}/${f}`).slice());
      check(`${dcm}: obj companions readable`, readable.every((b) => b.length > 0));
    }
    if (format === "stl") {
      check(`${dcm}: stl size`, main.length === 84 + triCount * 50, `${main.length}`);
    }
  }
}

console.log(failures.length ? `\n${failures.length} FAILURES` : "\nall checks passed");
process.exit(failures.length ? 1 : 0);
