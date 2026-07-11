// Port of main.js's undo/redo (lines ~5623-5781): a debounced snapshot stack
// over settings + exclusion mask (not a command stack — every step is a full
// state snapshot, matching the JS design).
#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <vector>

#include "app/settings_snapshot.h"

namespace app {

class ModelSession;

struct UndoEntry {
  SettingsSnapshot settings;
  bool selectionMode = false;
  std::vector<int32_t> excluded;
};

class UndoStack {
public:
  // Clears both stacks and takes a fresh baseline — main.js's
  // _clearUndoStacks (fresh model load, bake: the mesh's triangle indices no
  // longer match what's on the stack).
  void reset(const AppState& state, const ModelSession& session);

  // Call once per frame: commits a pending debounced capture once the
  // window elapses (main.js's _commitUndoCapture via setTimeout).
  void update(const AppState& state, const ModelSession& session);

  // Schedules a capture ~400ms out, restarting the debounce window on every
  // call — main.js's _scheduleUndoCapture. Call after any settings edit,
  // mask change, or pointerup (paint stroke end).
  void scheduleCapture();

  // Commits an in-flight debounced capture immediately, if one is pending —
  // main.js's _flushUndoCapture, called before undo()/redo().
  void flush(const AppState& state, const ModelSession& session);

  // Applies the previous/next snapshot to state/session. Returns false
  // (no-op) if the respective stack is empty.
  bool undo(AppState& state, ModelSession& session);
  bool redo(AppState& state, ModelSession& session);

  bool canUndo() const { return !_undoStack.empty(); }
  bool canRedo() const { return !_redoStack.empty(); }

  // For atomic, immediately-committed actions (Reset to Defaults) rather
  // than the debounced path: push the current baseline as an undo step and
  // clear redo (main.js's manual _undoStack.push(_baselineSnapshot) +
  // _redoStack.length = 0 in resetSettingsToDefaults), then — after the
  // caller mutates state/session — rebaseline() to capture the new state
  // without touching either stack (main.js's trailing
  // `_baselineSnapshot = _captureUndoSnapshot()`).
  void commitBaselineAsStep();
  void rebaseline(const AppState& state, const ModelSession& session);

  // Set by undo()/redo() when the applied entry names an active *preset*
  // (main.js's _selectPresetByName only searches IMAGE_PRESETS — custom
  // textures are never restored by undo/redo, matching that fidelity gap).
  // The caller looks it up and reselects (without reapplying preset
  // defaults), then should clear this.
  std::optional<std::string> pendingPresetName;

private:
  UndoEntry capture(const AppState& state, const ModelSession& session) const;
  bool equal(const UndoEntry& a, const UndoEntry& b) const;
  void commit(const AppState& state, const ModelSession& session);
  void applyEntry(AppState& state, ModelSession& session, const UndoEntry& e);

  static constexpr size_t kLimit = 50;
  static constexpr int kDebounceMs = 400;

  std::vector<UndoEntry> _undoStack, _redoStack;
  std::optional<UndoEntry> _baseline;
  std::optional<std::chrono::steady_clock::time_point> _pendingAt;
};

} // namespace app
