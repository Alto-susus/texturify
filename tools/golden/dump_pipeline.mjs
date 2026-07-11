// Golden dump: run the reference JS pipeline modules on deterministic
// fixtures and write inputs + outputs for the C++ harness (texturify_verify).
// Usage: node tools/golden/dump_pipeline.mjs
//
// All fixture buffers are dumped so the C++ side READS them instead of
// regenerating (avoids trig/LCG divergence). Comparison policy:
//   - topology / counts / ids: bitwise
//   - float coords from trig-free paths: bitwise
//   - trig paths (cylindrical/spherical/rotation): ≤1e-5
import { writeFileSync, mkdirSync } from 'fs';
import { dirname, join } from 'path';
import { fileURLToPath } from 'url';

import { THREE } from '../../reference/js/threeCompat.js';
import { subdivide } from '../../reference/js/subdivision.js';
import { regularizeMesh } from '../../reference/js/regularize.js';
import { applyDisplacement } from '../../reference/js/displacement.js';
import { decimate } from '../../reference/js/decimation.js';
import { resolveTJunctions, countEdgeDefects, countAreaSlivers } from '../../reference/js/meshRepair.js';
import { buildAdjacency, bucketFill, buildFaceWeights } from '../../reference/js/exclusion.js';
import { computeUV } from '../../reference/js/mapping.js';
import { analyzeTexture } from '../../reference/js/textureAnalysis.js';
import { computeSmartResolution, computeRecommendedMaxTri, estimateSubdivisionTriCount } from '../../reference/js/smartResolution.js';
import { runFastDiagnostics, runExpensiveDiagnostics, getEdgePositions, getShellAssignments } from '../../reference/js/meshValidation.js';
import { runExportPipeline, snapBottomToFlat } from '../../reference/js/exportPipeline.js';

const outDir = join(dirname(fileURLToPath(import.meta.url)), 'out');
mkdirSync(outDir, { recursive: true });

const wf32 = (name, arr) => writeFileSync(join(outDir, name), Buffer.from(new Float32Array(arr).buffer));
const wf64 = (name, arr) => writeFileSync(join(outDir, name), Buffer.from(new Float64Array(arr).buffer));
const wi32 = (name, arr) => writeFileSync(join(outDir, name), Buffer.from(new Int32Array(arr).buffer));
const wu32 = (name, arr) => writeFileSync(join(outDir, name), Buffer.from(new Uint32Array(arr).buffer));
const wu8  = (name, arr) => writeFileSync(join(outDir, name), Buffer.from(new Uint8Array(arr).buffer));
const wjson = (name, obj) => writeFileSync(join(outDir, name), JSON.stringify(obj, null, 1));

// ── Fixtures ─────────────────────────────────────────────────────────────────

// Deterministic 32-bit LCG (identical constants on the C++ side if needed).
let seed = 0x1BADB002 >>> 0;
function rnd() {
  seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
  return seed / 4294967296;
}

// 20mm cube soup: 12 triangles, +Z up, centered at origin.
function makeCube(s = 10) {
  const p = [];
  const quad = (a, b, c, d) => { p.push(...a, ...b, ...c, ...a, ...c, ...d); };
  quad([-s,-s,-s],[-s, s,-s],[ s, s,-s],[ s,-s,-s]); // bottom (z-)
  quad([-s,-s, s],[ s,-s, s],[ s, s, s],[-s, s, s]); // top (z+)
  quad([-s,-s,-s],[ s,-s,-s],[ s,-s, s],[-s,-s, s]); // y-
  quad([ s, s,-s],[-s, s,-s],[-s, s, s],[ s, s, s]); // y+
  quad([ s,-s,-s],[ s, s,-s],[ s, s, s],[ s,-s, s]); // x+
  quad([-s, s,-s],[-s,-s,-s],[-s,-s, s],[-s, s, s]); // x-
  return new Float32Array(p);
}

// UV sphere soup, radius r, seg×ring quads (trig runs only here in JS; the
// dumped f32 buffer is the shared source of truth for both sides).
function makeSphere(r = 15, segs = 24, rings = 16) {
  const vert = (i, j) => {
    const phi = (j / rings) * Math.PI;         // 0..PI from +Z
    const theta = (i / segs) * 2 * Math.PI;    // 0..2PI
    return [
      Math.fround(r * Math.sin(phi) * Math.cos(theta)),
      Math.fround(r * Math.sin(phi) * Math.sin(theta)),
      Math.fround(r * Math.cos(phi)),
    ];
  };
  const p = [];
  for (let j = 0; j < rings; j++) {
    for (let i = 0; i < segs; i++) {
      const a = vert(i, j), b = vert(i + 1, j), c = vert(i + 1, j + 1), d = vert(i, j + 1);
      if (j > 0) p.push(...a, ...b, ...c);          // skip degenerate top cap tris
      if (j < rings - 1) p.push(...a, ...c, ...d);  // skip degenerate bottom cap
    }
  }
  return new Float32Array(p);
}

