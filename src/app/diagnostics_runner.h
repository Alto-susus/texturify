// std::thread-backed runner for core::runExpensiveDiagnostics — port of
// main.js's "Run Advanced Checks" button + diagToken cancellation
// (main.js:1263-1289). Mirrors PipelineRunner's threading pattern: the
// geometry is snapshotted by value at start() so the worker thread never
// touches ModelSession/GL.
#pragma once

#include <atomic>
#include <thread>

#include "core/mesh_validation.h"

namespace app {

class DiagnosticsRunner {
public:
  ~DiagnosticsRunner();

  // No-op (returns false) if a run is already in progress. geometryEpoch is
  // ModelSession::geometryEpoch() at call time — poll() uses it to discard a
  // stale result if the mesh changed mid-run (main.js's `diagToken !== myToken`).
  bool start(const core::Geometry& geo, int geometryEpoch);

  // Call once per frame; cheap when idle. Returns true exactly once, with
  // `out` filled, when a run completes and its geometryEpoch still matches
  // currentEpoch (and it wasn't aborted). A stale or aborted result is
  // discarded silently.
  bool poll(int currentEpoch, core::ExpensiveDiagnostics& out);

  bool running() const { return _running.load(std::memory_order_acquire); }

  // Cooperative abort (mesh changed mid-run, or app closing) — does not
  // block; the worker polls this periodically inside runExpensiveDiagnostics.
  void cancel() { _abort.store(true, std::memory_order_relaxed); }

private:
  void run(); // worker-thread entry point

  std::thread _thread;
  std::atomic<bool> _running{false};
  std::atomic<bool> _done{false};
  std::atomic<bool> _abort{false};

  core::Geometry _geoStorage; // captured at start(); worker reads only
  int _startedEpoch = -1;

  core::ExpensiveDiagnostics _result; // written by worker; valid once _done
};

} // namespace app
