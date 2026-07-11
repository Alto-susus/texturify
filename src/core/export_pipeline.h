// Port of reference/js/exportPipeline.js — the heavy mesh pipeline behind
// Export and Bake. Pure data in/out: no UI, no i18n, no app state.
//
// Sequence:
//   subdivide → [regularize → re-subdivide] → displace
//   → [decimate]                 (export mode only)
//   → bottom clamp → smooth bottom
//   → [resolveTJunctions]        (export mode, when decimation ran)
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

#include "core/decimation.h"
#include "core/displacement.h"
#include "core/geometry.h"
#include "core/regularize.h"
#include "core/subdivision.h"

namespace core {

enum class PipelineMode { Export, Bake };

// The settings snapshot fields the pipeline consumes (main.js `settings`).
struct ExportPipelineSettings {
  DisplacementSettings displacement; // incl. bottomAngleLimit for the clamp
  double refineLength = 1.0;
  int64_t maxTriangles = 750'000;
  bool smoothBottom = true;
  bool harvestFlatFaces = true;
  double harvestTol = kDefaultHarvestTol;
  bool regularizeEnabled = true;
  double regularizeSecondPassMul = 1.1;
};

struct ExportPipelineInput {
  const Geometry* geometry = nullptr;          // non-indexed positions
  const std::vector<float>* faceWeights = nullptr; // per-vertex exclusion, optional
  const ImageDataRGBA* image = nullptr;
  ExportPipelineSettings settings;
  Bounds bounds;
  RegularizeOpts regularizeOpts;
  PipelineMode mode = PipelineMode::Export;
  int64_t subdivisionCap = kSubdivSafetyCapLow; // pass subdivisionSafetyCap()
};

struct RepairStats {
  int64_t beforeSlivers = 0;
  int64_t open = 0;
  int64_t nonManifold = 0;
  int64_t slivers = 0;
  int64_t tris = 0;
};

struct ExportPipelineResult {
  Geometry geometry; // positions + normals
  bool safetyCapHit = false;
  bool runDecimation = false;
  bool needsDecimation = false;
  std::vector<int32_t> faceParentId; // bake mode only, else empty
  std::optional<RepairStats> repairStats; // export mode, when repair ran
};

// Stages: "subdivide1", "regularize", "subdivide2", "displace", "decimate",
// "repair". Unused info fields are -1/false.
struct PipelineEventInfo {
  int64_t triCount = -1;
  double longestEdge = 0;
  int64_t from = -1;
  bool needsDecimation = false;
};
using PipelineEvent =
    std::function<void(const char* stage, double p, const PipelineEventInfo&)>;

// Flat-bottom clamp (bottomAngleLimit > 0): any vertex below the original
// model's bottom layer is snapped back up, with selective face-normal
// recomputation. Ensures the geometry has normals afterwards.
void clampBelowBottom(Geometry& geometry, double bottomZ);

// Smooth Bottom: per-welded-position snap of near-bottom vertices onto the
// bottom plane, rejected when any incident triangle would degenerate or fold
// (normal rotation > ~75°). Returns the number of retouched triangles.
int64_t snapBottomToFlat(Geometry& geometry, double bottomZ, double tol = 0.1);

// nullopt when aborted (shouldAbort polled between stages).
std::optional<ExportPipelineResult> runExportPipeline(
    const ExportPipelineInput& input, const PipelineEvent& onEvent = nullptr,
    const std::function<bool()>& shouldAbort = nullptr);

} // namespace core
