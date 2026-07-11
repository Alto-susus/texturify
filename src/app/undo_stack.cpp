#include "app/undo_stack.h"

#include <unordered_set>

#include "app/actions.h"

namespace app {

UndoEntry UndoStack::capture(const AppState& state, const ModelSession& session) const {
  UndoEntry e;
  e.settings = captureSettingsSnapshot(state);
  e.selectionMode = state.brushMode == BrushMode::Include;
  e.excluded = session.collectMaskFaces();
  return e;
}

bool UndoStack::equal(const UndoEntry& a, const UndoEntry& b) const {
  if (!a.settings.settingsEqual(b.settings)) return false;
  if (a.selectionMode != b.selectionMode) return false;
  if (a.excluded.size() != b.excluded.size()) return false;
  std::unordered_set<int32_t> sb(b.excluded.begin(), b.excluded.end());
  for (int32_t v : a.excluded)
    if (!sb.count(v)) return false;
  return true;
}

void UndoStack::reset(const AppState& state, const ModelSession& session) {
  _undoStack.clear();
  _redoStack.clear();
  _pendingAt.reset();
  _baseline = capture(state, session);
}

void UndoStack::commit(const AppState& state, const ModelSession& session) {
  UndoEntry next = capture(state, session);
  if (_baseline && equal(*_baseline, next)) return;
  if (_baseline) {
    _undoStack.push_back(std::move(*_baseline));
    if (_undoStack.size() > kLimit) _undoStack.erase(_undoStack.begin());
  }
  _redoStack.clear();
  _baseline = std::move(next);
}

void UndoStack::update(const AppState& state, const ModelSession& session) {
  if (!_pendingAt || std::chrono::steady_clock::now() < *_pendingAt) return;
  _pendingAt.reset();
  commit(state, session);
}

void UndoStack::commitBaselineAsStep() {
  if (_baseline) {
    _undoStack.push_back(std::move(*_baseline));
    if (_undoStack.size() > kLimit) _undoStack.erase(_undoStack.begin());
    _baseline.reset();
  }
  _redoStack.clear();
}

void UndoStack::rebaseline(const AppState& state, const ModelSession& session) {
  _baseline = capture(state, session);
}

void UndoStack::scheduleCapture() {
  _pendingAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(kDebounceMs);
}

void UndoStack::flush(const AppState& state, const ModelSession& session) {
  if (!_pendingAt) return;
  _pendingAt.reset();
  commit(state, session);
}

void UndoStack::applyEntry(AppState& state, ModelSession& session, const UndoEntry& e) {
  applySettingsSnapshot(state, e.settings);
  session.setSelectionModeImmediate(e.selectionMode);
  session.seedExcludedFaces(e.excluded);
  pendingPresetName.reset();
  if (!e.settings.activeMapName.empty() && !e.settings.activeMapIsCustom)
    pendingPresetName = e.settings.activeMapName;
}

bool UndoStack::undo(AppState& state, ModelSession& session) {
  flush(state, session);
  if (_undoStack.empty()) return false;
  UndoEntry prev = std::move(_undoStack.back());
  _undoStack.pop_back();
  if (_baseline) _redoStack.push_back(std::move(*_baseline));
  applyEntry(state, session, prev);
  _baseline = std::move(prev);
  return true;
}

bool UndoStack::redo(AppState& state, ModelSession& session) {
  flush(state, session);
  if (_redoStack.empty()) return false;
  UndoEntry next = std::move(_redoStack.back());
  _redoStack.pop_back();
  if (_baseline) _undoStack.push_back(std::move(*_baseline));
  applyEntry(state, session, next);
  _baseline = std::move(next);
  return true;
}

} // namespace app
