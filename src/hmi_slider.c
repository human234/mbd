#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include <hmi/hmi_slider.h>
#include <hmi/hmi_gauge.h>

#define HMI_SLIDER_READOUT_PAD 8
#define HMI_SLIDER_GAP         0
#define HMI_SLIDER_HEIGHT      24

static hmi_slider_t g_slider[HMI_MAX_SLIDERS];

static hmi_slider_t *slider_find(lv_obj_t *slider);
static void slider_value_changed_cb(lv_event_t *e);
static void slider_refresh_label(hmi_slider_t *s);

lv_obj_t *hmi_slider_create(lv_obj_t *parent, int x, int y, int width)
{
	if (width <= 0) {
		return NULL;
	}

	const lv_font_t *font;
	if (width >= 240) {
		font = &lv_font_montserrat_28;
	} else if (width >= 180) {
		font = &lv_font_montserrat_22;
	} else if (width >= 120) {
		font = &lv_font_montserrat_18;
	} else {
		font = &lv_font_montserrat_16;
	}

	int readout_height = lv_font_get_line_height(font) + 2 * HMI_SLIDER_READOUT_PAD;

	lv_obj_t *readout = lv_obj_create(parent);
	lv_obj_set_pos(readout, x, y);
	lv_obj_set_size(readout, width, readout_height);
	lv_obj_set_style_bg_color(readout, lv_color_hex(colorscheme[4]), 0);
	lv_obj_set_style_border_color(readout, lv_color_hex(colorscheme[8]), 0);
	lv_obj_set_style_border_width(readout, 1, 0);
	lv_obj_set_style_radius(readout, 0, 0);
	lv_obj_set_style_pad_all(readout, 0, 0);
	lv_obj_remove_flag(readout, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *value_label = lv_label_create(readout);
	lv_obj_align(value_label, LV_ALIGN_CENTER, 0, 0);
	lv_obj_set_style_text_font(value_label, font, 0);
	lv_obj_set_style_text_color(value_label, lv_color_hex(colorscheme[22]), 0);
	lv_label_set_text(value_label, "--");

	lv_obj_t *slider = lv_slider_create(parent);
	lv_obj_set_pos(slider, x, y + readout_height + HMI_SLIDER_GAP);
	lv_obj_set_size(slider, width, HMI_SLIDER_HEIGHT);
	lv_obj_set_style_radius(slider, 0, LV_PART_MAIN);
	lv_obj_set_style_radius(slider, 0, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(slider, lv_color_hex(colorscheme[23]), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_bg_color(slider, lv_color_hex(colorscheme[13]), LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(slider, lv_color_hex(colorscheme[22]), LV_PART_KNOB);
	lv_obj_set_style_radius(slider, 0, LV_PART_KNOB);
	lv_obj_set_style_bg_color(slider, lv_color_hex(colorscheme[13]), LV_PART_KNOB | LV_STATE_PRESSED);
	lv_obj_set_style_pad_all(slider, 4, LV_PART_KNOB);
	lv_obj_set_style_bg_color(slider, lv_color_hex(colorscheme[10]), LV_PART_MAIN | LV_STATE_DISABLED);
	lv_obj_set_style_bg_color(slider, lv_color_hex(colorscheme[10]), LV_PART_INDICATOR | LV_STATE_DISABLED);
	lv_obj_set_style_bg_color(slider, lv_color_hex(colorscheme[10]), LV_PART_KNOB | LV_STATE_DISABLED);

	lv_obj_add_event_cb(slider, slider_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

	hmi_slider_t *s = NULL;
	for (int i = 0; i < HMI_MAX_SLIDERS; i++) {
		if (!g_slider[i].used) {
			s = &g_slider[i];
			break;
		}
	}
	if (s) {
		s->used = true;
		s->slider = slider;
		s->readout = readout;
		s->value_label = value_label;
		s->min = 0.0;
		s->max = 100.0;
		s->step = 1.0;
		s->precision = 1;
		s->unit = NULL;
		s->cb = NULL;
		s->user_data = NULL;
		lv_slider_set_range(slider, 0, 100);
	}

	return slider;
}

void hmi_slider_set_range(lv_obj_t *slider, double min, double max, double step)
{
	hmi_slider_t *s = slider_find(slider);
	if (s == NULL) {
		return;
	}
	if (min > max) {
		double t = min;
		min = max;
		max = t;
	}
	if (step <= 0) {
		step = (max - min) / 100.0;
	}

	s->min = min;
	s->max = max;
	s->step = step;

	int steps = (int)((max - min) / step + 0.5);
	if (steps < 1) {
		steps = 1;
	}
	s->step = (max - min) / steps;

	lv_slider_set_range(slider, 0, steps);
	slider_refresh_label(s);
}

void hmi_slider_set_value(lv_obj_t *slider, double value)
{
	hmi_slider_t *s = slider_find(slider);
	if (s == NULL) {
		return;
	}
	if (value < s->min) {
		value = s->min;
	}
	if (value > s->max) {
		value = s->max;
	}

	int i = (int)((value - s->min) / s->step + 0.5);
	lv_slider_set_value(slider, i, LV_ANIM_OFF);
	slider_refresh_label(s);
}

double hmi_slider_get_value(lv_obj_t *slider)
{
	hmi_slider_t *s = slider_find(slider);
	if (s == NULL) {
		return 0.0;
	}
	return s->min + lv_slider_get_value(s->slider) * s->step;
}

void hmi_slider_set_precision(lv_obj_t *slider, int decimals)
{
	hmi_slider_t *s = slider_find(slider);
	if (s == NULL) {
		return;
	}
	if (decimals < 0) {
		decimals = 0;
	}
	if (decimals > 6) {
		decimals = 6;
	}
	s->precision = decimals;
	slider_refresh_label(s);
}

void hmi_slider_set_unit(lv_obj_t *slider, const char *unit)
{
	hmi_slider_t *s = slider_find(slider);
	if (s == NULL) {
		return;
	}
	s->unit = unit;
	slider_refresh_label(s);
}

void hmi_slider_set_callback(lv_obj_t *slider, hmi_slider_cb_t cb, void *user_data)
{
	hmi_slider_t *s = slider_find(slider);
	if (s == NULL) {
		return;
	}
	s->cb = cb;
	s->user_data = user_data;
}

void hmi_slider_set_enabled(lv_obj_t *slider, bool enabled)
{
	hmi_slider_t *s = slider_find(slider);
	if (s == NULL) {
		return;
	}
	if (enabled) {
		lv_obj_remove_state(s->slider, LV_STATE_DISABLED);
	} else {
		lv_obj_add_state(s->slider, LV_STATE_DISABLED);
	}
}

hmi_slider_t *hmi_slider_get(lv_obj_t *slider)
{
	return slider_find(slider);
}

static hmi_slider_t *slider_find(lv_obj_t *slider)
{
	for (int i = 0; i < HMI_MAX_SLIDERS; i++) {
		if (g_slider[i].used && g_slider[i].slider == slider) {
			return &g_slider[i];
		}
	}
	return NULL;
}

static void slider_value_changed_cb(lv_event_t *e)
{
	lv_obj_t *slider = lv_event_get_target_obj(e);
	hmi_slider_t *s = slider_find(slider);
	if (s == NULL) {
		return;
	}
	slider_refresh_label(s);
	if (s->cb) {
		s->cb(slider, s->min + lv_slider_get_value(s->slider) * s->step, s->user_data);
	}
}

static void slider_refresh_label(hmi_slider_t *s)
{
	double value = s->min + lv_slider_get_value(s->slider) * s->step;
	char buf[32];
	if (s->unit != NULL && s->unit[0] != '\0') {
		lv_snprintf(buf, sizeof(buf), "%.*f %s", s->precision, value, s->unit);
	} else {
		lv_snprintf(buf, sizeof(buf), "%.*f", s->precision, value);
	}
	lv_label_set_text(s->value_label, buf);
}