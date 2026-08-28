#pragma once

#include <cstdint>

namespace BulbUi {

// Starts the GUI thread. Must be called after CHIP's stack (and ideally the
// whole server) is already initialized -- see the .cpp for why this can't
// be a K_THREAD_DEFINE'd, auto-started-at-boot thread.
void Init();

// Push the current on/off + level state to the bulb widget. Safe to call
// from the CHIP thread -- the update is queued and applied on the UI
// thread, since LVGL itself is not thread-safe.
void SetState(bool onOff, uint8_t level);

} // namespace BulbUi
