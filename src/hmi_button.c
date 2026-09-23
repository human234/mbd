#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include <hmi/hmi_button.h>
#include <hmi/hmi_gauge.h>

#define HMI_BUTTON_TRANSITION_MS 120
#define HMI_BUTTON_SCALE_NORMAL  256
#define HMI_BUTTON_SCALE_PRESSED 246

static const lv_style_prop_t g_trans_props[] = {
	LV_STYLE_BG_COLOR,
	LV_STYLE_BORDER_COLOR,
	LV_STYLE_TRANSFORM_SCALE_X,
	LV_STYLE_TRANSFORM_SCALE_Y,
	0
};
static lv_style_transition_dsc_t g_trans;
static bool g_trans_ready = false;
static hmi_button_t g_button[HMI_MAX_BUTTONS];

static hmi_button_t *button_find(lv_obj_t *btn);
static void button_click_cb(lv_event_t *e);
static void button_apply_styles(lv_obj_t *btn, int width, int height);

lv_obj_t *hmi_button_create(lv_obj_t *parent, int x, int y, int width, int height)
{
	if (width <= 0 || height <= 0) {
		return NULL;
	}

	lv_obj_t *btn = lv_button_create(parent);
	lv_obj_set_size(btn, width, height);
	lv_obj_set_pos(btn, x, y);

	lv_obj_t *icon = lv_label_create(btn);
	lv_obj_t *label = lv_label_create(btn);
	lv_obj_set_style_text_font(icon, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
	lv_obj_set_style_text_color(icon, lv_color_hex(colorscheme[22]), 0);
	lv_obj_set_style_text_color(label, lv_color_hex(colorscheme[22]), 0);
	lv_obj_set_style_text_color(icon, lv_color_white(), LV_STATE_CHECKED);
	lv_obj_set_style_text_color(label, lv_color_white(), LV_STATE_CHECKED);
	lv_obj_set_style_text_opa(icon, LV_OPA_40, LV_STATE_DISABLED);
	lv_obj_set_style_text_opa(label, LV_OPA_40, LV_STATE_DISABLED);
	lv_obj_add_flag(icon, LV_OBJ_FLAG_HIDDEN);

	button_apply_styles(btn, width, height);
	lv_obj_add_event_cb(btn, button_click_cb, LV_EVENT_CLICKED, NULL);

	for (int i = 0; i < HMI_MAX_BUTTONS; i++) {
		if (!g_button[i].used) {
			g_button[i].used = true;
			g_button[i].btn = btn;
			g_button[i].icon = icon;
			g_button[i].label = label;
			g_button[i].type = HMI_BUTTON_NORMAL;
			g_button[i].layout = HMI_BUTTON_ICON_LEFT;
			g_button[i].cb = NULL;
			g_button[i].user_data = NULL;
			break;
		}
	}

	return btn;
}

void hmi_button_set_label(lv_obj_t *btn, const char *text)
{
	hmi_button_t *b = button_find(btn);
	if (b == NULL) {
		return;
	}
	lv_label_set_text(b->label, text != NULL ? text : "");
}

void hmi_button_set_icon(lv_obj_t *btn, const char *symbol)
{
	hmi_button_t *b = button_find(btn);
	if (b == NULL) {
		return;
	}
	if (symbol != NULL && symbol[0] != '\0') {
		lv_label_set_text(b->icon, symbol);
		lv_obj_remove_flag(b->icon, LV_OBJ_FLAG_HIDDEN);
	} else {
		lv_obj_add_flag(b->icon, LV_OBJ_FLAG_HIDDEN);
	}
}

void hmi_button_set_layout(lv_obj_t *btn, hmi_button_layout_t layout)
{
	hmi_button_t *b = button_find(btn);
	if (b == NULL) {
		return;
	}
	b->layout = layout;
	if (layout == HMI_BUTTON_ICON_NONE) {
		lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
		lv_obj_set_style_pad_all(btn, 0, 0);
	} else if (layout == HMI_BUTTON_ICON_LEFT) {
		lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
		lv_obj_set_style_flex_main_place(btn, LV_FLEX_ALIGN_CENTER, 0);
		lv_obj_set_style_flex_cross_place(btn, LV_FLEX_ALIGN_CENTER, 0);
		lv_obj_set_style_flex_track_place(btn, LV_FLEX_ALIGN_CENTER, 0);
		lv_obj_set_style_pad_all(btn, 8, 0);
		lv_obj_set_style_pad_column(btn, 6, 0);
	} else {
		lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
		lv_obj_set_style_flex_main_place(btn, LV_FLEX_ALIGN_CENTER, 0);
		lv_obj_set_style_flex_cross_place(btn, LV_FLEX_ALIGN_CENTER, 0);
		lv_obj_set_style_flex_track_place(btn, LV_FLEX_ALIGN_CENTER, 0);
		lv_obj_set_style_pad_all(btn, 6, 0);
		lv_obj_set_style_pad_row(btn, 4, 0);
	}
}

void hmi_button_set_type(lv_obj_t *btn, hmi_button_type_t type)
{
	hmi_button_t *b = button_find(btn);
	if (b == NULL) {
		return;
	}
	b->type = type;
	if (type == HMI_BUTTON_TOGGLE) {
		lv_obj_add_flag(btn, LV_OBJ_FLAG_CHECKABLE);
	} else {
		lv_obj_remove_flag(btn, LV_OBJ_FLAG_CHECKABLE);
		lv_obj_remove_state(btn, LV_STATE_CHECKED);
	}
}

void hmi_button_set_checked(lv_obj_t *btn, bool checked)
{
	hmi_button_t *b = button_find(btn);
	if (b == NULL || b->type != HMI_BUTTON_TOGGLE) {
		return;
	}
	if (checked) {
		lv_obj_add_state(btn, LV_STATE_CHECKED);
	} else {
		lv_obj_remove_state(btn, LV_STATE_CHECKED);
	}
}

bool hmi_button_is_checked(lv_obj_t *btn)
{
	hmi_button_t *b = button_find(btn);
	if (b == NULL) {
		return false;
	}
	return lv_obj_has_state(btn, LV_STATE_CHECKED);
}

void hmi_button_set_callback(lv_obj_t *btn, hmi_button_cb_t cb, void *user_data)
{
	hmi_button_t *b = button_find(btn);
	if (b == NULL) {
		return;
	}
	b->cb = cb;
	b->user_data = user_data;
}

void hmi_button_set_enabled(lv_obj_t *btn, bool enabled)
{
	hmi_button_t *b = button_find(btn);
	if (b == NULL) {
		return;
	}
	if (enabled) {
		lv_obj_remove_state(btn, LV_STATE_DISABLED);
	} else {
		lv_obj_add_state(btn, LV_STATE_DISABLED);
	}
}

hmi_button_t *hmi_button_get(lv_obj_t *btn)
{
	return button_find(btn);
}

static hmi_button_t *button_find(lv_obj_t *btn)
{
	for (int i = 0; i < HMI_MAX_BUTTONS; i++) {
		if (g_button[i].used && g_button[i].btn == btn) {
			return &g_button[i];
		}
	}
	return NULL;
}

static void button_click_cb(lv_event_t *e)
{
	lv_obj_t *btn = lv_event_get_target_obj(e);
	hmi_button_t *b = button_find(btn);
	if (b == NULL || b->cb == NULL) {
		return;
	}
	bool checked = lv_obj_has_state(btn, LV_STATE_CHECKED);
	b->cb(btn, checked, b->user_data);
}

static void button_apply_styles(lv_obj_t *btn, int width, int height)
{
	if (!g_trans_ready) {
		lv_style_transition_dsc_init(&g_trans, g_trans_props, lv_anim_path_ease_out,
					     HMI_BUTTON_TRANSITION_MS, 0, NULL);
		g_trans_ready = true;
	}

	lv_obj_set_style_radius(btn, 0, 0);
	lv_obj_set_style_bg_color(btn, lv_color_hex(colorscheme[6]), 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(btn, 1, 0);
	lv_obj_set_style_border_color(btn, lv_color_hex(colorscheme[8]), 0);
	lv_obj_set_style_border_side(btn, LV_BORDER_SIDE_FULL, 0);
	lv_obj_set_style_transition(btn, &g_trans, 0);
	lv_obj_set_style_transform_scale_x(btn, HMI_BUTTON_SCALE_NORMAL, 0);
	lv_obj_set_style_transform_scale_y(btn, HMI_BUTTON_SCALE_NORMAL, 0);
	lv_obj_set_style_transform_pivot_x(btn, width / 2, 0);
	lv_obj_set_style_transform_pivot_y(btn, height / 2, 0);

	lv_obj_set_style_bg_color(btn, lv_color_hex(colorscheme[7]), LV_STATE_PRESSED);
	lv_obj_set_style_transform_scale_x(btn, HMI_BUTTON_SCALE_PRESSED, LV_STATE_PRESSED);
	lv_obj_set_style_transform_scale_y(btn, HMI_BUTTON_SCALE_PRESSED, LV_STATE_PRESSED);

	lv_obj_set_style_border_color(btn, lv_color_hex(colorscheme[15]), LV_STATE_FOCUSED);
	lv_obj_set_style_border_width(btn, 2, LV_STATE_FOCUSED);

	lv_obj_set_style_bg_color(btn, lv_color_hex(colorscheme[13]), LV_STATE_CHECKED);
	lv_obj_set_style_border_color(btn, lv_color_hex(colorscheme[13]), LV_STATE_CHECKED);
	lv_obj_set_style_border_width(btn, 1, LV_STATE_CHECKED);
	lv_obj_set_style_bg_color(btn, lv_color_hex(colorscheme[12]), LV_STATE_CHECKED | LV_STATE_PRESSED);

	lv_obj_set_style_bg_color(btn, lv_color_hex(colorscheme[10]), LV_STATE_DISABLED);
	lv_obj_set_style_border_color(btn, lv_color_hex(colorscheme[10]), LV_STATE_DISABLED);
	lv_obj_set_style_bg_opa(btn, LV_OPA_50, LV_STATE_DISABLED);
}