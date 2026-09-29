/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Full HMI demo using the HMI library. The module provides the display
 * lifecycle (hmi_init/hmi_show) and all widgets; this app only wires
 * them together through the shared demo screen (hmi_show_ui).
 */

#include <lvgl.h>

#include <hmi/hmi.h>

int main(void)
{
	if (hmi_init() < 0) {
		return 0;
	}

	hmi_show_ui(lv_screen_active());
	hmi_show();

	return 0;   /* unreachable: hmi_show() loops forever */
}