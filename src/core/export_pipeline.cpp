#include "core/export_pipeline.h"

#include <algorithm>
#include <cmath>

#include "core/mesh_index.h"
#include "core/mesh_repair.h"

namespace core {

namespace {
constexpr double PI = 3.14159265358979323846;
} // namespace

void clampBelowBottom(Geometry& geometry, double bottomZ) {
  std::vector<float>& pa = geometry.positions;
  const bool hadNormals = geometry.normals.size() == pa.size();
  if (!hadNormals) geometry.normals.assign(pa.size(), 0.0f);
  std::vector<float>& na = geometry.normals;
  const float bz = (float)bottomZ;

  for (size_t i = 0; i + 8 < pa.size(); i += 9) {
    bool dirty = false;
    if (pa[i + 2] < bottomZ) { pa[i + 2] = bz; dirty = true; }
    if (pa[i + 5] < bottomZ) { pa[i + 5] = bz; dirty = true; }
    if (pa[i + 8] < bottomZ) { pa[i + 8] = bz; dirty = true; }

    if (dirty) {
      const double ux = (double)pa[i + 3] - pa[i];
      const double uy = (double)pa[i + 4] - pa[i + 1];
      const double uz = (double)pa[i + 5] - pa[i + 2];
      const double vx = (double)pa[i + 6] - pa[i];
      const double vy = (double)pa[i + 7] - pa[i + 1];
      const double vz = (double)pa[i + 8] - pa[i + 2];
      const double nx = uy * vz - uz * vy;
      const double ny = uz * vx - ux * vz;
      const double nz = ux * vy - uy * vx;
      double len = std::sqrt(nx * nx + ny * ny + nz * nz);
      if (len == 0) len = 1;
      na[i] = na[i + 3] = na[i + 6] = (float)(nx / len);
      na[i + 1] = na[i + 4] = na[i + 7] = (float)(ny / len);
      na[i + 2] = na[i + 5] = na[i + 8] = (float)(nz / len);
    }
  }
}

int64_t snapBottomToFlat(Geometry& geometry, double bottomZ, double tol) {
  std::vector<float>& pa = geometry.positions;
  const bool hadNormals = geometry.normals.size() == pa.size();
  std::vector<float> localNa;
  if (!hadNormals) localNa.assign(pa.size(), 0.0f);
  float* na = hadNormals ? geometry.normals.data() : localNa.data();

  const int64_t vertCount = (int64_t)pa.size() / 3;
  const int64_t triCount = vertCount / 3;

  // Weld positions (1e6 — the decimation grid; copies of one position are
  // bit-identical at this point) and build per-position incident corner lists.
  QuantizedPointMap weld(1e6, (size_t)std::min(vertCount, (int64_t)1 << 22));
  std::vector<uint32_t> vid(vertCount);
  int32_t nUnique = 0;
  for (int64_t i = 0; i < vertCount; i++) {
    int32_t id =
        weld.getOrSet(pa[i * 3], pa[i * 3 + 1], pa[i * 3 + 2], nUnique);
    if (weld.inserted) nUnique++;
    vid[i] = (uint32_t)id;
  }
  std::vector<uint32_t> start(nUnique + 1, 0);
  for (int64_t i = 0; i < vertCount; i++) start[vid[i] + 1]++;
  for (int32_t id = 0; id < nUnique; id++) start[id + 1] += start[id];
  std::vector<uint32_t> inc(vertCount);
  std::vector<uint32_t> cursor(nUnique, 0);
  for (int64_t i = 0; i < vertCount; i++)
    inc[start[vid[i]] + cursor[vid[i]]++] = (uint32_t)i;

  const double FOLD_COS = std::cos(75 * PI / 180);
  std::vector<uint8_t> dirtyTri(triCount, 0);
  double _zs[3];
  const float bzf = (float)bottomZ;

  for (int32_t id = 0; id < nUnique; id++) {
    const uint32_t first = inc[start[id]];
    const double z = pa[(size_t)first * 3 + 2];
    if (z == bottomZ || std::abs(z - bottomZ) > tol) continue;

    // Gate: simulate moving this position to the plane; every incident
    // triangle must keep positive area and not fold (rotation ≤ ~75°).
    bool ok = true;
    for (uint32_t k = start[id]; k < start[id + 1] && ok; k++) {
      const int64_t t = inc[k] / 3;
      const int64_t b = t * 9;
      const int64_t c0 = t * 3;
      for (int v = 0; v < 3; v++)
        _zs[v] = vid[c0 + v] == (uint32_t)id ? bottomZ : pa[b + v * 3 + 2];

      const double oux = (double)pa[b + 3] - pa[b];
      const double ouy = (double)pa[b + 4] - pa[b + 1];
      const double ouz = (double)pa[b + 5] - pa[b + 2];
      const double ovx = (double)pa[b + 6] - pa[b];
      const double ovy = (double)pa[b + 7] - pa[b + 1];
      const double ovz = (double)pa[b + 8] - pa[b + 2];
      const double onx = ouy * ovz - ouz * ovy;
      const double ony = ouz * ovx - oux * ovz;
      const double onz = oux * ovy - ouy * ovx;

      const double nuz = _zs[1] - _zs[0], nvz = _zs[2] - _zs[0];
      const double nnx = ouy * nvz - nuz * ovy;
      const double nny = nuz * ovx - oux * nvz;
      const double nnz = oux * ovy - ouy * ovx;

      const double o2 = onx * onx + ony * ony + onz * onz;
      const double n2 = nnx * nnx + nny * nny + nnz * nnz;
      if (n2 < 1e-20) { ok = false; break; } // would collapse to zero area
      if (o2 < 1e-20) continue; // already degenerate — can't judge rotation
      const double dot = onx * nnx + ony * nny + onz * nnz;
      if (dot < 0 || dot * dot < FOLD_COS * FOLD_COS * o2 * n2)
        ok = false; // would fold
    }
    if (!ok) continue;

    // Apply: snap all copies of this position; mark incident triangles dirty.
    for (uint32_t k = start[id]; k < start[id + 1]; k++) {
      pa[(size_t)inc[k] * 3 + 2] = bzf;
      dirtyTri[inc[k] / 3] = 1;
    }
  }

  // Recompute face normals on touched triangles.
  int64_t dirtyTris = 0;
  for (int64_t t = 0; t < triCount; t++) {
    if (!dirtyTri[t]) continue;
    dirtyTris++;
    const int64_t i = t * 9;
    const double ux = (double)pa[i + 3] - pa[i];
    const double uy = (double)pa[i + 4] - pa[i + 1];
    const double uz = (double)pa[i + 5] - pa[i + 2];
    const double vx = (double)pa[i + 6] - pa[i];
    const double vy = (double)pa[i + 7] - pa[i + 1];
    const double vz = (double)pa[i + 8] - pa[i + 2];
    const double nx = uy * vz - uz * vy;
    const double ny = uz * vx - ux * vz;
    const double nz = ux * vy - uy * vx;
    double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len == 0) len = 1;
    na[i] = na[i + 3] = na[i + 6] = (float)(nx / len);
    na[i + 1] = na[i + 4] = na[i + 7] = (float)(ny / len);
    na[i + 2] = na[i + 5] = na[i + 8] = (float)(nz / len);
  }

