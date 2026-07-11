// Golden comparisons: C++ pipeline modules vs reference-JS dumps produced by
// tools/golden/dump_pipeline.mjs. Topology/ids bitwise; trig-free float paths
// bitwise; trig paths (cylindrical/rotation) within 1e-5.
#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "harness.h"

#include "core/displacement.h"
#include "core/decimation.h"
#include "core/exclusion.h"
#include "core/export_pipeline.h"
#include "core/geometry.h"
#include "core/image.h"
#include "core/mapping.h"
#include "core/mesh_repair.h"
#include "core/mesh_validation.h"
#include "core/regularize.h"
#include "core/smart_resolution.h"
#include "core/subdivision.h"
#include "core/texture_analysis.h"

namespace {

const std::string kOut = "tools/golden/out/";
constexpr double kPi = 3.14159265358979323846;

core::Geometry loadGeo(const std::string& posFile) {
  core::Geometry g;
  g.positions = readArray<float>(kOut + posFile);
  return g;
}

core::Bounds loadBounds(const std::string& file) {
  auto d = readArray<double>(kOut + file);
  core::Bounds b;
  if (d.size() == 12) {
    b.min = {d[0], d[1], d[2]};
    b.max = {d[3], d[4], d[5]};
    b.size = {d[6], d[7], d[8]};
    b.center = {d[9], d[10], d[11]};
  }
  return b;
}

core::ImageDataRGBA loadTex() {
  core::ImageDataRGBA tex;
  tex.data = readArray<uint8_t>(kOut + "fx_tex.rgba");
  std::string meta = readText(kOut + "fx_tex.json");
  tex.width = (int)jsonNum(meta, "width", 64);
  tex.height = (int)jsonNum(meta, "height", 64);
  return tex;
}

bool bitEqualF32(const std::vector<float>& a, const std::vector<float>& b) {
  return a.size() == b.size() &&
         (a.empty() ||
          std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
}

// Max |a-b| over two float arrays; HUGE_VAL on size mismatch.
double maxDiffF32(const std::vector<float>& a, const std::vector<float>& b) {
  if (a.size() != b.size()) return HUGE_VAL;
  double m = 0;
  for (size_t i = 0; i < a.size(); i++)
    m = std::max(m, std::abs((double)a[i] - b[i]));
  return m;
}

// Settings used by dump_pipeline.mjs (main.js defaults, triplanar, rot 0).
core::DisplacementSettings goldenDispSettings() {
  core::DisplacementSettings s;
  s.mappingMode = core::MODE_TRIPLANAR;
  s.scaleU = 0.5;
  s.scaleV = 0.5;
  s.amplitude = 0.5;
  s.offsetU = 0;
  s.offsetV = 0;
  s.rotation = 0;
  s.bottomAngleLimit = 5;
  s.topAngleLimit = 0;
  s.mappingBlend = 1;
  s.seamBandWidth = 0.5;
  s.capAngle = 20;
  s.blendNormalSmoothing = 32;
  s.boundaryFalloff = 0;
  s.symmetricDisplacement = false;
  s.noDownwardZ = false;
  return s;
}

core::RegularizeOpts goldenRegOpts() {
  core::RegularizeOpts o;
  o.aspectThreshold = 5;
  o.slack = 3.0;
  o.aggressiveSlack = 8.0;
  o.extremeSliverAspect = 8;
  o.maxNormalDeltaCos = std::cos(15 * kPi / 180);
  o.aggressiveNormalDeltaCos = std::cos(25 * kPi / 180);
  return o;
}

// ── Exclusion ────────────────────────────────────────────────────────────────
void testExclusionGolden() {
  std::printf("[exclusion golden]\n");
  core::Geometry sphere = loadGeo("fx_sphere.f32");
  if (sphere.positions.empty()) {
    std::printf("  SKIP  run `node tools/golden/dump_pipeline.mjs` first\n");
    return;
  }
  std::string meta = readText(kOut + "excl_adj.json");
  core::AdjacencyData adj = core::buildAdjacency(sphere);
  check(adj.openEdgeCount == (int64_t)jsonNum(meta, "openEdgeCount", -1),
        "openEdgeCount matches");
  check(adj.nonManifoldEdgeCount ==
            (int64_t)jsonNum(meta, "nonManifoldEdgeCount", -1),
        "nonManifoldEdgeCount matches");
  int64_t adjTotal = 0;
  for (auto& l : adj.adjacency) adjTotal += (int64_t)l.size();
  check(adjTotal == (int64_t)jsonNum(meta, "adjTotal", -1),
        "adjacency entry total matches");

  check(bitEqualF32(adj.centroids, readArray<float>(kOut + "excl_centroids.f32")),
        "centroids bit-identical");
  check(bitEqualF32(adj.faceNormals,
                    readArray<float>(kOut + "excl_facenormals.f32")),
        "faceNormals bit-identical");
  check(bitEqualF32(adj.boundRadii,
                    readArray<float>(kOut + "excl_boundradii.f32")),
        "boundRadii bit-identical");

  auto goldenFlat = readArray<int32_t>(kOut + "excl_adjflat.i32");
  std::vector<int32_t> flat;
  for (auto& l : adj.adjacency) {
    flat.push_back((int32_t)l.size());
    for (auto& e : l) flat.push_back(e.neighbor);
  }
  check(flat == goldenFlat, "adjacency neighbor lists + order bit-identical");

  auto goldenFill = readArray<int32_t>(kOut + "excl_fill.i32");
  std::vector<int32_t> fill = core::bucketFill(40, adj, 30);
  check(fill == goldenFill, "bucketFill visit order bit-identical (n=" +
                                std::to_string(fill.size()) + ")");

  std::vector<uint8_t> mask(sphere.triangleCount(), 0);
  for (int i = 0; i < 10; i++) mask[i] = 1;
  check(bitEqualF32(core::buildFaceWeights(sphere, mask, false),
                    readArray<float>(kOut + "excl_weights.f32")),
        "faceWeights bit-identical");
  check(bitEqualF32(core::buildFaceWeights(sphere, mask, true),
                    readArray<float>(kOut + "excl_weights_inv.f32")),
        "inverted faceWeights bit-identical");
}

// ── Mapping ──────────────────────────────────────────────────────────────────
void testMappingGolden() {
  std::printf("[mapping golden]\n");
  auto samples = readArray<double>(kOut + "map_samples.f64");
  if (samples.empty()) {
    std::printf("  SKIP  run `node tools/golden/dump_pipeline.mjs` first\n");
    return;
  }
  core::Bounds bounds = loadBounds("fx_sphere_bounds.f64");

  struct Variant {
    const char* name;
    core::MappingSettings s;
  };
  core::MappingSettings def;
  def.scaleU = 0.5;
  def.scaleV = 0.5;
  def.mappingBlend = 1;
  def.seamBandWidth = 0.5;
  def.capAngle = 20;
  core::MappingSettings rot = def;
  rot.rotation = 30;
  rot.offsetU = 0.25;
  rot.offsetV = -0.1;
  core::MappingSettings asp = def;
  asp.textureAspectU = 1;
  asp.textureAspectV = 512.0 / 279.0;
  asp.mappingBlend = 0.5;
  const Variant variants[] = {{"default", def}, {"rot30", rot}, {"aspect", asp}};

  const size_t nSamples = samples.size() / 6;
  for (const Variant& variant : variants) {
    for (int mode = 0; mode <= 6; mode++) {
      auto golden = readArray<double>(kOut + "map_" + variant.name + "_m" +
                                      std::to_string(mode) + ".f64");
      std::vector<double> out;
      for (size_t i = 0; i < nSamples; i++) {
        const double* s = &samples[i * 6];
        core::UVResult r = core::computeUV({s[0], s[1], s[2]},
                                           {s[3], s[4], s[5]}, mode, variant.s,
                                           bounds);
        if (r.triplanar) {
          out.push_back(r.count);
          for (int k = 0; k < r.count; k++) {
            out.push_back(r.samples[k].u);
            out.push_back(r.samples[k].v);
            out.push_back(r.samples[k].w);
          }
        } else {
          out.push_back(1);
          out.push_back(r.samples[0].u);
          out.push_back(r.samples[0].v);
          out.push_back(1);
        }
      }
      bool sizeOk = out.size() == golden.size();
      double maxErr = sizeOk ? 0 : HUGE_VAL;
      if (sizeOk) {
        for (size_t i = 0; i < out.size(); i++) {
          double d = std::abs(out[i] - golden[i]);
          // UVs live in fract() space: a 1-ulp trig difference at the wrap
          // shows up as ±1 — treat wrapped values as equal.
          d = std::min({d, std::abs(d - 1.0)});
          maxErr = std::max(maxErr, d);
        }
      }
      check(maxErr <= 1e-9, std::string("mode ") + std::to_string(mode) + " " +
                                variant.name + " maxErr=" +
                                std::to_string(maxErr));
    }
  }
}

// ── Texture analysis + smart resolution ──────────────────────────────────────
void testTextureAnalysisGolden() {
  std::printf("[texture analysis golden]\n");
  core::ImageDataRGBA tex = loadTex();
  if (tex.data.empty()) {
    std::printf("  SKIP  run `node tools/golden/dump_pipeline.mjs` first\n");
    return;
  }
  std::string meta = readText(kOut + "texan.json");
  core::TextureAnalysis ta = core::analyzeTexture(tex);
  check(std::abs(ta.meanGrad - jsonNum(meta, "meanGrad")) < 1e-12,
        "meanGrad matches");
  check(std::abs(ta.sharpFrac - jsonNum(meta, "sharpFrac")) < 1e-12,
        "sharpFrac matches");
  check(ta.pixelsPerEdge == jsonNum(meta, "pixelsPerEdge"),
        "pixelsPerEdge matches");
}

void testSmartResolutionGolden() {
  std::printf("[smart resolution golden]\n");
  core::Geometry sphere = loadGeo("fx_sphere.f32");
  core::ImageDataRGBA tex = loadTex();
  if (sphere.positions.empty() || tex.data.empty()) {
    std::printf("  SKIP  run `node tools/golden/dump_pipeline.mjs` first\n");
    return;
  }
  core::Bounds bounds = loadBounds("fx_sphere_bounds.f64");
  std::string meta = readText(kOut + "smartres.json");
  core::SmartResolutionResult sr =
      core::computeSmartResolution(sphere, bounds, goldenDispSettings(), tex);
  check(sr.ok, "smart resolution computed");
  check(sr.edge == jsonNum(meta, "edge", -1),
        "edge matches (" + std::to_string(sr.edge) + ")");
  auto near = [&](double a, double b) {
    return std::abs(a - b) <= 1e-9 * std::max({std::abs(a), std::abs(b), 1.0});
  };
  check(near(sr.diagnostics.detailEdge, jsonNum(meta, "detailEdge")),
        "detailEdge matches");
  check(near(sr.diagnostics.budgetEdge, jsonNum(meta, "budgetEdge")),
        "budgetEdge matches");
  check(sr.diagnostics.estTriangles == jsonNum(meta, "estTriangles"),
        "estTriangles matches");
  check(near(sr.diagnostics.surfaceArea, jsonNum(meta, "surfaceArea")),
        "surfaceArea matches");
  check(sr.diagnostics.recommendedMaxTri ==
            (int64_t)jsonNum(meta, "recommendedMaxTri"),
        "recommendedMaxTri matches");

  std::string extra = readText(kOut + "smartres_extra.json");
  core::Geometry cube = loadGeo("fx_cube.f32");
  check(core::estimateSubdivisionTriCount(cube, 3) ==
            jsonNum(extra, "estCube3"),
        "estimateSubdivisionTriCount(cube,3) matches");
  check(core::estimateSubdivisionTriCount(sphere, 1) ==
            jsonNum(extra, "estSphere1"),
        "estimateSubdivisionTriCount(sphere,1) matches");
  check(core::computeRecommendedMaxTri(
            core::analyzeTexture(tex).pixelsPerEdge, 0.123, 2827.43, 0.5) ==
            (int64_t)jsonNum(extra, "recMaxTri"),
        "computeRecommendedMaxTri matches");
}

// ── Subdivision / regularize / displacement / decimation / repair chain ─────
struct ChainState {
  core::SubdivideResult subSphere;
  core::Geometry dispTri;
  core::Geometry dec;
  bool subOk = false, dispOk = false, decOk = false;
};

void testSubdivisionGolden(ChainState& chain) {
  std::printf("[subdivision golden]\n");
  core::Geometry cube = loadGeo("fx_cube.f32");
  core::Geometry sphere = loadGeo("fx_sphere.f32");
  if (cube.positions.empty() || sphere.positions.empty()) {
    std::printf("  SKIP  run `node tools/golden/dump_pipeline.mjs` first\n");
    return;
  }

  core::SubdivideResult subCube = core::subdivide(cube, 3.0);
  check(bitEqualF32(subCube.geometry.positions,
                    readArray<float>(kOut + "sub_cube_pos.f32")),
        "cube positions bit-identical (" +
            std::to_string(subCube.geometry.triangleCount()) + " tris)");
  check(bitEqualF32(subCube.geometry.normals,
                    readArray<float>(kOut + "sub_cube_nrm.f32")),
        "cube normals bit-identical");
  check(subCube.faceParentId == readArray<int32_t>(kOut + "sub_cube_parent.i32"),
        "cube faceParentId bit-identical");
  std::string cubeMeta = readText(kOut + "sub_cube_meta.json");
  check(subCube.safetyCapHit == jsonBool(cubeMeta, "safetyCapHit"),
        "cube safetyCapHit matches");

  auto weights = readArray<float>(kOut + "excl_weights.f32");
  chain.subSphere = core::subdivide(sphere, 1.2, nullptr, &weights);
  check(bitEqualF32(chain.subSphere.geometry.positions,
                    readArray<float>(kOut + "sub_sphere_pos.f32")),
        "sphere positions bit-identical (" +
            std::to_string(chain.subSphere.geometry.triangleCount()) +
            " tris)");
  check(bitEqualF32(chain.subSphere.geometry.normals,
                    readArray<float>(kOut + "sub_sphere_nrm.f32")),
        "sphere normals bit-identical");
  check(bitEqualF32(chain.subSphere.excludeWeight,
                    readArray<float>(kOut + "sub_sphere_excl.f32")),
        "sphere excludeWeight bit-identical");
  check(chain.subSphere.faceParentId ==
            readArray<int32_t>(kOut + "sub_sphere_parent.i32"),
        "sphere faceParentId bit-identical");
  chain.subOk = true;

  core::SubdivideResult subFast =
      core::subdivide(sphere, 1.5, nullptr, nullptr, /*fast=*/true);
  check(bitEqualF32(subFast.geometry.positions,
                    readArray<float>(kOut + "sub_fast_pos.f32")),
        "fast-mode positions bit-identical");
  check(bitEqualF32(subFast.geometry.normals,
                    readArray<float>(kOut + "sub_fast_nrm.f32")),
        "fast-mode normals bit-identical");
}

void testRegularizeGolden(const ChainState& chain) {
  std::printf("[regularize golden]\n");
  if (!chain.subOk) {
    std::printf("  SKIP  subdivision golden did not run\n");
    return;
  }
  core::RegularizeResult reg = core::regularizeMesh(
      chain.subSphere.geometry, chain.subSphere.faceParentId, 1.2,
      goldenRegOpts(),
      chain.subSphere.excludeWeight.empty() ? nullptr
                                            : &chain.subSphere.excludeWeight);
  std::string meta = readText(kOut + "reg_sphere_meta.json");
  check(reg.collapseCount == (int64_t)jsonNum(meta, "collapseCount", -1),
        "collapseCount matches (" + std::to_string(reg.collapseCount) + ")");
  check(bitEqualF32(reg.geometry.positions,
                    readArray<float>(kOut + "reg_sphere_pos.f32")),
        "positions bit-identical (" +
            std::to_string(reg.geometry.triangleCount()) + " tris)");
  check(bitEqualF32(reg.geometry.normals,
                    readArray<float>(kOut + "reg_sphere_nrm.f32")),
        "normals bit-identical");
  check(bitEqualF32(reg.excludeWeight,
                    readArray<float>(kOut + "reg_sphere_excl.f32")),
        "excludeWeight bit-identical");
  check(reg.faceParentId == readArray<int32_t>(kOut + "reg_sphere_parent.i32"),
        "faceParentId bit-identical");
  check(reg.rejectStats.normalChange ==
            (int64_t)jsonNum(meta, "normalChange", -1),
        "rejectStats.normalChange matches");
}

void testDisplacementGolden(ChainState& chain) {
  std::printf("[displacement golden]\n");
  if (!chain.subOk) {
    std::printf("  SKIP  subdivision golden did not run\n");
    return;
  }
  core::ImageDataRGBA tex = loadTex();
  core::Bounds bounds = loadBounds("fx_sphere_bounds.f64");

  chain.dispTri =
      core::applyDisplacement(chain.subSphere.geometry, tex,
                              goldenDispSettings(), bounds,
                              chain.subSphere.excludeWeight);
  check(bitEqualF32(chain.dispTri.positions,
                    readArray<float>(kOut + "disp_tri_pos.f32")),
        "triplanar positions bit-identical (" +
            std::to_string(chain.dispTri.triangleCount()) + " tris)");
  check(bitEqualF32(chain.dispTri.normals,
                    readArray<float>(kOut + "disp_tri_nrm.f32")),
        "triplanar normals bit-identical");
  chain.dispOk = bitEqualF32(chain.dispTri.positions,
                             readArray<float>(kOut + "disp_tri_pos.f32"));

  core::DisplacementSettings cyl = goldenDispSettings();
  cyl.mappingMode = core::MODE_CYLINDRICAL;
  cyl.rotation = 30;
  cyl.boundaryFalloff = 1.5;
  core::Geometry dispCyl =
      core::applyDisplacement(chain.subSphere.geometry, tex, cyl, bounds,
                              chain.subSphere.excludeWeight);
  auto goldPos = readArray<float>(kOut + "disp_cyl_pos.f32");
  double err = maxDiffF32(dispCyl.positions, goldPos);
  check(err <= 1e-5,
        "cylindrical positions within 1e-5 (maxErr=" + std::to_string(err) +
            ")");
  check(dispCyl.positions.size() == goldPos.size(),
        "cylindrical topology matches");
}

void testDecimationGolden(ChainState& chain) {
  std::printf("[decimation golden]\n");
  if (!chain.dispOk) {
    std::printf("  SKIP  displacement golden did not pass\n");
    return;
  }
  std::string meta = readText(kOut + "dec_meta.json");
  int64_t target = (int64_t)jsonNum(meta, "target", -1);
  chain.dec = core::decimate(chain.dispTri, target, nullptr, true, 0.005);
  check((int64_t)chain.dec.triangleCount() == (int64_t)jsonNum(meta, "triCount", -1),
        "decimated tri count matches (" +
            std::to_string(chain.dec.triangleCount()) + ")");
  check(bitEqualF32(chain.dec.positions,
                    readArray<float>(kOut + "dec_sphere_pos.f32")),
        "positions bit-identical");
  check(bitEqualF32(chain.dec.normals,
                    readArray<float>(kOut + "dec_sphere_nrm.f32")),
        "normals bit-identical");
  chain.decOk = bitEqualF32(chain.dec.positions,
                            readArray<float>(kOut + "dec_sphere_pos.f32"));
}

void testRepairGolden(const ChainState& chain) {
  std::printf("[mesh repair golden]\n");
  if (!chain.decOk) {
    std::printf("  SKIP  decimation golden did not pass\n");
    return;
  }
  std::string meta = readText(kOut + "repair_meta.json");
  // First "open"/"nonManifold" occurrences in repair_meta.json belong to the
  // "before" object.
  core::EdgeDefects before = core::countEdgeDefects(chain.dec);
  check(before.open == (int64_t)jsonNum(meta, "open", -1) &&
            before.nonManifold == (int64_t)jsonNum(meta, "nonManifold", -1),
        "pre-repair edge defects match (open=" + std::to_string(before.open) +
            ", nm=" + std::to_string(before.nonManifold) + ")");
  check(core::countAreaSlivers(chain.dec) ==
            (int64_t)jsonNum(meta, "beforeSlivers", -1),
        "beforeSlivers matches");
  core::Geometry repaired = core::resolveTJunctions(chain.dec);
  check(bitEqualF32(repaired.positions,
                    readArray<float>(kOut + "repair_sphere_pos.f32")),
        "repaired positions bit-identical (" +
            std::to_string(repaired.triangleCount()) + " tris)");
  check(bitEqualF32(repaired.normals,
                    readArray<float>(kOut + "repair_sphere_nrm.f32")),
        "repaired normals bit-identical");
}

void testSnapBottomGolden(const ChainState& chain) {
  std::printf("[snapBottomToFlat golden]\n");
  if (!chain.dispOk) {
    std::printf("  SKIP  displacement golden did not pass\n");
    return;
  }
  core::Bounds bounds = loadBounds("fx_sphere_bounds.f64");
  core::Geometry g;
  g.positions = chain.dispTri.positions;
  g.normals = chain.dispTri.normals;
  int64_t dirty = core::snapBottomToFlat(g, bounds.min.z, 0.35);
  std::string meta = readText(kOut + "snapbottom_meta.json");
  check(dirty == (int64_t)jsonNum(meta, "dirtyTris", -1),
        "dirtyTris matches (" + std::to_string(dirty) + ")");
  check(bitEqualF32(g.positions, readArray<float>(kOut + "snapbottom_pos.f32")),
        "positions bit-identical");
  check(bitEqualF32(g.normals, readArray<float>(kOut + "snapbottom_nrm.f32")),
        "normals bit-identical");
}

// ── Mesh validation ──────────────────────────────────────────────────────────
void testValidationGolden() {
  std::printf("[mesh validation golden]\n");
  core::Geometry def = loadGeo("fx_defective.f32");
  if (def.positions.empty()) {
    std::printf("  SKIP  run `node tools/golden/dump_pipeline.mjs` first\n");
    return;
  }
  int64_t triCount = (int64_t)def.triangleCount();
  core::AdjacencyData adj = core::buildAdjacency(def);
  core::FastDiagnostics fast = core::runFastDiagnostics(adj, triCount);
  std::string fmeta = readText(kOut + "valid_fast.json");
  check(fast.openEdges == (int64_t)jsonNum(fmeta, "openEdges", -1),
        "openEdges matches (" + std::to_string(fast.openEdges) + ")");
  check(fast.nonManifoldEdges ==
            (int64_t)jsonNum(fmeta, "nonManifoldEdges", -1),
        "nonManifoldEdges matches");
  check(fast.shellCount == (int64_t)jsonNum(fmeta, "shellCount", -1),
        "shellCount matches (" + std::to_string(fast.shellCount) + ")");

  core::EdgeHighlightPositions edges = core::getEdgePositions(def);
  check(bitEqualF32(edges.open, readArray<float>(kOut + "valid_open.f32")),
        "open edge segments bit-identical");
  check(bitEqualF32(edges.nonManifold,
                    readArray<float>(kOut + "valid_nm.f32")),
        "non-manifold edge segments bit-identical");

  std::vector<uint32_t> shells = core::getShellAssignments(adj, triCount);
  check(shells == readArray<uint32_t>(kOut + "valid_shells.u32"),
        "shell assignments bit-identical");

  core::ExpensiveDiagnostics exp = core::runExpensiveDiagnostics(def);
  std::string emeta = readText(kOut + "valid_expensive.json");
  check(exp.intersectingPairs ==
            (int64_t)jsonNum(emeta, "intersectingPairs", -1),
        "intersectingPairs matches (" +
            std::to_string(exp.intersectingPairs) + ")");
  check(exp.overlappingPairs == (int64_t)jsonNum(emeta, "overlappingPairs", -1),
        "overlappingPairs matches (" + std::to_string(exp.overlappingPairs) +
            ")");
}

// ── Box blur ─────────────────────────────────────────────────────────────────
void testBlurGolden() {
  std::printf("[box blur golden]\n");
  core::ImageDataRGBA tex = loadTex();
  auto golden = readArray<uint8_t>(kOut + "blur_s2.rgba");
  if (tex.data.empty() || golden.empty()) {
    std::printf("  SKIP  run `node tools/golden/dump_pipeline.mjs` first\n");
    return;
  }
  core::blurImageRGBA(tex, 2);
  check(tex.data == golden, "3-pass box blur (sigma=2) bit-identical");
}

// ── Full export pipeline ─────────────────────────────────────────────────────
void testPipelineGolden() {
  std::printf("[export pipeline golden]\n");
  core::Geometry sphere = loadGeo("fx_sphere.f32");
  if (sphere.positions.empty()) {
    std::printf("  SKIP  run `node tools/golden/dump_pipeline.mjs` first\n");
    return;
  }
  core::ImageDataRGBA tex = loadTex();
  auto weights = readArray<float>(kOut + "excl_weights.f32");

  core::ExportPipelineInput in;
  in.geometry = &sphere;
  in.faceWeights = &weights;
  in.image = &tex;
  in.bounds = loadBounds("fx_sphere_bounds.f64");
  in.regularizeOpts = goldenRegOpts();
  in.settings.displacement = goldenDispSettings();
  in.settings.refineLength = 1.0;
  in.settings.maxTriangles = 5000;
  in.settings.smoothBottom = true;
  in.settings.harvestFlatFaces = true;
  in.settings.harvestTol = 0.005;
  in.settings.regularizeEnabled = true;
  in.settings.regularizeSecondPassMul = 1.1;
  in.subdivisionCap = core::kSubdivSafetyCapLow; // Node golden used 16M

  auto res = core::runExportPipeline(in);
  std::string meta = readText(kOut + "pipe_export_meta.json");
  check(res.has_value(), "export pipeline completed");
  if (!res) return;
  check((int64_t)res->geometry.triangleCount() ==
            (int64_t)jsonNum(meta, "triCount", -1),
        "final tri count matches (" +
            std::to_string(res->geometry.triangleCount()) + ")");
  check(res->runDecimation == jsonBool(meta, "runDecimation") &&
            res->needsDecimation == jsonBool(meta, "needsDecimation") &&
            res->safetyCapHit == jsonBool(meta, "safetyCapHit"),
        "flags match");
  check(bitEqualF32(res->geometry.positions,
                    readArray<float>(kOut + "pipe_export_pos.f32")),
        "positions bit-identical");
  check(bitEqualF32(res->geometry.normals,
                    readArray<float>(kOut + "pipe_export_nrm.f32")),
        "normals bit-identical");
  check(res->repairStats.has_value() &&
            res->repairStats->open == (int64_t)jsonNum(meta, "open", -1) &&
            res->repairStats->nonManifold ==
                (int64_t)jsonNum(meta, "nonManifold", -1) &&
            res->repairStats->slivers ==
                (int64_t)jsonNum(meta, "slivers", -1),
        "repairStats match");

  in.mode = core::PipelineMode::Bake;
  auto bake = core::runExportPipeline(in);
  std::string bmeta = readText(kOut + "pipe_bake_meta.json");
  check(bake.has_value(), "bake pipeline completed");
  if (!bake) return;
  check((int64_t)bake->geometry.triangleCount() ==
            (int64_t)jsonNum(bmeta, "triCount", -1),
        "bake tri count matches (" +
            std::to_string(bake->geometry.triangleCount()) + ")");
  check(bitEqualF32(bake->geometry.positions,
                    readArray<float>(kOut + "pipe_bake_pos.f32")),
        "bake positions bit-identical");
  check(bake->faceParentId == readArray<int32_t>(kOut + "pipe_bake_parent.i32"),
        "bake faceParentId bit-identical");
}

} // namespace

void runPipelineGoldenTests(const std::string& filter) {
  auto want = [&](const char* name) {
    return filter.empty() || filter == name;
  };
  ChainState chain;
  if (want("exclusion")) testExclusionGolden();
  if (want("mapping")) testMappingGolden();
  if (want("texan")) testTextureAnalysisGolden();
  if (want("smartres")) testSmartResolutionGolden();
  // The chain tests feed each other; "chain" runs them all.
  bool chainAll = filter.empty() || filter == "chain";
  if (chainAll || want("subdivision")) testSubdivisionGolden(chain);
  if (chainAll || want("regularize")) testRegularizeGolden(chain);
  if (chainAll || want("displacement")) testDisplacementGolden(chain);
  if (chainAll || want("decimation")) testDecimationGolden(chain);
  if (chainAll || want("repair")) testRepairGolden(chain);
  if (chainAll || want("snapbottom")) testSnapBottomGolden(chain);
  if (want("validation")) testValidationGolden();
  if (want("blur")) testBlurGolden();
  if (want("pipeline")) testPipelineGolden();
}
