#ifndef HMI_SLIDER_H
#define HMI_SLIDER_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#define HMI_MAX_SLIDERS 8

typedef void (*hmi_slider_cb_t)(lv_obj_t *slider, double value, void *user_data);

typedef struct {
	lv_obj_t *slider;
	lv_obj_t *readout;
	lv_obj_t *value_label;
	double min, max, step;
	int precision;
	const char *unit;
	hmi_slider_cb_t cb;
	void *user_data;
	bool used;
} hmi_slider_t;

lv_obj_t *hmi_slider_create(lv_obj_t *parent, int x, int y, int width);
void hmi_slider_set_range(lv_obj_t *slider, double min, double max, double step);
void hmi_slider_set_value(lv_obj_t *slider, double value);
double hmi_slider_get_value(lv_obj_t *slider);
void hmi_slider_set_precision(lv_obj_t *slider, int decimals);
void hmi_slider_set_unit(lv_obj_t *slider, const char *unit);
void hmi_slider_set_callback(lv_obj_t *slider, hmi_slider_cb_t cb, void *user_data);
void hmi_slider_set_enabled(lv_obj_t *slider, bool enabled);
hmi_slider_t *hmi_slider_get(lv_obj_t *slider);

#endif /* HMI_SLIDER_H */