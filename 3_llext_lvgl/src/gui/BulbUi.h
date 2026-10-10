#pragma once

#include <cstdint>

#include <lvgl.h>

namespace BulbUi {

// A bulb UI: the built-in widget, or one provided by a loaded extension (see
// include/bulb_ui_extension.h for what the two functions must do).
struct Ui
{
    void (*create)(lv_obj_t * parent);
    void (*update)(bool on, uint8_t percent);
};

// Starts the GUI thread. Must be called after CHIP's stack (and ideally the
// whole server) is already initialized -- see the .cpp for why this can't
// be a K_THREAD_DEFINE'd, auto-started-at-boot thread.
void Init();

// Push the current on/off + level state to the bulb UI. Safe to call
// from the CHIP thread -- the update is queued and applied on the UI
// thread, since LVGL itself is not thread-safe.
void SetState(bool onOff, uint8_t level);

// Replace the UI with `ui`, or with the built-in one if null. Safe to call
// from the CHIP thread: the switch happens on the UI thread and this blocks
// until it's done -- so once SetUi(nullptr) has returned, the previous UI's
// code is guaranteed not to be running or referenced any more, and an
// extension providing it can be unloaded. Returns false (nothing switched)
// if the GUI thread isn't up.
bool SetUi(const Ui * ui);

} // namespace BulbUi
