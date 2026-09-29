// Open3SDCM web demo.
//
// Loads the Emscripten build of Open3SDCMLib (open3sdcm.js / open3sdcm.wasm
// built by the wasm-release CMake preset and deployed next to this file),
// parses the uploaded DCM in the browser and shows the mesh in a three.js
// viewer. Export buttons write STL / PLY / OBJ with the library's own
// C++ writers.
//
// Coordinate conventions to keep in mind:
//  * parser.vertices()/triangles() are zero-copy views into WASM memory;
//    everything is .slice()d immediately.
//  * parser.uv() returns per-corner coordinates in the DCM's top-left
//    origin convention; three.js textures use bottom-left origin, so v
//    is flipped here (the library's own OBJ writer does the same flip).

import * as THREE from "three";
import { OrbitControls } from "three/addons/controls/OrbitControls.js";
import createOpen3SDCM from "./open3sdcm.js";

const OUTPUT_DIR = "/open3sdcm-output";

const els = {
  fileInput: document.getElementById("file-input"),
  status: document.getElementById("status"),
  exportSection: document.getElementById("export"),
  exportStl: document.getElementById("export-stl"),
  exportPly: document.getElementById("export-ply"),
  exportObj: document.getElementById("export-obj"),
  viewer: document.getElementById("viewer"),
  viewerHint: document.getElementById("viewer-hint"),
  meshInfo: document.getElementById("mesh-info"),
};

let Open3SDCM = null;      // the instantiated WASM module
let parser = null;         // current Open3SDCM.DCMParser
let sceneMesh = null;      // current THREE.Mesh
let currentName = null;    // base name of the loaded scan
let busy = false;

// ---------------------------------------------------------------------------
// three.js boilerplate
// ---------------------------------------------------------------------------

const scene = new THREE.Scene();
scene.background = new THREE.Color(0x181c22);

const camera = new THREE.PerspectiveCamera(45, 1, 0.1, 5000);
camera.position.set(60, 40, 80);

const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setPixelRatio(window.devicePixelRatio);
els.viewer.appendChild(renderer.domElement);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;

scene.add(new THREE.HemisphereLight(0xffffff, 0x33383f, 1.6));
const keyLight = new THREE.DirectionalLight(0xffffff, 2.2);
keyLight.position.set(1, 1.5, 0.8);
scene.add(keyLight);

function resize() {
  const width = els.viewer.clientWidth;
  const height = els.viewer.clientHeight;
  renderer.setSize(width, height);
  camera.aspect = width / height;
  camera.updateProjectionMatrix();
}
window.addEventListener("resize", resize);
resize();

renderer.setAnimationLoop(() => {
  controls.update();
  renderer.render(scene, camera);
});

// ---------------------------------------------------------------------------
// WASM module
// ---------------------------------------------------------------------------

setStatus("Loading WebAssembly module…");
createOpen3SDCM()
    .then((mod) => {
      Open3SDCM = mod;
      setStatus("Ready — drop a .dcm scan to view it.");
    })
    .catch((err) => setStatus(`Failed to load the WASM module: ${err}`, true));

// ---------------------------------------------------------------------------
// Parsing and viewer
// ---------------------------------------------------------------------------

function setStatus(text, isError = false) {
  els.status.textContent = text;
  els.status.classList.toggle("error", isError);
  els.meshInfo.textContent = "";
}

function clearMesh() {
  if (sceneMesh) {
    sceneMesh.geometry.dispose();
    sceneMesh.material.map?.dispose();
    sceneMesh.material.dispose();
    scene.remove(sceneMesh);
    sceneMesh = null;
  }
}

