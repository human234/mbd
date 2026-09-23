#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include <hmi/hmi_chart.h>
#include <hmi/hmi_gauge.h>

#define HMI_CHART_PAD 6
#define HMI_CHART_GRID_H 5
#define HMI_CHART_GRID_V 8
#define HMI_CHART_BORDER 1
#define HMI_CHART_HEADER_LEN 16
#define HMI_CHART_LEGEND_LEN 18
#define HMI_CHART_DEFAULT_PTS 60
#define HMI_CHART_DEFAULT_PERIOD 100
#define HMI_CHART_SWATCH_SIZE 10

static hmi_chart_t g_chart[HMI_MAX_CHARTS];

static hmi_chart_t *chart_find(lv_obj_t *chart);
static hmi_chart_t *chart_find_timer(lv_timer_t *timer);
static void chart_timer_cb(lv_timer_t *timer);
static lv_chart_axis_t chart_axis(hmi_chart_axis_t axis);

lv_obj_t *hmi_chart_create(lv_obj_t *parent, int x, int y, int width, int height,
			   const char *title)
{
	if (width <= 0 || height <= 0) {
		return NULL;
	}

	int pad = HMI_CHART_PAD + HMI_CHART_BORDER;

	lv_obj_t *panel = lv_obj_create(parent);
	lv_obj_set_pos(panel, x, y);
	lv_obj_set_size(panel, width, height);
	lv_obj_set_style_bg_color(panel, lv_color_hex(colorscheme[4]), 0);
	lv_obj_set_style_border_color(panel, lv_color_hex(colorscheme[8]), 0);
	lv_obj_set_style_border_width(panel, HMI_CHART_BORDER, 0);
	lv_obj_set_style_radius(panel, 0, 0);
	lv_obj_set_style_pad_all(panel, 0, 0);
	lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *title_label = lv_label_create(panel);
	lv_obj_set_pos(title_label, pad, pad);
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_12, 0);
	lv_obj_set_style_text_color(title_label, lv_color_hex(colorscheme[22]), 0);
	lv_label_set_text(title_label, title != NULL ? title : "");

	lv_obj_t *chart = lv_chart_create(panel);
	int chart_h = height - 2 * pad - HMI_CHART_HEADER_LEN - HMI_CHART_LEGEND_LEN;
	lv_obj_set_pos(chart, pad, pad + HMI_CHART_HEADER_LEN);
	lv_obj_set_size(chart, width - 2 * pad, LV_MAX(chart_h, 20));
	lv_chart_set_update_mode(chart, LV_CHART_UPDATE_MODE_SHIFT);
	lv_chart_set_point_count(chart, HMI_CHART_DEFAULT_PTS);
	lv_chart_set_div_line_count(chart, HMI_CHART_GRID_H, HMI_CHART_GRID_V);
	lv_chart_set_axis_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
	lv_chart_set_axis_range(chart, LV_CHART_AXIS_SECONDARY_Y, 0, 100);
	lv_obj_set_style_bg_color(chart, lv_color_hex(colorscheme[0]), 0);
	lv_obj_set_style_border_color(chart, lv_color_hex(colorscheme[8]), 0);
	lv_obj_set_style_border_width(chart, 1, 0);
	lv_obj_set_style_radius(chart, 0, 0);
	lv_obj_set_style_line_color(chart, lv_color_hex(colorscheme[9]), LV_PART_MAIN);
	lv_obj_set_style_radius(chart, 0, LV_PART_ITEMS);
	lv_obj_set_style_line_width(chart, 2, LV_PART_ITEMS);
	lv_obj_set_style_size(chart, 0, 0, LV_PART_ITEMS);

	lv_obj_t *legend = lv_obj_create(panel);
	lv_obj_set_pos(legend, pad, height - pad - HMI_CHART_LEGEND_LEN);
	lv_obj_set_size(legend, width - 2 * pad, HMI_CHART_LEGEND_LEN);
	lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(legend, 0, 0);
	lv_obj_set_style_pad_all(legend, 0, 0);
	lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_flex_main_place(legend, LV_FLEX_ALIGN_START, 0);
	lv_obj_set_style_flex_cross_place(legend, LV_FLEX_ALIGN_CENTER, 0);
	lv_obj_set_style_pad_column(legend, 10, 0);

	hmi_chart_t *c = NULL;
	for (int i = 0; i < HMI_MAX_CHARTS; i++) {
		if (!g_chart[i].used) {
			c = &g_chart[i];
			break;
		}
	}
	if (c) {
		c->used = true;
		c->panel = panel;
		c->title = title_label;
		c->chart = chart;
		c->legend = legend;
		c->series_count = 0;
		c->point_count = HMI_CHART_DEFAULT_PTS;
		c->timer = lv_timer_create(chart_timer_cb, HMI_CHART_DEFAULT_PERIOD, NULL);
		for (int s = 0; s < HMI_CHART_MAX_SERIES; s++) {
			c->series[s] = NULL;
			c->value[s] = NULL;
			c->name[s] = NULL;
			c->axis[s] = HMI_CHART_AXIS_LEFT;
			c->axis_min[s] = 0;
			c->axis_max[s] = 100;
		}
	}

	return chart;
}

