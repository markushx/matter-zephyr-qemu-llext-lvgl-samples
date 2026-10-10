/*
 * LLEXT UI extension: shows the bulb as a round lamp on a dark background
 * whose brightness follows the level, replacing the app's built-in arc
 * widget. See include/bulb_ui_extension.h for the contract, and ../ui_bar
 * for a second, different UI.
 */

#include <zephyr/llext/symbol.h>

#include <bulb_ui_extension.h>

static lv_obj_t *led;
static lv_obj_t *caption;

void ui_create(lv_obj_t *parent)
{
	/* `parent` is this UI's own container, so restyling it is fine. */
	lv_obj_set_style_bg_color(parent, lv_color_hex(0x202020), 0);
	lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);

	led = lv_led_create(parent);
	lv_obj_set_size(led, 110, 110);
	lv_obj_align(led, LV_ALIGN_CENTER, 0, -15);
	lv_led_set_color(led, lv_palette_main(LV_PALETTE_AMBER));

	caption = lv_label_create(parent);
	lv_obj_set_style_text_color(caption, lv_color_white(), 0);
	lv_obj_align(caption, LV_ALIGN_CENTER, 0, 65);
}
LL_EXTENSION_SYMBOL(ui_create);

void ui_update(bool on, uint8_t percent)
{
	if (on) {
		lv_led_set_brightness(led, LV_LED_BRIGHT_MIN +
					   (LV_LED_BRIGHT_MAX - LV_LED_BRIGHT_MIN) * percent / 100);
		lv_label_set_text_fmt(caption, "Lamp on, %u %%", percent);
	} else {
		lv_led_off(led);
		lv_label_set_text(caption, "Lamp off");
	}
}
LL_EXTENSION_SYMBOL(ui_update);
