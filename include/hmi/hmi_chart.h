#ifndef HMI_CHART_H
#define HMI_CHART_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#define HMI_MAX_CHARTS       4
#define HMI_CHART_MAX_SERIES 4
#define HMI_CHART_NAME_LEN   16

typedef enum {
	HMI_CHART_AXIS_LEFT,   /* LV_CHART_AXIS_PRIMARY_Y   */
	HMI_CHART_AXIS_RIGHT   /* LV_CHART_AXIS_SECONDARY_Y */
} hmi_chart_axis_t;

typedef struct {
	lv_obj_t *panel;
	lv_obj_t *title;
	lv_obj_t *chart;
	lv_obj_t *legend;
	lv_chart_series_t *series[HMI_CHART_MAX_SERIES];
	float *value[HMI_CHART_MAX_SERIES];
	const char *name[HMI_CHART_MAX_SERIES];
	hmi_chart_axis_t axis[HMI_CHART_MAX_SERIES];
	int32_t axis_min[HMI_CHART_MAX_SERIES];
	int32_t axis_max[HMI_CHART_MAX_SERIES];
	uint32_t series_count;
	uint32_t point_count;
	lv_timer_t *timer;
	bool used;
} hmi_chart_t;

lv_obj_t *hmi_chart_create(lv_obj_t *parent, int x, int y, int width, int height,
			   const char *title);
lv_chart_series_t *hmi_chart_add_series(lv_obj_t *chart, uint32_t color_idx,
					const char *name, hmi_chart_axis_t axis);
void hmi_chart_set_series_range(lv_obj_t *chart, uint32_t series_idx, int32_t min,
				int32_t max);
void hmi_chart_set_points(lv_obj_t *chart, uint32_t pts);
void hmi_chart_set_period(lv_obj_t *chart, uint32_t ms);
void hmi_chart_bind_value(lv_obj_t *chart, uint32_t series_idx, float *value);
void hmi_chart_push_value(lv_obj_t *chart, uint32_t series_idx, int32_t value);
hmi_chart_t *hmi_chart_get(lv_obj_t *chart);

#endif /* HMI_CHART_H */