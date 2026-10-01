/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * zccli public contract.
 *
 * This header plus <lvgl.h> and <hmi/hmi.h> is everything you need.
 * There is no setup, no build system and no RTOS code to write.
 *
 * Your application provides any or all of:
 *
 *   void zc_user_init(lv_obj_t *root)  -- build your screen here
 *   int  main(void)                    -- optional, takes over startup
 *
 * Both are weak symbols in the runtime, so whatever you define wins.
 * Define neither and you get a placeholder screen that tells you so.
 */
#pragma once

#include <lvgl.h>

/* Called once with the active screen, after the display is up. */
void zc_user_init(lv_obj_t *root);
