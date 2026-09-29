#ifndef HMI_GAUGE_H
#define HMI_GAUGE_H

#include <lvgl.h>
#include <stdint.h>

#define HMI_MAX_GAUGES 8

extern uint32_t colorscheme[];

lv_obj_t *hmi_ui_gauge_create(lv_obj_t *parent, int size, const char *title, const char *unit);
void hmi_ui_widget_set_pos(lv_obj_t *widget, int x, int y);
void hmi_ui_gauge_set_range(lv_obj_t *gauge, int32_t min, int32_t max);
void hmi_ui_gauge_bind_value(lv_obj_t *gauge, float *value);
void hmi_gauge_show_ui(lv_obj_t *parent);

/* Demo-application value backing the gauge created by hmi_gauge_show_ui().
 * Read/written by the demo comm handlers and telemetry timer.
 */
float hmi_gauge_demo_get(void);
void hmi_gauge_demo_set(float value);

/* Address of the same value, for hmi_ui_gauge_bind_value(). Lets an
 * application build its own gauge (size, range, placement) that still
 * tracks what the comm handlers write.
 */
float *hmi_gauge_demo_value(void);

#endif /* HMI_GAUGE_H */