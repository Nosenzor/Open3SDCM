// Binary STL comparison used by smoke-test.mjs.
//
// Pure function on two byte arrays (no wasm, no filesystem), so it can be
// exercised on its own: compareStl(a, b) -> result object.
//
// Semantics (the cross-ISA contract between the WebAssembly build and the
// native parser):
//  - the 80-byte header is ignored;
//  - both files must be well-formed binary STL (size === 84 + 50 * facets)
//    and carry the same facet count;
//  - all vertex coordinates must be BIT-identical (raw float32 bits, so a
//    -0/+0 or NaN payload difference is a difference);
//  - facet normals are float32 cross products of the facet's edges and may
//    differ in the last bits across ISAs. Rounding in that cross product is
//    amplified by 1/sin of the angle between the two edges, so a sliver
//    facet's normal legitimately differs far more than a well-shaped one's
//    (e.g. 2.6e-5 for a 0.4 x 0.0002 sliver of TestData scan_040). Each
//    normal component must be finite and within `normalTolerance` (the limit
//    for a well-shaped facet) times that factor; a facet with zero area has
//    no defined normal and only needs finite components.

const HEADER_BYTES = 80;
const COUNT_END = 84;
const FACET_BYTES = 50;
const MAX_REPORTED_ERRORS = 5;

function viewOf(bytes) {
  return new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
}

// 1/sin of the angle between the facet's edges v0->v1 and v0->v2 (>= 1),
// computed in double precision; Infinity for a zero-area facet.
function normalConditioning(view, base) {
  const vertex = (corner) => [0, 4, 8].map((k) => view.getFloat32(base + corner * 12 + k, true));
  const [v0, v1, v2] = [vertex(1), vertex(2), vertex(3)];
  const e1 = v1.map((x, i) => x - v0[i]);
  const e2 = v2.map((x, i) => x - v0[i]);
  const cross = Math.hypot(e1[1] * e2[2] - e1[2] * e2[1],
                           e1[2] * e2[0] - e1[0] * e2[2],
                           e1[0] * e2[1] - e1[1] * e2[0]);
  return cross === 0 ? Infinity : (Math.hypot(...e1) * Math.hypot(...e2)) / cross;
}

// Returns the facet count when `bytes` is a well-formed binary STL, else an
// error string.
function facetCount(bytes, label) {
  if (bytes.byteLength < COUNT_END) {
    return { error: `${label}: ${bytes.byteLength} bytes is too short for a binary STL header` };
  }
  const facets = viewOf(bytes).getUint32(HEADER_BYTES, true);
  const expected = COUNT_END + FACET_BYTES * facets;
  if (bytes.byteLength !== expected) {
    return {
      error: `${label}: header declares ${facets} facets (${expected} bytes) but the file is ${bytes.byteLength} bytes`,
    };
  }
  return { facets };
}

/**
 * @param {Uint8Array} actual     STL bytes under test (e.g. exported by the wasm build).
 * @param {Uint8Array} reference  Reference STL bytes (native parser or committed file).
 * @param {{normalTolerance?: number}} [options]
 * @returns {{ok: boolean, facets: number, referenceFacets: number,
 *            vertexDiffs: number, normalDiffs: number, maxNormalDelta: number,
 *            errors: string[]}}
 */
export function compareStl(actual, reference, { normalTolerance = 1e-5 } = {}) {
  const result = {
    ok: false,
    facets: 0,
    referenceFacets: 0,
    vertexDiffs: 0,
    normalDiffs: 0,
    maxNormalDelta: 0,
    errors: [],
  };
  const fail = (message) => {
    if (result.errors.length < MAX_REPORTED_ERRORS) result.errors.push(message);
  };

  const a = facetCount(actual, "actual");
  const b = facetCount(reference, "reference");
  if (a.error) fail(a.error);
  if (b.error) fail(b.error);
  if (a.error || b.error) return result;

  result.facets = a.facets;
  result.referenceFacets = b.facets;
  if (a.facets !== b.facets) {
    fail(`facet count mismatch: actual=${a.facets} reference=${b.facets}`);
    return result;
  }

  const va = viewOf(actual);
  const vb = viewOf(reference);
  let badNormals = 0;
  let badVertexFacets = 0;
  for (let i = 0; i < a.facets; i++) {
    const base = COUNT_END + i * FACET_BYTES;
    const tolerance = normalTolerance * normalConditioning(vb, base);
    // Corner 0 is the facet normal, corners 1..3 are the vertices.
    for (let corner = 0; corner < 4; corner++) {
      for (let k = 0; k < 12; k += 4) {
        const off = base + corner * 12 + k;
        if (corner > 0) {
          if (va.getUint32(off, true) !== vb.getUint32(off, true)) {
            result.vertexDiffs++;
            if (badVertexFacets++ < MAX_REPORTED_ERRORS) {
              fail(`facet ${i} vertex ${corner - 1} coordinate ${k / 4} differs ` +
                   `(actual ${va.getFloat32(off, true)}, reference ${vb.getFloat32(off, true)})`);
            }
          }
          continue;
        }
        const x = va.getFloat32(off, true);
        const y = vb.getFloat32(off, true);
        const delta = Math.abs(x - y);
        if (!Number.isFinite(x) || !Number.isFinite(y) || delta > tolerance) {
          if (badNormals++ < MAX_REPORTED_ERRORS) {
            fail(`facet ${i} normal component ${k / 4} out of tolerance ` +
                 `(actual ${x}, reference ${y}, tolerance ${tolerance.toExponential(2)})`);
          }
          continue;
        }
        if (delta > 0) result.normalDiffs++;
        if (delta > result.maxNormalDelta) result.maxNormalDelta = delta;
      }
    }
  }
  if (badVertexFacets > MAX_REPORTED_ERRORS) {
    result.errors.push(`... ${result.vertexDiffs} vertex coordinate values differ in total`);
  }
  if (badNormals > MAX_REPORTED_ERRORS) {
    result.errors.push(`... ${badNormals} facet normal components out of tolerance in total`);
  }
  result.ok = result.errors.length === 0 && result.vertexDiffs === 0 && badNormals === 0;
  return result;
}