// 64×64 grayscale RGBA texture from the LCG, box-smoothed once so gradients
// are meaningful for textureAnalysis / smartResolution.
function makeTexture(w = 64, h = 64) {
  const raw = new Uint8Array(w * h);
  for (let i = 0; i < w * h; i++) raw[i] = (rnd() * 256) | 0;
  const data = new Uint8ClampedArray(w * h * 4);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      // 3×3 wrap-around box smooth for coherent gradients
      let sum = 0;
      for (let dy = -1; dy <= 1; dy++)
        for (let dx = -1; dx <= 1; dx++)
          sum += raw[((y + dy + h) % h) * w + ((x + dx + w) % w)];
      const g = Math.round(sum / 9);
      const o = (y * w + x) * 4;
      data[o] = data[o + 1] = data[o + 2] = g;
      data[o + 3] = 255;
    }
  }
  return { data, width: w, height: h };
}

function geoFrom(positions) {
  const g = new THREE.BufferGeometry();
  g.setAttribute('position', new THREE.BufferAttribute(positions, 3));
  return g;
}

function boundsOf(geometry) {
  geometry.computeBoundingBox();
  const bb = geometry.boundingBox;
  const size = new THREE.Vector3(); bb.getSize(size);
  const center = new THREE.Vector3(); bb.getCenter(center);
  return { min: bb.min, max: bb.max, size, center };
}

const dumpBounds = (name, b) => wf64(name, [
  b.min.x, b.min.y, b.min.z, b.max.x, b.max.y, b.max.z,
  b.size.x, b.size.y, b.size.z, b.center.x, b.center.y, b.center.z,
]);

const cubePos = makeCube();
const spherePos = makeSphere();
const tex = makeTexture();

wf32('fx_cube.f32', cubePos);
wf32('fx_sphere.f32', spherePos);
wu8('fx_tex.rgba', tex.data);
wjson('fx_tex.json', { width: tex.width, height: tex.height });

const sphereGeo = geoFrom(spherePos);
const sphereBounds = boundsOf(sphereGeo);
dumpBounds('fx_sphere_bounds.f64', sphereBounds);
const cubeGeo = geoFrom(cubePos);
const cubeBounds = boundsOf(cubeGeo);
dumpBounds('fx_cube_bounds.f64', cubeBounds);

// Settings snapshot — main.js defaults, triplanar, rotation 0 (trig-free).
const SETTINGS = {
  mappingMode: 5, scaleU: 0.5, scaleV: 0.5, amplitude: 0.5,
  offsetU: 0, offsetV: 0, rotation: 0,
  refineLength: 1.0, maxTriangles: 750000,
  bottomAngleLimit: 5, topAngleLimit: 0,
  mappingBlend: 1, seamBandWidth: 0.5, capAngle: 20,
  blendNormalSmoothing: 32, boundaryFalloff: 0,
  symmetricDisplacement: false, noDownwardZ: false,
  smoothBottom: true, harvestFlatFaces: true, harvestTol: 0.005,
  cylinderCenterX: null, cylinderCenterY: null, cylinderRadius: null,
  regularizeEnabled: true, regularizeSecondPassMul: 1.1,
};
// main.js _regularizeOpts() over the default settings
const REG_OPTS = {
  aspectThreshold: 5, slack: 3.0, aggressiveSlack: 8.0, extremeSliverAspect: 8,
  maxNormalDeltaCos: Math.cos(15 * Math.PI / 180),
  aggressiveNormalDeltaCos: Math.cos(25 * Math.PI / 180),
};

const dumpGeo = (prefix, g) => {
  wf32(`${prefix}_pos.f32`, g.attributes.position.array);
  if (g.attributes.normal) wf32(`${prefix}_nrm.f32`, g.attributes.normal.array);
  if (g.attributes.excludeWeight) wf32(`${prefix}_excl.f32`, g.attributes.excludeWeight.array);
};

