#pragma once

#include <cstdint>

namespace BulbUi {

// Push the current on/off + level state to the bulb widget. Safe to call
// from the CHIP thread -- the update is queued and applied on the UI
// thread, since LVGL itself is not thread-safe.
void SetState(bool onOff, uint8_t level);

} // namespace BulbUi
