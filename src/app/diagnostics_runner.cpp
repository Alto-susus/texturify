#include "app/diagnostics_runner.h"

namespace app {

DiagnosticsRunner::~DiagnosticsRunner() {
  if (_thread.joinable()) {
    _abort.store(true, std::memory_order_relaxed);
    _thread.join();
  }
}

bool DiagnosticsRunner::start(const core::Geometry& geo, int geometryEpoch) {
  if (_running.load(std::memory_order_acquire)) return false;
  if (_thread.joinable()) _thread.join(); // previous run already finished

  _geoStorage = geo;
  _startedEpoch = geometryEpoch;
  _abort.store(false, std::memory_order_relaxed);
  _done.store(false, std::memory_order_relaxed);
  _running.store(true, std::memory_order_release);
  _thread = std::thread(&DiagnosticsRunner::run, this);
  return true;
}

void DiagnosticsRunner::run() {
  auto shouldAbort = [this]() { return _abort.load(std::memory_order_relaxed); };
  _result = core::runExpensiveDiagnostics(_geoStorage, shouldAbort);
  _running.store(false, std::memory_order_release);
  _done.store(true, std::memory_order_release);
}

bool DiagnosticsRunner::poll(int currentEpoch, core::ExpensiveDiagnostics& out) {
  if (!_done.load(std::memory_order_acquire)) return false;
  _done.store(false, std::memory_order_relaxed);
  if (_thread.joinable()) _thread.join();

  if (currentEpoch != _startedEpoch || _result.aborted) return false;
  out = std::move(_result);
  return true;
}

} // namespace app