// ── Exclusion (sphere, faces 0..9 excluded) ──────────────────────────────────
{
  const adj = buildAdjacency(sphereGeo);
  const triCount = spherePos.length / 9;
  wjson('excl_adj.json', {
    openEdgeCount: adj.openEdgeCount,
    nonManifoldEdgeCount: adj.nonManifoldEdgeCount,
    adjTotal: adj.adjacency.reduce((s, l) => s + (l ? l.length : 0), 0),
  });
  wf32('excl_centroids.f32', adj.centroids);
  wf32('excl_facenormals.f32', adj.faceNormals);
  wf32('excl_boundradii.f32', adj.boundRadii);
  // flattened adjacency in per-tri order: [count, (neighbor, angle*1000|0)...]
  const flat = [];
  for (let t = 0; t < triCount; t++) {
    const l = adj.adjacency[t] || [];
    flat.push(l.length);
    for (const { neighbor } of l) flat.push(neighbor);
  }
  wi32('excl_adjflat.i32', flat);
  const fill = bucketFill(40, adj.adjacency, 30);
  wi32('excl_fill.i32', [...fill]);
  const excluded = new Set([0, 1, 2, 3, 4, 5, 6, 7, 8, 9]);
  wf32('excl_weights.f32', buildFaceWeights(sphereGeo, excluded, false));
  wf32('excl_weights_inv.f32', buildFaceWeights(sphereGeo, excluded, true));
  var sphereFaceWeights = buildFaceWeights(sphereGeo, excluded, false); // for pipeline
}

// ── Mapping: computeUV samples, all 7 modes ──────────────────────────────────
{
  // Deterministic sample points on/near the sphere with varied normals.
  const samples = [];
  for (let i = 0; i < 200; i++) {
    const px = (rnd() - 0.5) * 30, py = (rnd() - 0.5) * 30, pz = (rnd() - 0.5) * 30;
    let nx = rnd() - 0.5, ny = rnd() - 0.5, nz = rnd() - 0.5;
    const l = Math.sqrt(nx * nx + ny * ny + nz * nz) || 1;
    samples.push([px, py, pz, nx / l, ny / l, nz / l]);
  }
  wf64('map_samples.f64', samples.flat());

  const variants = [
    { name: 'default', s: { ...SETTINGS } },
    { name: 'rot30',   s: { ...SETTINGS, rotation: 30, offsetU: 0.25, offsetV: -0.1 } },
    { name: 'aspect',  s: { ...SETTINGS, textureAspectU: 1, textureAspectV: 512 / 279, mappingBlend: 0.5 } },
  ];
  for (const { name, s } of variants) {
    for (let mode = 0; mode <= 6; mode++) {
      const out = [];
      for (const [px, py, pz, nx, ny, nz] of samples) {
        const r = computeUV({ x: px, y: py, z: pz }, { x: nx, y: ny, z: nz }, mode, s, sphereBounds);
        if (r.triplanar) {
          out.push(r.samples.length);
          for (const smp of r.samples) out.push(smp.u, smp.v, smp.w);
        } else {
          out.push(1, r.u, r.v, 1);
        }
      }
      wf64(`map_${name}_m${mode}.f64`, out);
    }
  }
}

// ── Texture analysis + smart resolution ──────────────────────────────────────
{
  const ta = analyzeTexture(tex);
  wjson('texan.json', ta);
  const sr = computeSmartResolution({
    geometry: sphereGeo, bounds: sphereBounds, settings: SETTINGS,
    texture: { imageData: tex, width: tex.width, height: tex.height },
  });
  wjson('smartres.json', sr);
  wjson('smartres_extra.json', {
    estCube3: estimateSubdivisionTriCount(cubeGeo, 3),
    estSphere1: estimateSubdivisionTriCount(sphereGeo, 1),
    recMaxTri: computeRecommendedMaxTri({
      pixelsPerEdge: ta.pixelsPerEdge, pixMm: 0.123, surfaceArea: 2827.43, amplitude: 0.5,
    }),
  });
}

// ── Subdivision ──────────────────────────────────────────────────────────────
const subCube = await subdivide(geoFrom(cubePos.slice()), 3.0, () => {});
dumpGeo('sub_cube', subCube.geometry);
wi32('sub_cube_parent.i32', subCube.faceParentId);
wjson('sub_cube_meta.json', {
  safetyCapHit: subCube.safetyCapHit,
  triCount: subCube.geometry.attributes.position.count / 3,
});

const subSphere = await subdivide(geoFrom(spherePos.slice()), 1.2, () => {}, sphereFaceWeights);
dumpGeo('sub_sphere', subSphere.geometry);
wi32('sub_sphere_parent.i32', subSphere.faceParentId);
wjson('sub_sphere_meta.json', {
  safetyCapHit: subSphere.safetyCapHit,
  triCount: subSphere.geometry.attributes.position.count / 3,
});

