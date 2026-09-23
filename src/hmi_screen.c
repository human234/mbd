#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include <hmi/hmi.h>

static float demo_speed = 0.0f;
static float demo_current = 4.0f;
static float demo_temp = 40.0f;
static float demo_dir = 1.0f;
static float demo_phase = 0.0f;
static float demo_angle = 0.0f;
static float demo_ismag = 20.0f;
static float demo_d = 0.0f;
static float demo_q = 0.0f;

static void demo_sim_cb(lv_timer_t *timer)
{
	LV_UNUSED(timer);
	demo_speed += 30.0f;
	if (demo_speed > 1500.0f) {
		demo_speed = 0.0f;
	}

	demo_current += demo_dir * 1.2f;
	if (demo_current > 28.0f || demo_current < 4.0f) {
		demo_dir = -demo_dir;
	}

	demo_temp = 40.0f + demo_speed / 1500.0f * 30.0f;

	demo_phase += 0.05f;
	demo_angle += 7.0f;
	if (demo_angle >= 360.0f) {
		demo_angle -= 360.0f;
	}
	demo_ismag = 25.0f + demo_speed / 1500.0f * 60.0f;
	demo_d = 28.0f * cosf(demo_phase);
	demo_q = 28.0f * sinf(demo_phase);
}

static void tab_changed_cb(lv_obj_t *tv, uint32_t active, void *user_data)
{
	LV_UNUSED(tv);
	LV_UNUSED(user_data);
	LV_LOG_USER("tab changed to %d", (int)active);
}

int hmi_show_ui(lv_obj_t *parent)
{
	lv_obj_t *tv = hmi_tabs_create(parent, 0, 0, 320, 480);
	hmi_tabs_set_callback(tv, tab_changed_cb, NULL);

	lv_obj_t *page = hmi_tabs_add_tab(tv, "Gauges");
	hmi_ui_gauge_create(page, 100, "Speed", "km/h");
	lv_obj_t *btn = hmi_button_create(page, 0, 100, 320, 40);
	hmi_button_set_label(btn, "Start Pump");
	hmi_button_set_icon(btn, LV_SYMBOL_PLAY);
	hmi_button_set_layout(btn, HMI_BUTTON_ICON_LEFT);
	hmi_button_set_type(btn, HMI_BUTTON_TOGGLE);
	hmi_button_set_callback(btn, NULL, NULL);

	btn = hmi_button_create(page, 0, 150, 150, 80);
	hmi_button_set_label(btn, "STOP");
	hmi_button_set_icon(btn, LV_SYMBOL_STOP);
	hmi_button_set_layout(btn, HMI_BUTTON_ICON_TOP);

	btn = hmi_button_create(page, 160, 150, 80, 80);
	hmi_button_set_icon(btn, LV_SYMBOL_POWER);
	hmi_button_set_layout(btn, HMI_BUTTON_ICON_NONE);
	hmi_button_set_type(btn, HMI_BUTTON_TOGGLE);

	btn = hmi_button_create(page, 0, 240, 320, 40);
	hmi_button_set_label(btn, "Menu");
	hmi_button_set_icon(btn, LV_SYMBOL_SETTINGS);
	hmi_button_set_layout(btn, HMI_BUTTON_ICON_LEFT);
	hmi_button_set_callback(btn, NULL, NULL);

	page = hmi_tabs_add_tab(tv, "Controls");
	lv_obj_t *s = hmi_slider_create(page, 10, 10, 300);
	hmi_slider_set_range(s, 0.0, 10.0, 0.5);
	hmi_slider_set_unit(s, "rpm");
	hmi_slider_set_precision(s, 1);
	hmi_slider_set_value(s, 2.5);

	s = hmi_slider_create(page, 10, 70, 300);
	hmi_slider_set_range(s, -20.0, 60.0, 5.0);
	hmi_slider_set_unit(s, "C");
	hmi_slider_set_precision(s, 0);

	page = hmi_tabs_add_tab(tv, "Status");
	lv_obj_t *lbl = lv_label_create(page);
	lv_label_set_text(lbl, "All systems nominal");
	lv_obj_set_style_text_color(lbl, lv_color_hex(colorscheme[22]), 0);
	lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
	lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

	page = hmi_tabs_add_tab(tv, "Trend");
	lv_obj_t *chart = hmi_chart_create(page, 10, 10, 300, 200, "Motor telemetry");
	hmi_chart_add_series(chart, 13, "Speed", HMI_CHART_AXIS_LEFT);
	hmi_chart_add_series(chart, 16, "Current", HMI_CHART_AXIS_RIGHT);
	hmi_chart_add_series(chart, 18, "Temp", HMI_CHART_AXIS_RIGHT);
	hmi_chart_set_series_range(chart, 0, 0, 1500);
	hmi_chart_set_series_range(chart, 1, 0, 30);
	hmi_chart_set_series_range(chart, 2, 0, 80);
	hmi_chart_bind_value(chart, 0, &demo_speed);
	hmi_chart_bind_value(chart, 1, &demo_current);
	hmi_chart_bind_value(chart, 2, &demo_temp);

	page = hmi_tabs_add_tab(tv, "Vector");
	lv_obj_t *polar = hmi_polar_create(page, 10, 30, 220, "Vector plane");
	hmi_polar_set_magnitude_range(polar, 100.0f);
	uint32_t v_norm = hmi_polar_add_vector(polar, 13, "Is", HMI_POLAR_MODE_POLAR);
	uint32_t v_dq = hmi_polar_add_vector(polar, 16, "Idq", HMI_POLAR_MODE_DQ);
	hmi_polar_bind_polar(polar, v_norm, &demo_ismag, &demo_angle);
	hmi_polar_bind_dq(polar, v_dq, &demo_d, &demo_q);

	lv_timer_create(demo_sim_cb, 100, NULL);

	return 0;
}