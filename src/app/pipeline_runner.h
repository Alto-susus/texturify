// std::thread-backed runner for the export/bake pipeline
// (core::runExportPipeline) — port of main.js's handleExport/bakeTextures
// plus the worker-thread machinery (ensurePipelineWorker/runPipeline). All
// ModelSession/Viewer/GL touches happen in poll() on the main thread; the
// background thread only calls into core/ (pure CPU, thread-safe).
#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "app/app_state.h"
#include "core/export_pipeline.h"
#include "render/math3d.h"

namespace app {

class ModelSession;

enum class PipelineJob { ExportStl, Export3mf, Bake };

// Settings -> core::DisplacementSettings, shared by the pipeline and the
// Smart Resolution button (core::computeSmartResolution takes the same
// struct). Folds invertDisplacement's sign into amplitude, matching
// main.js's `settings.amplitude = (invert ? -1 : 1) * textureHeight`.
core::DisplacementSettings toDisplacementSettings(const Settings& s);

// Settings -> core::RegularizeOpts, shared by the pipeline and
// ModelSession's live 3D displacement-preview subdivision.
core::RegularizeOpts toRegularizeOpts(const Settings& s);

// Port of main.js's _restoreOriginalPose(): maps positions/normals (which run
// in the in-app working space) back to the model's original file pose,
// undoing both import centering and any in-app rotation. Used by the export
// pipeline and by .texturify project save (model.stl is written in original
// pose, see app/project_file.h).
void restoreOriginalPose(std::vector<float>& positions, std::vector<float>* normals,
                         const render::Quat& poseRot, const core::Vec3& poseTrans);

class PipelineRunner {
public:
  ~PipelineRunner();

  // Snapshots geometry/settings/texture/pose and starts the background
  // thread. Returns false (no-op) if a run is already in progress, or the
  // model/texture aren't ready — matches handleExport/bakeTextures' guard
  // `if (!currentGeometry || !activeMapEntry || isExporting || isBaking)`.
  // savePath is only used for Export jobs (already chosen via a save dialog).
  bool start(PipelineJob job, AppState& state, ModelSession& session,
             const core::ImageDataRGBA& image, const std::string& savePath);

  // Call once per frame. Cheap when idle. Updates state.pipeline* while
  // running; on completion, applies the result (session.setGeometry for
  // Bake, already-written file for Export) and clears pipelineRunning.
  void poll(AppState& state, ModelSession& session);

  bool running() const { return _running.load(std::memory_order_acquire); }

  // True exactly once after poll() applies a successful Bake result — the
  // caller should clear undo/redo history then (main.js's _clearUndoStacks
  // in adoptBakedGeometry: mask indices reference the pre-bake triangle
  // set). Consuming resets it to false.
  bool consumeBakeCompleted() {
    bool v = _bakeJustCompleted;
    _bakeJustCompleted = false;
    return v;
  }

private:
  void run(); // worker-thread entry point

  std::thread _thread;
  std::atomic<bool> _running{false};
  std::atomic<bool> _done{false};

  std::mutex _progressMutex;
  double _progressFraction = 0;
  std::string _progressLabel;

  // Captured at start(); read-only from the worker thread while it runs.
  PipelineJob _job = PipelineJob::ExportStl;
  std::string _savePath;
  core::ExportPipelineInput _input;
  core::Geometry _geoStorage;         // _input.geometry points here
  core::ImageDataRGBA _imageStorage;  // _input.image points here
  std::vector<float> _faceWeightsStorage; // _input.faceWeights points here (may stay empty)
  render::Quat _poseRotSnapshot;
  core::Vec3 _poseTransSnapshot{0, 0, 0};
  bool _bakeKeepMask = true;
  int _startedGeometryEpoch = -1;
  std::string _newMeshName; // bake: old name + "_baked"

  // Written by the worker thread; valid for poll() to read once _done is
  // true (the atomic release/acquire pair above is the sync point).
  std::optional<core::ExportPipelineResult> _result;
  std::string _error;
  bool _bakeJustCompleted = false;
};

} // namespace app
