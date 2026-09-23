#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include <hmi/hmi_polar.h>
#include <hmi/hmi_gauge.h>

#define HMI_POLAR_PAD 6
#define HMI_POLAR_BORDER 1
#define HMI_POLAR_HEADER_LEN 16
#define HMI_POLAR_LEGEND_LEN 16
#define HMI_POLAR_LABEL_SPACE 16
#define HMI_POLAR_SWATCH_SIZE 10
#define HMI_POLAR_SPOKES 12
#define HMI_POLAR_VECTOR_W 2
#define HMI_POLAR_DOT_R 3
#define HMI_POLAR_DEFAULT_PERIOD 100
#define HMI_POLAR_DEFAULT_MAX 100.0f
#define HMI_POLAR_PI 3.14159265f

static hmi_polar_t g_polar[HMI_MAX_POLARS];

static hmi_polar_t *polar_find(lv_obj_t *obj);
static hmi_polar_t *polar_find_timer(lv_timer_t *timer);
static void polar_timer_cb(lv_timer_t *timer);
static void polar_draw_cb(lv_event_t *e);
static void polar_geometry(const hmi_polar_t *p, int32_t *cx, int32_t *cy, int32_t *R);
static void polar_vector_rt(const hmi_polar_t *p, uint32_t v, float *r, float *deg);
static void polar_update_labels(hmi_polar_t *p);
static void polar_layout_labels(hmi_polar_t *p);