async function loadDcmFile(file) {
  if (!Open3SDCM || busy) return;
  busy = true;
  clearMesh();

  // embind instances are raw C++ pointers: without .delete() the previous
  // parser and its whole decoded mesh would stay pinned on the wasm heap.
  parser?.delete();
  parser = null;

  // Drop files from a previous scan (e.g. its OBJ texture) so the OBJ
  // export below cannot pick them up as companions of this scan.
  if (Open3SDCM.FS.analyzePath(OUTPUT_DIR).exists) {
    for (const stale of Open3SDCM.FS.readdir(OUTPUT_DIR)) {
      if (stale !== "." && stale !== "..") Open3SDCM.FS.unlink(`${OUTPUT_DIR}/${stale}`);
    }
  }

  setExportEnabled(false);
  els.viewer.setAttribute("data-empty", "true");

  setStatus(`Parsing ${file.name} (${(file.size / 1048576).toFixed(1)} MB)…`);
  // Let the status repaint before the synchronous WASM parse.
  await new Promise((resolve) => setTimeout(resolve, 20));

  try {
    const bytes = new Uint8Array(await file.arrayBuffer());
    parser = new Open3SDCM.DCMParser();
    parser.parseBytes(bytes);

    if (!parser.hasMesh()) {
      setStatus(`${file.name}: no mesh found in this DCM file.`, true);
      return;
    }

    await buildMesh();
    currentName = file.name.replace(/\.dcm$/i, "");
    const info =
        `${currentName}: ${parser.vertexCount().toLocaleString()} vertices, ` +
        `${parser.triangleCount().toLocaleString()} triangles` +
        (parser.textureImage().length ? ", textured" : "");
    setStatus(info);
    setExportEnabled(true);
  } catch (err) {
    setStatus(`Failed to parse ${file.name}: ${err}`, true);
  } finally {
    busy = false;
  }
}

async function buildMesh() {
  const vertexCount = parser.vertexCount();
  const triangleCount = parser.triangleCount();

  // Non-indexed geometry: UVs are per triangle corner (a shared vertex can
  // carry a different UV in each triangle), which indexed geometry cannot
  // express.
  const positions = new Float32Array(triangleCount * 3 * 3);
  const uvs = new Float32Array(triangleCount * 3 * 2);

  const vertices = parser.vertices().slice();   // (N, 3) float32
  const triangles = parser.triangles().slice();  // (M, 3) uint32
  const rawUvs = parser.uv().slice();            // per-corner (u, v) or empty
  const hasUvs = rawUvs.length === triangleCount * 3 * 2;

  let p = 0;
  let t = 0;
  for (let i = 0; i < triangleCount; i++) {
    for (let c = 0; c < 3; c++) {
      const v = triangles[i * 3 + c] * 3;
      positions[p++] = vertices[v];
      positions[p++] = vertices[v + 1];
      positions[p++] = vertices[v + 2];

      if (hasUvs) {
        const u = rawUvs[t * 2];
        const v = rawUvs[t * 2 + 1];
        // NaN marks an undecoded corner; the DCM origin is top-left, three.js
        // expects bottom-left, hence the v flip.
        uvs[t * 2] = Number.isNaN(u) ? 0 : u;
        uvs[t * 2 + 1] = Number.isNaN(v) ? 0 : 1 - v;
      }
      t++;
    }
  }

  const geometry = new THREE.BufferGeometry();
  geometry.setAttribute("position", new THREE.BufferAttribute(positions, 3));
  if (hasUvs) geometry.setAttribute("uv", new THREE.BufferAttribute(uvs, 2));
  geometry.computeVertexNormals();

  const material = new THREE.MeshStandardMaterial({
    color: 0xffffff,
    roughness: 0.75,
    metalness: 0.0,
    side: THREE.DoubleSide,
  });

  const packed = parser.baseColor();

  const textureBytes = parser.textureImage().slice();
  if (textureBytes.length && hasUvs) {
    // The library embeds the image bytes as-is, typically JPEG (the
    // format also allows PNG); sniff the magic bytes for the blob type.
    const isPng = textureBytes[0] === 0x89 && textureBytes[1] === 0x50;
    // The DCM's UVs use a top-left image origin; app.js converts them to
    // three.js's bottom-left convention, so the image itself must be flipped
    // to match - and for ImageBitmap textures three.js ignores Texture.flipY,
    // the flip has to be baked in here.
    const bitmap = await createImageBitmap(
        new Blob([textureBytes], { type: isPng ? "image/png" : "image/jpeg" }),
        { imageOrientation: "flipY" });
    const texture = new THREE.Texture(bitmap);
    texture.colorSpace = THREE.SRGBColorSpace;
    texture.needsUpdate = true;
    material.map = texture;
  }

  if (!material.map && packed >= 0) {
    // The format carries a single mesh-wide tint; use it whenever there is
    // no texture to render.
    material.color.setHex(packed);
  }

  sceneMesh = new THREE.Mesh(geometry, material);
  scene.add(sceneMesh);
  els.viewer.removeAttribute("data-empty");

  fitCamera(geometry);
}