// fast (preview) variant
const subFast = await subdivide(geoFrom(spherePos.slice()), 1.5, () => {}, null, { fast: true });
dumpGeo('sub_fast', subFast.geometry);
wjson('sub_fast_meta.json', { triCount: subFast.geometry.attributes.position.count / 3 });

// ── Regularize (on the sphere subdivision) ───────────────────────────────────
const reg = regularizeMesh(subSphere.geometry, subSphere.faceParentId, 1.2, REG_OPTS);
dumpGeo('reg_sphere', reg.geometry);
wi32('reg_sphere_parent.i32', reg.faceParentId);
wjson('reg_sphere_meta.json', {
  collapseCount: reg.collapseCount,
  rejectStats: reg.rejectStats ?? null,
  triCount: reg.geometry.attributes.position.count / 3,
});

// ── Displacement ─────────────────────────────────────────────────────────────
// Trig-free: triplanar, rotation 0 → expected bit-identical.
const dispTri = applyDisplacement(
  subSphere.geometry, tex, tex.width, tex.height, SETTINGS, sphereBounds, () => {});
dumpGeo('disp_tri', dispTri);

// Trig path: cylindrical + rotation → ≤1e-5 tolerance on the C++ side.
const cylSettings = { ...SETTINGS, mappingMode: 3, rotation: 30, boundaryFalloff: 1.5 };
const dispCyl = applyDisplacement(
  subSphere.geometry, tex, tex.width, tex.height, cylSettings, sphereBounds, () => {});
dumpGeo('disp_cyl', dispCyl);

// ── Decimation ───────────────────────────────────────────────────────────────
const dispTriCount = dispTri.attributes.position.count / 3;
const decTarget = Math.floor(dispTriCount / 2);
const dec = await decimate(dispTri, decTarget, () => {}, true, 0.005);
dumpGeo('dec_sphere', dec);
wjson('dec_meta.json', {
  from: dispTriCount, target: decTarget,
  triCount: dec.attributes.position.count / 3,
});

// ── Mesh repair ──────────────────────────────────────────────────────────────
{
  const before = countEdgeDefects(dec);
  const beforeSlivers = countAreaSlivers(dec);
  const repaired = resolveTJunctions(dec);
  const after = countEdgeDefects(repaired);
  dumpGeo('repair_sphere', repaired);
  wjson('repair_meta.json', {
    before, beforeSlivers, after, afterSlivers: countAreaSlivers(repaired),
    triCount: repaired.attributes.position.count / 3,
  });
}

// ── snapBottomToFlat (standalone) ────────────────────────────────────────────
{
  const g = geoFrom(new Float32Array(dispTri.attributes.position.array));
  g.setAttribute('normal', new THREE.BufferAttribute(new Float32Array(dispTri.attributes.normal.array), 3));
  const dirty = snapBottomToFlat(g, sphereBounds.min.z, 0.35);
  dumpGeo('snapbottom', g);
  wjson('snapbottom_meta.json', { dirtyTris: dirty });
}

// ── Mesh validation ──────────────────────────────────────────────────────────
{
  // Defective fixture: cube + duplicate of tri 0 (reversed winding) + one
  // piercing triangle + one floating (separate shell) triangle.
  const base = Array.from(cubePos);
  base.push(cubePos[0], cubePos[1], cubePos[2],   // duplicate of tri 0, flipped
            cubePos[6], cubePos[7], cubePos[8],
            cubePos[3], cubePos[4], cubePos[5]);
  base.push(-3, -3, 5, 4, 3, 15, 3, -4, 15);      // pierces the top face
  base.push(30, 30, 30, 34, 30, 30, 32, 34, 33);  // floating shell
  const defPos = new Float32Array(base);
  wf32('fx_defective.f32', defPos);
  const defGeo = geoFrom(defPos);
  const adj = buildAdjacency(defGeo);
  const triCount = defPos.length / 9;
  wjson('valid_fast.json', runFastDiagnostics(adj, triCount));
  const edges = getEdgePositions(defGeo);
  wf32('valid_open.f32', edges.open);
  wf32('valid_nm.f32', edges.nonManifold);
  wu32('valid_shells.u32', getShellAssignments(adj.adjacency, triCount));
  const exp = await runExpensiveDiagnostics(defGeo, { get: () => 0 });
  wjson('valid_expensive.json', {
    intersectingPairs: exp.intersectingPairs,
    overlappingPairs: exp.overlappingPairs,
    intersectFaces: [...exp.intersectFaces],
    overlapFaces: [...exp.overlapFaces],
  });
}

