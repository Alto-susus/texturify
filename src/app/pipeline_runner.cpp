#include "app/pipeline_runner.h"

#include <cmath>
#include <cstdio>

#include "app/actions.h"
#include "core/exclusion.h"
#include "core/exporter.h"
#include "core/regularize.h"
#include "core/subdivision.h"

namespace app {

using core::Vec3;

namespace {

// Port of main.js's buildCombinedFaceWeights(): user-painted exclusion +
// top/bottom angle masking combined into per-vertex weights, so subdivision
// skips interior edges of faces that won't be displaced either way.
std::vector<float> buildCombinedFaceWeights(const core::Geometry& geo,
                                            const std::unordered_set<int32_t>& excluded,
                                            bool invert, double bottomAngleLimit,
                                            double topAngleLimit) {
  const size_t triCount = geo.triangleCount();
  std::vector<uint8_t> mask(triCount, 0);
  for (int32_t f : excluded)
    if (f >= 0 && (size_t)f < triCount) mask[f] = 1;
  std::vector<float> weights = core::buildFaceWeights(geo, mask, invert);

  const bool hasAngleMask = bottomAngleLimit > 0 || topAngleLimit > 0;
  if (!hasAngleMask) return weights;

  const auto& pos = geo.positions;
  for (size_t t = 0; t < triCount; t++) {
    if (weights[t * 3] > 0.99f) continue; // already excluded
    const size_t b = t * 9;
    Vec3 a{pos[b], pos[b + 1], pos[b + 2]};
    Vec3 bb{pos[b + 3], pos[b + 4], pos[b + 5]};
    Vec3 c{pos[b + 6], pos[b + 7], pos[b + 8]};
    Vec3 n = (bb - a).cross(c - a);
    const double area = n.length();
    const double faceNzNorm = area > 1e-12 ? n.z / area : 0;
    const double faceAngle = std::acos(std::abs(faceNzNorm)) * (180.0 / render::kPi);
    const bool angleMasked =
        faceNzNorm < 0 ? (bottomAngleLimit > 0 && faceAngle <= bottomAngleLimit)
                       : (topAngleLimit > 0 && faceAngle <= topAngleLimit);
    if (angleMasked) {
      weights[t * 3] = 1.0f;
      weights[t * 3 + 1] = 1.0f;
      weights[t * 3 + 2] = 1.0f;
    }
  }
  return weights;
}

core::ExportPipelineSettings toExportPipelineSettings(const Settings& s) {
  core::ExportPipelineSettings out;
  out.displacement = toDisplacementSettings(s);

  out.refineLength = s.refineLength;
  out.maxTriangles = s.maxTriangles;
  out.smoothBottom = s.smoothBottom;
  out.harvestFlatFaces = s.harvestFlatFaces;
  out.harvestTol = s.harvestTol;
  out.regularizeEnabled = s.regularizeEnabled;
  out.regularizeSecondPassMul = s.regularizeSecondPassMul;
  return out;
}

void writeFile(const std::string& path, const std::vector<uint8_t>& bytes,
              std::string& error) {
  FILE* f = std::fopen(path.c_str(), "wb");
  if (!f) {
    error = "could not open file for writing";
    return;
  }
  const size_t written = std::fwrite(bytes.data(), 1, bytes.size(), f);
  std::fclose(f);
  if (written != bytes.size()) error = "write failed (disk full?)";
}

} // namespace

core::RegularizeOpts toRegularizeOpts(const Settings& s) {
  core::RegularizeOpts o;
  o.aspectThreshold = s.regularizeAspectThreshold;
  o.slack = s.regularizeSlack;
  o.aggressiveSlack = s.regularizeAggressiveSlack;
  o.extremeSliverAspect = s.regularizeExtremeAspect;
  o.maxNormalDeltaCos = std::cos(render::degToRad(s.regularizeNormalDeg));
  o.aggressiveNormalDeltaCos = std::cos(render::degToRad(s.regularizeAggressiveNormalDeg));
  return o;
}

core::DisplacementSettings toDisplacementSettings(const Settings& s) {
  core::DisplacementSettings d;
  d.mappingMode = s.mappingMode;
  d.scaleU = s.scaleU;
  d.scaleV = s.scaleV;
  d.offsetU = s.offsetU;
  d.offsetV = s.offsetV;
  d.rotation = s.rotation;
  // main.js: settings.amplitude = (invertDisplacement ? -1 : 1) * textureHeight
  // — s.amplitude here plays textureHeight's role (see buildPreviewParams).
  d.amplitude = s.invertDisplacement ? -s.amplitude : s.amplitude;
  d.symmetricDisplacement = s.symmetricDisplacement;
  d.noDownwardZ = s.noDownwardZ;
  d.bottomAngleLimit = s.bottomAngleLimit;
  d.topAngleLimit = s.topAngleLimit;
  d.mappingBlend = s.mappingBlend;
  d.seamBandWidth = s.seamBandWidth;
  d.capAngle = s.capAngle;
  d.cylinderCenterX = s.cylinderCenterX;
  d.cylinderCenterY = s.cylinderCenterY;
  d.cylinderRadius = s.cylinderRadius;
  d.boundaryFalloff = s.boundaryFalloff;
  d.blendNormalSmoothing = s.blendNormalSmoothing;
  return d;
}

void restoreOriginalPose(std::vector<float>& positions, std::vector<float>* normals,
                         const render::Quat& poseRot, const Vec3& poseTrans) {
  const bool identity = std::abs(poseRot.w) > 1 - 1e-12;
  if (identity) {
    if (poseTrans.x == 0 && poseTrans.y == 0 && poseTrans.z == 0) return;
    for (size_t i = 0; i + 2 < positions.size(); i += 3) {
      positions[i] -= (float)poseTrans.x;
      positions[i + 1] -= (float)poseTrans.y;
      positions[i + 2] -= (float)poseTrans.z;
    }
    return;
  }
  const render::Quat rotInv{-poseRot.x, -poseRot.y, -poseRot.z, poseRot.w};
  for (size_t i = 0; i + 2 < positions.size(); i += 3) {
    Vec3 v{positions[i] - poseTrans.x, positions[i + 1] - poseTrans.y,
          positions[i + 2] - poseTrans.z};
    v = rotInv.rotate(v);
    positions[i] = (float)v.x;
    positions[i + 1] = (float)v.y;
    positions[i + 2] = (float)v.z;
  }
  if (normals) {
    for (size_t i = 0; i + 2 < normals->size(); i += 3) {
      Vec3 v{(*normals)[i], (*normals)[i + 1], (*normals)[i + 2]};
      v = rotInv.rotate(v);
      (*normals)[i] = (float)v.x;
      (*normals)[i + 1] = (float)v.y;
      (*normals)[i + 2] = (float)v.z;
    }
  }
}

PipelineRunner::~PipelineRunner() {
  if (_thread.joinable()) _thread.join();
}

bool PipelineRunner::start(PipelineJob job, AppState& state, ModelSession& session,
                           const core::ImageDataRGBA& image, const std::string& savePath) {
  if (_running.load(std::memory_order_acquire)) return false;
  if (session.geometry().positions.empty() || image.data.empty()) return false;

  // Precision masking must be baked back into the base mesh first — the
  // pipeline always runs on session.geometry() (the base mesh), matching
  // handleExport/bakeTextures' `if (precisionMaskingEnabled) deactivate...`.
  if (state.precisionMasking) session.setPrecisionMasking(false);

  _job = job;
  _savePath = savePath;
  _geoStorage = session.geometry();
  _imageStorage = image;
  _poseRotSnapshot = session.poseRot();
  _poseTransSnapshot = session.poseTrans();
  _bakeKeepMask = state.bakeKeepMask;
  _startedGeometryEpoch = session.geometryEpoch();
  _newMeshName = state.meshName + "_baked";

  const bool invert = state.brushMode == BrushMode::Include;
  const bool hasAngleMask =
      state.settings.bottomAngleLimit > 0 || state.settings.topAngleLimit > 0;
  const auto& excluded = session.excludedFaces();
  if (!excluded.empty() || invert || hasAngleMask) {
    _faceWeightsStorage = buildCombinedFaceWeights(
        _geoStorage, excluded, invert, state.settings.bottomAngleLimit,
        state.settings.topAngleLimit);
  } else {
    _faceWeightsStorage.clear();
  }

  _input = core::ExportPipelineInput{};
  _input.geometry = &_geoStorage;
  _input.faceWeights = _faceWeightsStorage.empty() ? nullptr : &_faceWeightsStorage;
  _input.image = &_imageStorage;
  _input.settings = toExportPipelineSettings(state.settings);
  _input.bounds = session.bounds();
  _input.regularizeOpts = toRegularizeOpts(state.settings);
  _input.mode = job == PipelineJob::Bake ? core::PipelineMode::Bake : core::PipelineMode::Export;
  _input.subdivisionCap = core::subdivisionSafetyCap();

  _result.reset();
  _error.clear();
  {
    std::lock_guard<std::mutex> lock(_progressMutex);
    _progressFraction = 0.02;
    _progressLabel = "Subdividing mesh\xe2\x80\xa6";
  }
  state.pipelineRunning = true;
  state.pipelineProgress = 0.02f;
  state.pipelineStage = "Subdividing mesh\xe2\x80\xa6";
  state.pipelineErrorMessage.clear();

  _running.store(true, std::memory_order_release);
  _done.store(false, std::memory_order_relaxed);
  _thread = std::thread([this] { run(); });
  return true;
}

void PipelineRunner::run() {
  const bool isBake = _job == PipelineJob::Bake;

  auto setProgress = [this](double fraction, std::string label) {
    std::lock_guard<std::mutex> lock(_progressMutex);
    _progressFraction = fraction;
    _progressLabel = std::move(label);
  };

  // Segment weights/labels mirror _onExportPipelineEvent/_onBakePipelineEvent
  // exactly (export: subdivide1 .02-.30, regularize .30, subdivide2 .32-.38,
  // displace .38-.70, decimate .71-.96, repair .96; bake: subdivide1 .02-.36,
  // regularize .36, subdivide2 .38-.47, displace .47-.87).
  core::PipelineEvent onEvent = [&](const char* stage, double p,
                                    const core::PipelineEventInfo& info) {
    std::string s(stage);
    if (s == "subdivide1") {
      char buf[96];
      if (info.triCount >= 0)
        std::snprintf(buf, sizeof(buf), "Refining mesh: %lld tris (edge %.2fmm)",
                      (long long)info.triCount, info.longestEdge);
      else
        std::snprintf(buf, sizeof(buf), "Subdividing mesh\xe2\x80\xa6");
      setProgress(0.02 + p * (isBake ? 0.34 : 0.28), buf);
    } else if (s == "regularize") {
      setProgress(isBake ? 0.36 : 0.30, "Regularizing mesh\xe2\x80\xa6");
    } else if (s == "subdivide2") {
      char buf[96];
      if (info.triCount >= 0)
        std::snprintf(buf, sizeof(buf), "Refining mesh: %lld tris (edge %.2fmm)",
                      (long long)info.triCount, info.longestEdge);
      else
        std::snprintf(buf, sizeof(buf), "Subdividing mesh\xe2\x80\xa6");
      setProgress((isBake ? 0.38 : 0.32) + p * (isBake ? 0.09 : 0.06), buf);
    } else if (s == "displace") {
      if (p == 0) {
        char buf[80];
        std::snprintf(buf, sizeof(buf), "Applying displacement to %lld triangles\xe2\x80\xa6",
                     (long long)info.triCount);
        setProgress(isBake ? 0.47 : 0.38, buf);
      } else {
        setProgress((isBake ? 0.47 : 0.38) + p * (isBake ? 0.40 : 0.32),
                   "Displacing vertices\xe2\x80\xa6");
      }
    } else if (s == "decimate") {
      if (info.needsDecimation) {
        if (p == 0)
          setProgress(0.71, "Decimating mesh\xe2\x80\xa6");
        else
          setProgress(0.71 + p * 0.25, "Decimating mesh\xe2\x80\xa6");
      } else {
        setProgress(0.71 + p * 0.25, "Harvesting flat faces\xe2\x80\xa6");
      }
    } else if (s == "repair") {
      setProgress(0.96, "Repairing mesh\xe2\x80\xa6");
    }
  };

  std::optional<core::ExportPipelineResult> result =
      core::runExportPipeline(_input, onEvent, nullptr);

  if (!result) {
    _error = "pipeline aborted";
    _done.store(true, std::memory_order_release);
    return;
  }

  if (_job != PipelineJob::Bake) {
    setProgress(0.97, _job == PipelineJob::Export3mf ? "Writing 3MF\xe2\x80\xa6"
                                                     : "Writing STL\xe2\x80\xa6");
    restoreOriginalPose(result->geometry.positions,
                        result->geometry.normals.empty() ? nullptr : &result->geometry.normals,
                        _poseRotSnapshot, _poseTransSnapshot);
    const std::vector<uint8_t> bytes = _job == PipelineJob::Export3mf
                                          ? core::export3MF(result->geometry)
                                          : core::exportSTL(result->geometry);
    writeFile(_savePath, bytes, _error);
  } else {
    setProgress(0.90, "Finalizing\xe2\x80\xa6");
  }

  _result = std::move(result);
  setProgress(1.0, "Done");
  _done.store(true, std::memory_order_release);
}

void PipelineRunner::poll(AppState& state, ModelSession& session) {
  if (!_running.load(std::memory_order_acquire)) return;
  {
    std::lock_guard<std::mutex> lock(_progressMutex);
    state.pipelineProgress = (float)_progressFraction;
    state.pipelineStage = _progressLabel;
  }
  if (!_done.load(std::memory_order_acquire)) return;

  if (_thread.joinable()) _thread.join();
  _running.store(false, std::memory_order_release);
  state.pipelineRunning = false;

  if (!_error.empty()) {
    state.pipelineErrorMessage = _error;
    _error.clear();
    _result.reset();
    return;
  }
  if (!_result) return; // aborted (shouldAbort is never set, so unreachable)

  if (_job == PipelineJob::Bake) {
    // Stale: the model changed underneath (e.g. a new file was loaded) while
    // the bake was running — discard rather than clobber the new model.
    if (session.geometryEpoch() == _startedGeometryEpoch) {
      std::vector<int32_t> preExcluded;
      if (_bakeKeepMask) {
        for (size_t i = 0; i < _result->faceParentId.size(); i++) {
          const int32_t parent = _result->faceParentId[i];
          const bool wasExcluded = !_faceWeightsStorage.empty() &&
                                   (size_t)parent * 3 < _faceWeightsStorage.size() &&
                                   _faceWeightsStorage[(size_t)parent * 3] > 0.99f;
          if (!wasExcluded) preExcluded.push_back((int32_t)i);
        }
      }
      state.meshName = _newMeshName;
      session.setGeometry(std::move(_result->geometry), state.meshName);
      session.seedExcludedFaces(preExcluded);
      state.displacementPreview3D = false;
      state.meshDirty = false; // freshly baked
      _bakeJustCompleted = true;
    }
  } else {
    state.triLimitWarning = _result->safetyCapHit;
  }
  _result.reset();
}

} // namespace app