function fitCamera(geometry) {
  geometry.computeBoundingBox();
  const box = geometry.boundingBox;
  const center = box.getCenter(new THREE.Vector3());
  const size = box.getSize(new THREE.Vector3()).length() || 1;
  const distance = size * 1.2;

  camera.near = Math.max(size / 1000, 0.01);
  camera.far = size * 50;
  camera.position.set(
      center.x + distance * 0.6,
      center.y + distance * 0.45,
      center.z + distance * 0.65);
  camera.lookAt(center);
  camera.updateProjectionMatrix();

  controls.target.copy(center);
  controls.update();
}

// ---------------------------------------------------------------------------
// Export (STL / PLY / OBJ via the library's own C++ writers)
// ---------------------------------------------------------------------------

function setExportEnabled(enabled) {
  for (const button of [els.exportStl, els.exportPly, els.exportObj]) {
    button.disabled = !enabled;
  }
  els.exportSection.setAttribute("aria-hidden", String(!enabled));
}

function download(bytes, filename, mime = "application/octet-stream") {
  const url = URL.createObjectURL(new Blob([bytes], { type: mime }));
  const anchor = document.createElement("a");
  anchor.href = url;
  anchor.download = filename;
  anchor.click();
  setTimeout(() => URL.revokeObjectURL(url), 5000);
}

function exportMesh(format) {
  if (!parser || busy) return;
  if (format === "obj" && Open3SDCM.FS.analyzePath(OUTPUT_DIR).exists) {
    for (const entry of Open3SDCM.FS.readdir(OUTPUT_DIR)) {
      if (entry === `${currentName}.mtl` || entry.startsWith(`${currentName}_texture`)) {
        Open3SDCM.FS.unlink(`${OUTPUT_DIR}/${entry}`);
      }
    }
  }
  const main = parser.exportMeshBytes(format, currentName).slice();
  if (!main.length) {
    setStatus(`Export failed: the mesh could not be written as ${format.toUpperCase()}.`, true);
    return;
  }
  download(main, `${currentName}.${format === "stlb" ? "stl" : format}`);

  // OBJ carries colour through companion files (.mtl + texture image), which
  // the library writes next to the .obj in the virtual filesystem. The
  // directory also holds exports from the other buttons, so filter to this
  // scan's OBJ companions only.
  if (format === "obj") {
    for (const entry of Open3SDCM.FS.readdir(OUTPUT_DIR)) {
      if (entry === "." || entry === "..") continue;
      if (entry === `${currentName}.mtl` || entry.startsWith(`${currentName}_texture`)) {
        download(parser.readFile(`${OUTPUT_DIR}/${entry}`).slice(), entry);
      }
    }
  }
}

els.exportStl.addEventListener("click", () => exportMesh("stl"));
els.exportPly.addEventListener("click", () => exportMesh("ply"));
els.exportObj.addEventListener("click", () => exportMesh("obj"));

// ---------------------------------------------------------------------------
// Input wiring
// ---------------------------------------------------------------------------

els.fileInput.addEventListener("change", () => {
  if (els.fileInput.files.length) loadDcmFile(els.fileInput.files[0]);
  els.fileInput.value = "";
});

for (const eventName of ["dragenter", "dragover"]) {
  els.viewer.addEventListener(eventName, (event) => {
    event.preventDefault();
    els.viewer.setAttribute("data-dropping", "true");
  });
}
for (const eventName of ["dragleave", "drop"]) {
  els.viewer.addEventListener(eventName, (event) => {
    event.preventDefault();
    els.viewer.removeAttribute("data-dropping");
  });
}
els.viewer.addEventListener("drop", (event) => {
  const file = event.dataTransfer?.files?.[0];
  if (file) loadDcmFile(file);
});
window.addEventListener("dragover", (event) => event.preventDefault());
window.addEventListener("drop", (event) => event.preventDefault());
