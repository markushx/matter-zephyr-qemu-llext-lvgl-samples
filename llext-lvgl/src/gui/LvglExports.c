/*
 * The LVGL API a UI extension (see include/bulb_ui_extension.h) may call.
 *
 * An extension can only link against symbols the app explicitly exports.
 * Listing a function here also keeps it in the image at all: the app itself
 * uses very little of LVGL, and the linker's section garbage collection
 * would otherwise drop e.g. the whole bar widget. An extension that calls
 * something not listed here fails to load ("Undefined symbol ..." on the
 * console, TransferStatus = Error) -- add it here and rebuild the app.
 *
 * LVGL functions that are `static inline` in its headers (lv_pct(),
 * lv_obj_set_style_pad_all(), ...) are compiled into the extension itself and
 * need no entry; whatever they call does.
 */

#include <zephyr/llext/symbol.h>

#include <lvgl.h>

/* Objects: creation, geometry, flags */
EXPORT_SYMBOL(lv_obj_create);
EXPORT_SYMBOL(lv_obj_delete);
EXPORT_SYMBOL(lv_obj_clean);
EXPORT_SYMBOL(lv_obj_set_size);
EXPORT_SYMBOL(lv_obj_set_width);
EXPORT_SYMBOL(lv_obj_set_height);
EXPORT_SYMBOL(lv_obj_set_pos);
EXPORT_SYMBOL(lv_obj_align);
EXPORT_SYMBOL(lv_obj_align_to);
EXPORT_SYMBOL(lv_obj_add_flag);
EXPORT_SYMBOL(lv_obj_remove_flag);
EXPORT_SYMBOL(lv_obj_add_state);
EXPORT_SYMBOL(lv_obj_remove_state);
EXPORT_SYMBOL(lv_obj_remove_style);
EXPORT_SYMBOL(lv_obj_remove_style_all);

/* Local styles */
EXPORT_SYMBOL(lv_obj_set_style_bg_color);
EXPORT_SYMBOL(lv_obj_set_style_bg_opa);
EXPORT_SYMBOL(lv_obj_set_style_border_color);
EXPORT_SYMBOL(lv_obj_set_style_border_width);
EXPORT_SYMBOL(lv_obj_set_style_radius);
EXPORT_SYMBOL(lv_obj_set_style_pad_top);
EXPORT_SYMBOL(lv_obj_set_style_pad_bottom);
EXPORT_SYMBOL(lv_obj_set_style_pad_left);
EXPORT_SYMBOL(lv_obj_set_style_pad_right);
EXPORT_SYMBOL(lv_obj_set_style_text_color);
EXPORT_SYMBOL(lv_obj_set_style_text_align);
EXPORT_SYMBOL(lv_obj_set_style_arc_color);
EXPORT_SYMBOL(lv_obj_set_style_arc_width);

/* Colors */
EXPORT_SYMBOL(lv_palette_main);
EXPORT_SYMBOL(lv_palette_lighten);
EXPORT_SYMBOL(lv_palette_darken);
EXPORT_SYMBOL(lv_color_hex);
EXPORT_SYMBOL(lv_color_black);
EXPORT_SYMBOL(lv_color_white);

/* Widgets */
EXPORT_SYMBOL(lv_label_create);
EXPORT_SYMBOL(lv_label_set_text);
EXPORT_SYMBOL(lv_label_set_text_fmt);

EXPORT_SYMBOL(lv_arc_create);
EXPORT_SYMBOL(lv_arc_set_value);
EXPORT_SYMBOL(lv_arc_set_range);
EXPORT_SYMBOL(lv_arc_set_rotation);
EXPORT_SYMBOL(lv_arc_set_bg_angles);

EXPORT_SYMBOL(lv_bar_create);
EXPORT_SYMBOL(lv_bar_set_value);
EXPORT_SYMBOL(lv_bar_set_range);

EXPORT_SYMBOL(lv_led_create);
EXPORT_SYMBOL(lv_led_set_color);
EXPORT_SYMBOL(lv_led_set_brightness);
EXPORT_SYMBOL(lv_led_on);
EXPORT_SYMBOL(lv_led_off);
