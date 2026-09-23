#ifndef HMI_SCREEN_H
#define HMI_SCREEN_H

#include <lvgl.h>

/*
 * Renders the full HMI demo screen (tab view with Gauges / Controls /
 * Status / Trend / Vector pages) as the LVGL screen. Mirrors the Linux
 * simulator demo layout 1:1 for a 320x480 portrait display.
 */
int hmi_show_ui(lv_obj_t *parent);

#endif /* HMI_SCREEN_H */