  if (dirtyTris > 0 && !hadNormals) geometry.normals = std::move(localNa);
  return dirtyTris;
}

std::optional<ExportPipelineResult> runExportPipeline(
    const ExportPipelineInput& input, const PipelineEvent& onEvent,
    const std::function<bool()>& shouldAbort) {
  const ExportPipelineSettings& settings = input.settings;
  const bool bake = input.mode == PipelineMode::Bake;
  const Bounds& bounds = input.bounds;

  auto emit = [&](const char* stage, double p, const PipelineEventInfo& info) {
    if (onEvent) onEvent(stage, p, info);
  };
  auto aborted = [&]() { return shouldAbort && shouldAbort(); };

  emit("subdivide1", 0, {});
  if (aborted()) return std::nullopt;

  SubdivideResult sub = subdivide(
      *input.geometry, settings.refineLength,
      [&](double p, int64_t triCount, double longestEdge) {
        emit("subdivide1", p, {triCount, longestEdge});
      },
      input.faceWeights, false, input.subdivisionCap);
  if (aborted()) return std::nullopt;

  const bool safetyCapHit = sub.safetyCapHit;
  std::vector<int32_t> faceParentId = std::move(sub.faceParentId);

  // Regularize sub-slivers, then re-subdivide stretched edges. Skipped when
  // the Advanced toggle is off. Export mode passes a zero parent map (it
  // doesn't consume parents); bake mode threads + composes the real one.
  if (settings.regularizeEnabled) {
    emit("regularize", 0, {});
    std::vector<int32_t> zeroParents;
    const std::vector<int32_t>& regParents =
        bake ? faceParentId
             : (zeroParents.assign(sub.geometry.positions.size() / 9, 0),
                zeroParents);
    RegularizeResult reg = regularizeMesh(
        sub.geometry, regParents, settings.refineLength, input.regularizeOpts,
        sub.excludeWeight.empty() ? nullptr : &sub.excludeWeight);
    sub.geometry = Geometry{};
    const std::vector<float>* secondPassWeights =
        reg.excludeWeight.empty() ? nullptr : &reg.excludeWeight;
    SubdivideResult resub = subdivide(
        reg.geometry, settings.refineLength * settings.regularizeSecondPassMul,
        [&](double p, int64_t triCount, double longestEdge) {
          emit("subdivide2", p, {triCount, longestEdge});
        },
        secondPassWeights, false, input.subdivisionCap);
    reg.geometry = Geometry{};
    if (bake) {
      std::vector<int32_t> composed(resub.faceParentId.size());
      for (size_t i = 0; i < resub.faceParentId.size(); i++)
        composed[i] = reg.faceParentId[resub.faceParentId[i]];
      faceParentId = std::move(composed);
    }
    sub = std::move(resub);
  }
  if (aborted()) return std::nullopt;

  const int64_t subTriCount = (int64_t)sub.geometry.positions.size() / 9;
  emit("displace", 0, {subTriCount});
  Geometry displaced = applyDisplacement(
      sub.geometry, *input.image, settings.displacement, bounds,
      sub.excludeWeight,
      [&](double p) { emit("displace", p, {subTriCount}); });
  if (aborted()) return std::nullopt;

  sub.geometry = Geometry{}; // displacement created a separate copy

  const int64_t dispTriCount = (int64_t)displaced.positions.size() / 9;
  const bool needsDecimation = dispTriCount > settings.maxTriangles;
  Geometry finalGeometry = std::move(displaced);

  // Decimation runs only in export mode (bake keeps the parent-face map,
  // which decimate drops): when over the target OR when flat-face harvesting
  // alone is wanted.
  const bool runDecimation =
      !bake && (needsDecimation || settings.harvestFlatFaces);
  if (runDecimation) {
    emit("decimate", 0, {-1, 0, dispTriCount, needsDecimation});
    Geometry decimated = decimate(
        finalGeometry, settings.maxTriangles,
        [&](double p) {
          emit("decimate", p, {-1, 0, dispTriCount, needsDecimation});
        },
        settings.harvestFlatFaces, settings.harvestTol);
    finalGeometry = std::move(decimated);
    if (aborted()) return std::nullopt;
  }

  if (settings.displacement.bottomAngleLimit > 0) {
    clampBelowBottom(finalGeometry, bounds.min.z);
  }
  if (settings.smoothBottom) {
    snapBottomToFlat(finalGeometry, bounds.min.z, 0.1);
  }

  // Resolve T-junctions so the export is watertight & manifold. Only on the
  // decimated (sparse) mesh — welding the dense pre-decimation mesh at the
  // export grid would collapse fine detail into degenerates.
  std::optional<RepairStats> repairStats;
  if (runDecimation) {
    emit("repair", 0, {});
    const int64_t beforeSlivers = countAreaSlivers(finalGeometry);
    finalGeometry = resolveTJunctions(finalGeometry);
    EdgeDefects after = countEdgeDefects(finalGeometry);
    repairStats = RepairStats{beforeSlivers, after.open, after.nonManifold,
                              countAreaSlivers(finalGeometry), after.tris};
    if (aborted()) return std::nullopt;
  }

  ExportPipelineResult result;
  result.geometry = std::move(finalGeometry);
  result.safetyCapHit = safetyCapHit;
  result.runDecimation = runDecimation;
  result.needsDecimation = needsDecimation;
  if (bake) result.faceParentId = std::move(faceParentId);
  result.repairStats = repairStats;
  return result;
}

} // namespace core