// ── Box blur (main.js _boxBlurH/_boxBlurV verbatim — not exported) ───────────
{
  function _boxBlurH(src, dst, w, h, r) {
    const iarr = 1 / (2 * r + 1);
    for (let y = 0; y < h; y++) {
      const row = y * w;
      for (let ch = 0; ch < 4; ch++) {
        let val = 0;
        for (let x = -r; x <= r; x++) val += src[(row + Math.max(0, Math.min(x, w - 1))) * 4 + ch];
        for (let x = 0; x < w; x++) {
          val += src[(row + Math.min(x + r, w - 1)) * 4 + ch]
               - src[(row + Math.max(x - r - 1, 0)) * 4 + ch];
          dst[(row + x) * 4 + ch] = Math.round(val * iarr);
        }
      }
    }
  }
  function _boxBlurV(src, dst, w, h, r) {
    const iarr = 1 / (2 * r + 1);
    for (let x = 0; x < w; x++) {
      for (let ch = 0; ch < 4; ch++) {
        let val = 0;
        for (let y = -r; y <= r; y++) val += src[(Math.max(0, Math.min(y, h - 1)) * w + x) * 4 + ch];
        for (let y = 0; y < h; y++) {
          val += src[(Math.min(y + r, h - 1) * w + x) * 4 + ch]
               - src[(Math.max(y - r - 1, 0) * w + x) * 4 + ch];
          dst[(y * w + x) * 4 + ch] = Math.round(val * iarr);
        }
      }
    }
  }
  const sigma = 2;
  const r = Math.max(1, Math.round((Math.sqrt(4 * sigma * sigma + 1) - 1) / 2));
  const a = new Uint8ClampedArray(tex.data);
  const b = new Uint8ClampedArray(a.length);
  for (let pass = 0; pass < 3; pass++) {
    _boxBlurH(a, b, tex.width, tex.height, r);
    _boxBlurV(b, a, tex.width, tex.height, r);
  }
  wu8('blur_s2.rgba', a);
}

// ── Full export pipeline ─────────────────────────────────────────────────────
{
  // Force decimation with a low triangle target.
  const exSettings = { ...SETTINGS, maxTriangles: 5000 };
  const res = await runExportPipeline({
    positions: new Float32Array(spherePos),
    faceWeights: sphereFaceWeights,
    imageData: tex, imgWidth: tex.width, imgHeight: tex.height,
    settings: exSettings,
    bounds: {
      min: { x: sphereBounds.min.x, y: sphereBounds.min.y, z: sphereBounds.min.z },
      max: { x: sphereBounds.max.x, y: sphereBounds.max.y, z: sphereBounds.max.z },
      size: { x: sphereBounds.size.x, y: sphereBounds.size.y, z: sphereBounds.size.z },
      center: { x: sphereBounds.center.x, y: sphereBounds.center.y, z: sphereBounds.center.z },
    },
    regularizeOpts: REG_OPTS,
    mode: 'export',
  });
  wf32('pipe_export_pos.f32', res.positions);
  if (res.normals) wf32('pipe_export_nrm.f32', res.normals);
  wjson('pipe_export_meta.json', {
    safetyCapHit: res.safetyCapHit, runDecimation: res.runDecimation,
    needsDecimation: res.needsDecimation, repairStats: res.repairStats,
    triCount: res.positions.length / 9,
  });

  const resBake = await runExportPipeline({
    positions: new Float32Array(spherePos),
    faceWeights: sphereFaceWeights,
    imageData: tex, imgWidth: tex.width, imgHeight: tex.height,
    settings: exSettings,
    bounds: {
      min: { x: sphereBounds.min.x, y: sphereBounds.min.y, z: sphereBounds.min.z },
      max: { x: sphereBounds.max.x, y: sphereBounds.max.y, z: sphereBounds.max.z },
      size: { x: sphereBounds.size.x, y: sphereBounds.size.y, z: sphereBounds.size.z },
      center: { x: sphereBounds.center.x, y: sphereBounds.center.y, z: sphereBounds.center.z },
    },
    regularizeOpts: REG_OPTS,
    mode: 'bake',
  });
  wf32('pipe_bake_pos.f32', resBake.positions);
  wi32('pipe_bake_parent.i32', resBake.faceParentId);
  wjson('pipe_bake_meta.json', {
    safetyCapHit: resBake.safetyCapHit, runDecimation: resBake.runDecimation,
    needsDecimation: resBake.needsDecimation,
    triCount: resBake.positions.length / 9,
  });
}

console.log('pipeline goldens written to', outDir);