lv_obj_t *hmi_polar_create(lv_obj_t *parent, int x, int y, int size, const char *title)
{
	if (size <= 0) {
		return NULL;
	}

	int pad = HMI_POLAR_PAD + HMI_POLAR_BORDER;

	lv_obj_t *panel = lv_obj_create(parent);
	lv_obj_set_pos(panel, x, y);
	lv_obj_set_size(panel, size, size);
	lv_obj_set_style_bg_color(panel, lv_color_hex(colorscheme[4]), 0);
	lv_obj_set_style_border_color(panel, lv_color_hex(colorscheme[8]), 0);
	lv_obj_set_style_border_width(panel, HMI_POLAR_BORDER, 0);
	lv_obj_set_style_radius(panel, 0, 0);
	lv_obj_set_style_pad_all(panel, 0, 0);
	lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

	lv_obj_t *title_label = lv_label_create(panel);
	lv_obj_set_pos(title_label, pad, pad);
	lv_obj_set_style_text_font(title_label, &lv_font_montserrat_12, 0);
	lv_obj_set_style_text_color(title_label, lv_color_hex(colorscheme[22]), 0);
	lv_label_set_text(title_label, title != NULL ? title : "");

	lv_obj_t *legend = lv_obj_create(panel);
	lv_obj_set_pos(legend, pad, size - pad - HMI_POLAR_LEGEND_LEN);
	lv_obj_set_size(legend, size - 2 * pad, HMI_POLAR_LEGEND_LEN);
	lv_obj_set_style_bg_opa(legend, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(legend, 0, 0);
	lv_obj_set_style_pad_all(legend, 0, 0);
	lv_obj_set_flex_flow(legend, LV_FLEX_FLOW_ROW);
	lv_obj_set_style_flex_main_place(legend, LV_FLEX_ALIGN_START, 0);
	lv_obj_set_style_flex_cross_place(legend, LV_FLEX_ALIGN_CENTER, 0);
	lv_obj_set_style_pad_column(legend, 10, 0);

	hmi_polar_t *p = NULL;
	for (int i = 0; i < HMI_MAX_POLARS; i++) {
		if (!g_polar[i].used) {
			p = &g_polar[i];
			break;
		}
	}
	if (p) {
		p->used = true;
		p->panel = panel;
		p->title = title_label;
		p->legend = legend;
		p->vector_count = 0;
		p->mag_max = HMI_POLAR_DEFAULT_MAX;
		for (uint32_t v = 0; v < HMI_POLAR_MAX_VECTORS; v++) {
			p->color_idx[v] = 0;
			p->name[v] = NULL;
			p->mode[v] = HMI_POLAR_MODE_POLAR;
			p->own_mag[v] = 0.0f;
			p->own_angle[v] = 0.0f;
			p->own_d[v] = 0.0f;
			p->own_q[v] = 0.0f;
			p->mag[v] = &p->own_mag[v];
			p->angle[v] = &p->own_angle[v];
			p->d[v] = &p->own_d[v];
			p->q[v] = &p->own_q[v];
		}
		p->timer = lv_timer_create(polar_timer_cb, HMI_POLAR_DEFAULT_PERIOD, NULL);
		lv_obj_add_event_cb(panel, polar_draw_cb, LV_EVENT_DRAW_MAIN, p);

		for (int k = 0; k < HMI_POLAR_RINGS; k++) {
			lv_obj_t *lbl = lv_label_create(panel);
			lv_obj_set_width(lbl, 40);
			lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
			lv_obj_set_style_text_color(lbl, lv_color_hex(colorscheme[22]), 0);
			lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
			lv_label_set_text(lbl, "");
			p->ring_label[k] = lbl;
		}
		lv_obj_update_layout(panel);
		polar_update_labels(p);
		polar_layout_labels(p);
	}

	return panel;
}

uint32_t hmi_polar_add_vector(lv_obj_t *polar, uint32_t color_idx, const char *name,
			      hmi_polar_mode_t mode)
{
	hmi_polar_t *p = polar_find(polar);
	if (p == NULL) {
		return UINT32_MAX;
	}
	if (p->vector_count >= HMI_POLAR_MAX_VECTORS) {
		return UINT32_MAX;
	}

	uint32_t idx = p->vector_count;
	p->color_idx[idx] = color_idx;
	p->name[idx] = name;
	p->mode[idx] = mode;
	p->vector_count++;

	lv_obj_t *swatch = lv_obj_create(p->legend);
	lv_obj_set_size(swatch, HMI_POLAR_SWATCH_SIZE, HMI_POLAR_SWATCH_SIZE);
	lv_obj_set_style_bg_color(swatch, lv_color_hex(colorscheme[color_idx]), 0);
	lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(swatch, 0, 0);
	lv_obj_set_style_radius(swatch, 0, 0);

	lv_obj_t *label = lv_label_create(p->legend);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
	lv_obj_set_style_text_color(label, lv_color_hex(colorscheme[23]), 0);
	lv_label_set_text(label, name != NULL ? name : "");

	return idx;
}

void hmi_polar_set_magnitude_range(lv_obj_t *polar, float max)
{
	hmi_polar_t *p = polar_find(polar);
	if (p == NULL || max <= 0.0f) {
		return;
	}
	p->mag_max = max;
	polar_update_labels(p);
}

void hmi_polar_bind_polar(lv_obj_t *polar, uint32_t vec, float *mag, float *angle_deg)
{
	hmi_polar_t *p = polar_find(polar);
	if (p == NULL || vec >= p->vector_count) {
		return;
	}
	p->mode[vec] = HMI_POLAR_MODE_POLAR;
	if (mag) {
		p->mag[vec] = mag;
	}
	if (angle_deg) {
		p->angle[vec] = angle_deg;
	}
}

void hmi_polar_bind_dq(lv_obj_t *polar, uint32_t vec, float *d, float *q)
{
	hmi_polar_t *p = polar_find(polar);
	if (p == NULL || vec >= p->vector_count) {
		return;
	}
	p->mode[vec] = HMI_POLAR_MODE_DQ;
	if (d) {
		p->d[vec] = d;
	}
	if (q) {
		p->q[vec] = q;
	}
}

void hmi_polar_set_vector(lv_obj_t *polar, uint32_t vec, float mag, float angle_deg)
{
	hmi_polar_t *p = polar_find(polar);
	if (p == NULL || vec >= p->vector_count) {
		return;
	}
	p->mode[vec] = HMI_POLAR_MODE_POLAR;
	*p->mag[vec] = mag;
	*p->angle[vec] = angle_deg;
}

void hmi_polar_set_vector_dq(lv_obj_t *polar, uint32_t vec, float d, float q)
{
	hmi_polar_t *p = polar_find(polar);
	if (p == NULL || vec >= p->vector_count) {
		return;
	}
	p->mode[vec] = HMI_POLAR_MODE_DQ;
	*p->d[vec] = d;
	*p->q[vec] = q;
}

hmi_polar_t *hmi_polar_get(lv_obj_t *polar)
{
	return polar_find(polar);
}

static hmi_polar_t *polar_find(lv_obj_t *obj)
{
	for (int i = 0; i < HMI_MAX_POLARS; i++) {
		if (g_polar[i].used && g_polar[i].panel == obj) {
			return &g_polar[i];
		}
	}
	return NULL;
}

static hmi_polar_t *polar_find_timer(lv_timer_t *timer)
{
	for (int i = 0; i < HMI_MAX_POLARS; i++) {
		if (g_polar[i].used && g_polar[i].timer == timer) {
			return &g_polar[i];
		}
	}
	return NULL;
}

static void polar_timer_cb(lv_timer_t *timer)
{
	hmi_polar_t *p = polar_find_timer(timer);
	if (p == NULL) {
		return;
	}
	lv_obj_invalidate(p->panel);
}

static void polar_geometry(const hmi_polar_t *p, int32_t *cx, int32_t *cy, int32_t *R)
{
	int32_t w = lv_obj_get_width(p->panel);
	int32_t h = lv_obj_get_height(p->panel);
	int32_t pad = HMI_POLAR_PAD + HMI_POLAR_BORDER;
	int32_t avail = h - 2 * pad - HMI_POLAR_HEADER_LEN - HMI_POLAR_LEGEND_LEN;
	if (avail < 20) {
		avail = 20;
	}
	int32_t usable = avail - HMI_POLAR_LABEL_SPACE;
	if (usable < 20) {
		usable = 20;
	}
	*cx = w / 2;
	*cy = pad + HMI_POLAR_HEADER_LEN + HMI_POLAR_LABEL_SPACE + usable / 2;
	*R = usable / 2 - 2;
	if (*R < 20) {
		*R = 20;
	}
}

static void polar_vector_rt(const hmi_polar_t *p, uint32_t v, float *r, float *deg)
{
	if (p->mode[v] == HMI_POLAR_MODE_DQ) {
		float d = *p->d[v];
		float q = *p->q[v];
		*r = sqrtf(d * d + q * q);
		*deg = atan2f(q, d) * 180.0f / HMI_POLAR_PI;
	} else {
		*r = *p->mag[v];
		*deg = *p->angle[v];
	}
	if (*r < 0.0f) {
		*r = 0.0f;
	}
}

static void polar_update_labels(hmi_polar_t *p)
{
	for (int k = 0; k < HMI_POLAR_RINGS; k++) {
		float v = p->mag_max * (k + 1) / HMI_POLAR_RINGS;
		lv_label_set_text_fmt(p->ring_label[k], "%g", (double)v);
	}
}

static void polar_layout_labels(hmi_polar_t *p)
{
	int32_t cx, cy, R;
	polar_geometry(p, &cx, &cy, &R);
	if (R <= 0) {
		return;
	}
	for (int k = 0; k < HMI_POLAR_RINGS; k++) {
		int32_t r = R * (k + 1) / HMI_POLAR_RINGS;
		int32_t x = cx - 20;
		int32_t y = cy - r - 10;
		lv_obj_set_pos(p->ring_label[k], x, y);
	}
}

static void polar_draw_cb(lv_event_t *e)
{
	hmi_polar_t *p = lv_event_get_user_data(e);
	if (p == NULL) {
		return;
	}
	lv_layer_t *layer = lv_event_get_layer(e);

	int32_t cx, cy, R;
	polar_geometry(p, &cx, &cy, &R);
	if (R <= 0) {
		return;
	}

	lv_area_t coords;
	lv_obj_get_coords(p->panel, &coords);
	cx += coords.x1;
	cy += coords.y1;

	/* Concentric magnitude rings */
	lv_draw_arc_dsc_t arc;
	lv_draw_arc_dsc_init(&arc);
	arc.opa = LV_OPA_COVER;
	arc.color = lv_color_hex(colorscheme[14]);
	arc.width = 1;
	arc.start_angle = 0;
	arc.end_angle = 360;

	arc.center.x = cx;
	arc.center.y = cy;
	for (int k = 1; k <= HMI_POLAR_RINGS; k++) {
		arc.radius = (uint16_t)(R * k / HMI_POLAR_RINGS);
		lv_draw_arc(layer, &arc);
	}

	/* Phase dividing lines (spokes). Principal d/q axes at
	 * 0/90/180/270 deg are highlighted */
	lv_draw_line_dsc_t line;
	lv_draw_line_dsc_init(&line);
	line.opa = LV_OPA_COVER;
	line.width = 1;
	line.round_start = 0;
	line.round_end = 0;
	line.p1.x = cx;
	line.p1.y = cy;
	for (int k = 0; k < HMI_POLAR_SPOKES; k++) {
		float a = HMI_POLAR_PI * k * 2.0f / HMI_POLAR_SPOKES;
		line.p2.x = cx + (int32_t)(R * cosf(a));
		line.p2.y = cy - (int32_t)(R * sinf(a));
		if ((k % (HMI_POLAR_SPOKES / 4)) == 0) {
			line.color = lv_color_hex(colorscheme[15]);
		} else {
			line.color = lv_color_hex(colorscheme[14]);
		}
		lv_draw_line(layer, &line);
	}

	float max = p->mag_max > 0.0f ? p->mag_max : 1.0f;
	for (uint32_t v = 0; v < p->vector_count; v++) {
		float r, deg;
		polar_vector_rt(p, v, &r, &deg);
		if (r > max) {
			r = max;
		}
		float rr = (float)R * r / max;
		float a = deg * HMI_POLAR_PI / 180.0f;
		int32_t px = cx + (int32_t)(rr * cosf(a));
		int32_t py = cy - (int32_t)(rr * sinf(a));

		lv_draw_line_dsc_t vline;
		lv_draw_line_dsc_init(&vline);
		vline.opa = LV_OPA_COVER;
		vline.color = lv_color_hex(colorscheme[p->color_idx[v]]);
		vline.width = HMI_POLAR_VECTOR_W;
		vline.p1.x = cx;
		vline.p1.y = cy;
		vline.p2.x = px;
		vline.p2.y = py;
		lv_draw_line(layer, &vline);

		lv_draw_arc_dsc_t dot;
		lv_draw_arc_dsc_init(&dot);
		dot.opa = LV_OPA_COVER;
		dot.color = lv_color_hex(colorscheme[p->color_idx[v]]);
		dot.width = HMI_POLAR_DOT_R;
		dot.radius = HMI_POLAR_DOT_R;
		dot.start_angle = 0;
		dot.end_angle = 360;
		dot.center.x = px;
		dot.center.y = py;
		lv_draw_arc(layer, &dot);
	}
}