lv_chart_series_t *hmi_chart_add_series(lv_obj_t *chart, uint32_t color_idx, const char *name,
					hmi_chart_axis_t axis)
{
	hmi_chart_t *c = chart_find(chart);
	if (c == NULL) {
		return NULL;
	}
	if (c->series_count >= HMI_CHART_MAX_SERIES) {
		return NULL;
	}

	lv_chart_series_t *ser = lv_chart_add_series(chart, lv_color_hex(colorscheme[color_idx]),
						     chart_axis(axis));
	uint32_t idx = c->series_count;
	c->series[idx] = ser;
	c->name[idx] = name;
	c->axis[idx] = axis;
	c->axis_min[idx] = 0;
	c->axis_max[idx] = 100;
	c->series_count++;

	lv_obj_t *swatch = lv_obj_create(c->legend);
	lv_obj_set_size(swatch, HMI_CHART_SWATCH_SIZE, HMI_CHART_SWATCH_SIZE);
	lv_obj_set_style_bg_color(swatch, lv_color_hex(colorscheme[color_idx]), 0);
	lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(swatch, 0, 0);
	lv_obj_set_style_radius(swatch, 0, 0);

	lv_obj_t *label = lv_label_create(c->legend);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
	lv_obj_set_style_text_color(label, lv_color_hex(colorscheme[23]), 0);
	lv_label_set_text(label, name != NULL ? name : "");

	return ser;
}

void hmi_chart_set_series_range(lv_obj_t *chart, uint32_t series_idx, int32_t min, int32_t max)
{
	hmi_chart_t *c = chart_find(chart);
	if (c == NULL || series_idx >= c->series_count) {
		return;
	}
	if (min > max) {
		int32_t t = min;
		min = max;
		max = t;
	}
	c->axis_min[series_idx] = min;
	c->axis_max[series_idx] = max;
	lv_chart_set_axis_range(chart, chart_axis(c->axis[series_idx]), min, max);
}

void hmi_chart_set_points(lv_obj_t *chart, uint32_t pts)
{
	hmi_chart_t *c = chart_find(chart);
	if (c == NULL || pts == 0) {
		return;
	}
	c->point_count = pts;
	lv_chart_set_point_count(chart, pts);
	lv_chart_refresh(chart);
}

void hmi_chart_set_period(lv_obj_t *chart, uint32_t ms)
{
	hmi_chart_t *c = chart_find(chart);
	if (c == NULL || c->timer == NULL) {
		return;
	}
	lv_timer_set_period(c->timer, ms);
}

void hmi_chart_bind_value(lv_obj_t *chart, uint32_t series_idx, float *value)
{
	hmi_chart_t *c = chart_find(chart);
	if (c == NULL || series_idx >= c->series_count) {
		return;
	}
	c->value[series_idx] = value;
}

void hmi_chart_push_value(lv_obj_t *chart, uint32_t series_idx, int32_t value)
{
	hmi_chart_t *c = chart_find(chart);
	if (c == NULL || series_idx >= c->series_count) {
		return;
	}
	if (value < c->axis_min[series_idx]) {
		value = c->axis_min[series_idx];
	}
	if (value > c->axis_max[series_idx]) {
		value = c->axis_max[series_idx];
	}
	lv_chart_set_next_value(chart, c->series[series_idx], value);
}

hmi_chart_t *hmi_chart_get(lv_obj_t *chart)
{
	return chart_find(chart);
}

static hmi_chart_t *chart_find(lv_obj_t *chart)
{
	for (int i = 0; i < HMI_MAX_CHARTS; i++) {
		if (g_chart[i].used && g_chart[i].chart == chart) {
			return &g_chart[i];
		}
	}
	return NULL;
}

static hmi_chart_t *chart_find_timer(lv_timer_t *timer)
{
	for (int i = 0; i < HMI_MAX_CHARTS; i++) {
		if (g_chart[i].used && g_chart[i].timer == timer) {
			return &g_chart[i];
		}
	}
	return NULL;
}

static void chart_timer_cb(lv_timer_t *timer)
{
	hmi_chart_t *c = chart_find_timer(timer);
	if (c == NULL || c->series_count == 0) {
		return;
	}
	for (uint32_t s = 0; s < c->series_count; s++) {
		if (c->series[s] == NULL || c->value[s] == NULL) {
			continue;
		}
		int32_t v = (int32_t)(*c->value[s]);
		if (v < c->axis_min[s]) {
			v = c->axis_min[s];
		}
		if (v > c->axis_max[s]) {
			v = c->axis_max[s];
		}
		lv_chart_set_next_value(c->chart, c->series[s], v);
	}
}

static lv_chart_axis_t chart_axis(hmi_chart_axis_t axis)
{
	if (axis == HMI_CHART_AXIS_RIGHT) {
		return LV_CHART_AXIS_SECONDARY_Y;
	}
	return LV_CHART_AXIS_PRIMARY_Y;
}