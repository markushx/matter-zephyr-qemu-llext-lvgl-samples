/*
 * LLEXT UI extension: shows the bulb as a horizontal "dimmer" bar with a
 * percentage readout, replacing the app's built-in arc widget. See
 * include/bulb_ui_extension.h for the contract, and ../ui_led for a second,
 * different UI.
 */

#include <zephyr/llext/symbol.h>

#include <bulb_ui_extension.h>

static lv_obj_t *bar;
static lv_obj_t *value;

void ui_create(lv_obj_t *parent)
{
	lv_obj_t *title = lv_label_create(parent);

	lv_label_set_text(title, "Dimmer");
	lv_obj_align(title, LV_ALIGN_CENTER, 0, -45);

	bar = lv_bar_create(parent);
	lv_obj_set_size(bar, 220, 28);
	lv_obj_align(bar, LV_ALIGN_CENTER, 0, 0);
	lv_bar_set_range(bar, 0, 100);

	value = lv_label_create(parent);
	lv_obj_align(value, LV_ALIGN_CENTER, 0, 45);
}
LL_EXTENSION_SYMBOL(ui_create);

void ui_update(bool on, uint8_t percent)
{
	lv_bar_set_value(bar, on ? percent : 0, LV_ANIM_OFF);
	lv_obj_set_style_bg_color(bar, lv_palette_main(on ? LV_PALETTE_BLUE : LV_PALETTE_GREY),
				  LV_PART_INDICATOR);

	if (on) {
		lv_label_set_text_fmt(value, "%u %%", percent);
	} else {
		lv_label_set_text(value, "off");
	}
}
LL_EXTENSION_SYMBOL(ui_update);
