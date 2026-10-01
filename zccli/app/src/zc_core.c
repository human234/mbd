/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * zccli frozen runtime. Do not edit -- this file is owned by the zccli
 * image and is copied back on every build.
 *
 * It exists so that a user application never has to know that there is a
 * Zephyr kernel, a display driver stack or a board underneath. The display
 * is brought up here, then zc_user_init() gets the active screen to draw
 * on, then hmi_show() runs the LVGL loop forever.
 *
 * Both symbols below are deliberately weak, so a user file can override
 * either one without touching this file.
 */
#include <lvgl.h>

#include <hmi/hmi.h>

#include "zccli.h"

/* Placeholder: shown when the user's file did not define zc_user_init().
 * A silently blank screen would be a confusing first experience, so the
 * missing function gets stated on the panel instead.
 */
__attribute__((weak)) void zc_user_init(lv_obj_t *root)
{
	lv_obj_t *lbl = lv_label_create(root);

	lv_label_set_text(lbl,
			  "zccli\n\n"
			  "This screen is empty because your\n"
			  "file does not define:\n\n"
			  "  void zc_user_init(lv_obj_t *root)\n\n"
			  "Put your UI code in there.");
	lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_color(lbl, lv_color_hex(0x00E5FF), 0);
	lv_obj_set_style_text_font(lbl, &lv_font_montserrat_18, 0);
	lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
}

__attribute__((weak)) int main(void)
{
	if (hmi_init() < 0) {
		return -1;
	}

	zc_user_init(lv_screen_active());

	hmi_show();

	return 0; /* unreachable: hmi_show() loops forever */
}
