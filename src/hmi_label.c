#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include <hmi/hmi_gauge.h>
#include <hmi/hmi_label.h>

#define HMI_READOUT_PAD       4
#define HMI_READOUT_PERIOD_MS 100

static hmi_readout_t g_readout[HMI_MAX_READOUTS];
static lv_timer_t *g_readout_timer;

static hmi_readout_t *readout_find(lv_obj_t *ro);
static void readout_timer_cb(lv_timer_t *timer);
static void readout_refresh(hmi_readout_t *r);
static uint32_t readout_state_color(hmi_readout_state_t state);
static const lv_font_t *readout_font(int width);

lv_obj_t *hmi_readout_create(lv_obj_t *parent, int x, int y, int width,
			     const char *title, const char *unit)
{
	if (width <= 0) {
		return NULL;
	}

	hmi_readout_t *r = NULL;
	for (int i = 0; i < HMI_MAX_READOUTS; i++) {
		if (!g_readout[i].used) {
			r = &g_readout[i];
			break;
		}
	}
	if (r == NULL) {
		return NULL;
	}

	const lv_font_t *title_font = &lv_font_montserrat_12;
	const lv_font_t *value_font = readout_font(width);
	int title_h = lv_font_get_line_height(title_font);
	int height = 2 * HMI_READOUT_PAD + title_h + lv_font_get_line_height(value_font);

	lv_obj_t *panel = lv_obj_create(parent);
	lv_obj_set_pos(panel, x, y);
	lv_obj_set_size(panel, width, height);
	lv_obj_set_style_bg_color(panel, lv_color_hex(colorscheme[4]), 0);
	lv_obj_set_style_border_color(panel, lv_color_hex(colorscheme[8]), 0);
	lv_obj_set_style_border_width(panel, 1, 0);
	lv_obj_set_style_radius(panel, 0, 0);
	lv_obj_set_style_pad_all(panel, 0, 0);
	lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *title_label = lv_label_create(panel);
	lv_obj_set_pos(title_label, HMI_READOUT_PAD, HMI_READOUT_PAD);
	lv_obj_set_width(title_label, width - 2 * HMI_READOUT_PAD);
	lv_obj_set_style_text_font(title_label, title_font, 0);
	lv_obj_set_style_text_color(title_label, lv_color_hex(colorscheme[23]), 0);
	lv_label_set_long_mode(title_label, LV_LABEL_LONG_DOT);
	lv_label_set_text(title_label, title != NULL ? title : "");

	lv_obj_t *value_label = lv_label_create(panel);
	lv_obj_set_pos(value_label, HMI_READOUT_PAD, HMI_READOUT_PAD + title_h);
	lv_obj_set_width(value_label, width - 2 * HMI_READOUT_PAD);
	lv_obj_set_style_text_font(value_label, value_font, 0);
	lv_obj_set_style_text_color(value_label, lv_color_hex(colorscheme[22]), 0);
	lv_label_set_long_mode(value_label, LV_LABEL_LONG_DOT);
	lv_label_set_text(value_label, "--");

	r->used = true;
	r->panel = panel;
	r->title = title_label;
	r->value = value_label;
	r->bound = NULL;
	r->unit = unit;
	r->precision = 1;
	r->state = HMI_READOUT_NEUTRAL;

	return panel;
}

void hmi_readout_bind_value(lv_obj_t *ro, float *value)
{
	hmi_readout_t *r = readout_find(ro);
	if (r == NULL) {
		return;
	}
	r->bound = value;
	if (g_readout_timer == NULL) {
		g_readout_timer = lv_timer_create(readout_timer_cb, HMI_READOUT_PERIOD_MS, NULL);
	}
	readout_refresh(r);
}

void hmi_readout_set_precision(lv_obj_t *ro, int decimals)
{
	hmi_readout_t *r = readout_find(ro);
	if (r == NULL) {
		return;
	}
	if (decimals < 0) {
		decimals = 0;
	}
	if (decimals > 6) {
		decimals = 6;
	}
	r->precision = decimals;
	readout_refresh(r);
}

void hmi_readout_set_text(lv_obj_t *ro, const char *text)
{
	hmi_readout_t *r = readout_find(ro);
	if (r == NULL) {
		return;
	}
	/* A bound readout is re-formatted on the next timer tick and will
	 * overwrite this; set_text() is for readouts left unbound.
	 */
	lv_label_set_text(r->value, text != NULL ? text : "");
}

void hmi_readout_set_state(lv_obj_t *ro, hmi_readout_state_t state)
{
	hmi_readout_t *r = readout_find(ro);
	if (r == NULL) {
		return;
	}
	r->state = state;
	lv_obj_set_style_text_color(r->value, lv_color_hex(readout_state_color(state)), 0);
}

hmi_readout_t *hmi_readout_get(lv_obj_t *ro)
{
	return readout_find(ro);
}

static hmi_readout_t *readout_find(lv_obj_t *ro)
{
	for (int i = 0; i < HMI_MAX_READOUTS; i++) {
		if (g_readout[i].used && g_readout[i].panel == ro) {
			return &g_readout[i];
		}
	}
	return NULL;
}

static void readout_timer_cb(lv_timer_t *timer)
{
	LV_UNUSED(timer);
	for (int i = 0; i < HMI_MAX_READOUTS; i++) {
		if (g_readout[i].used && g_readout[i].bound != NULL) {
			readout_refresh(&g_readout[i]);
		}
	}
}

static void readout_refresh(hmi_readout_t *r)
{
	if (r->bound == NULL) {
		return;
	}

	char buf[32];
	if (r->unit != NULL && r->unit[0] != '\0') {
		lv_snprintf(buf, sizeof(buf), "%.*f %s", r->precision, (double)*r->bound, r->unit);
	} else {
		lv_snprintf(buf, sizeof(buf), "%.*f", r->precision, (double)*r->bound);
	}
	lv_label_set_text(r->value, buf);
}

static uint32_t readout_state_color(hmi_readout_state_t state)
{
	switch (state) {
	case HMI_READOUT_OK:
		return colorscheme[17];
	case HMI_READOUT_WARN:
		return colorscheme[18];
	case HMI_READOUT_FAULT:
		return colorscheme[19];
	case HMI_READOUT_NEUTRAL:
	default:
		return colorscheme[22];
	}
}

static const lv_font_t *readout_font(int width)
{
	if (width >= 200) {
		return &lv_font_montserrat_32;
	}
	if (width >= 150) {
		return &lv_font_montserrat_28;
	}
	if (width >= 110) {
		return &lv_font_montserrat_22;
	}
	if (width >= 80) {
		return &lv_font_montserrat_18;
	}
	return &lv_font_montserrat_16;
}
