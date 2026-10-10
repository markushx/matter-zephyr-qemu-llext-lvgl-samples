/*
 * Contract between the 3_llext_lvgl app and a UI extension loaded through the
 * ExtensionLoaderCluster. An extension that exports both functions below
 * (LL_EXTENSION_SYMBOL) replaces the app's built-in bulb widget until it is
 * unloaded or another extension is loaded.
 *
 * Both are only ever called on the app's GUI thread, so they may use LVGL
 * freely -- limited to the functions the app exports to extensions, see
 * src/gui/LvglExports.c.
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Build the UI as children of `parent`: an unstyled, screen-sized container
 * that belongs to this UI alone. The app deletes it (and with it everything
 * created here) before the extension is unloaded, so there is no destroy
 * hook -- but that also means nothing may outlive `parent` (no timers,
 * animations or objects created elsewhere).
 */
void ui_create(lv_obj_t *parent);

/*
 * Show the bulb's state. `percent` is the Matter level scaled to 0-100.
 * Called once right after ui_create() and then on every state change.
 */
void ui_update(bool on, uint8_t percent);

#ifdef __cplusplus
}
#endif
