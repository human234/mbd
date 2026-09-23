#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include <hmi/hmi_tabs.h>
#include <hmi/hmi_gauge.h>

#define HMI_TABS_BAR_HEIGHT 40

static hmi_tabs_t g_tabs[HMI_MAX_TABS];

static hmi_tabs_t *tabs_find(lv_obj_t *tv);
static void tabs_style_bar(lv_obj_t *tv);
static void tabs_style_page(lv_obj_t *page);
static void tabs_value_changed_cb(lv_event_t *e);

lv_obj_t *hmi_tabs_create(lv_obj_t *parent, int x, int y, int width, int height)
{
	if (width <= 0 || height <= 0) {
		return NULL;
	}

	lv_obj_t *tv = lv_tabview_create(parent);
	lv_obj_set_pos(tv, x, y);
	lv_obj_set_size(tv, width, height);
	lv_obj_set_style_bg_color(tv, lv_color_hex(colorscheme[4]), 0);
	lv_obj_set_style_radius(tv, 0, 0);
	lv_obj_set_style_border_width(tv, 0, 0);
	lv_obj_set_style_pad_all(tv, 0, 0);

	lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tv);
	lv_obj_set_style_bg_color(tab_bar, lv_color_hex(colorscheme[4]), 0);
	lv_obj_set_style_radius(tab_bar, 0, 0);
	lv_obj_set_style_border_width(tab_bar, 0, 0);
	lv_obj_set_style_pad_all(tab_bar, 0, 0);
	lv_obj_set_style_pad_column(tab_bar, 0, 0);

	lv_obj_t *cont = lv_tabview_get_content(tv);
	lv_obj_set_style_bg_color(cont, lv_color_hex(colorscheme[0]), 0);
	lv_obj_set_style_radius(cont, 0, 0);
	lv_obj_set_style_border_width(cont, 0, 0);
	lv_obj_set_style_pad_all(cont, 0, 0);
	lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

	lv_tabview_set_tab_bar_size(tv, HMI_TABS_BAR_HEIGHT);
	lv_tabview_set_tab_bar_position(tv, LV_DIR_TOP);

	lv_obj_add_event_cb(tv, tabs_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

	hmi_tabs_t *t = NULL;
	for (int i = 0; i < HMI_MAX_TABS; i++) {
		if (!g_tabs[i].used) {
			t = &g_tabs[i];
			break;
		}
	}
	if (t) {
		t->used = true;
		t->tv = tv;
		t->cb = NULL;
		t->user_data = NULL;
	}

	return tv;
}

lv_obj_t *hmi_tabs_add_tab(lv_obj_t *tv, const char *name)
{
	lv_obj_t *page = lv_tabview_add_tab(tv, name != NULL ? name : "");
	tabs_style_bar(tv);
	tabs_style_page(page);
	return page;
}

void hmi_tabs_set_active(lv_obj_t *tv, uint32_t idx, bool animate)
{
	lv_tabview_set_active(tv, idx, animate ? LV_ANIM_ON : LV_ANIM_OFF);
}

uint32_t hmi_tabs_get_active(lv_obj_t *tv)
{
	return lv_tabview_get_tab_active(tv);
}

void hmi_tabs_rename(lv_obj_t *tv, uint32_t idx, const char *name)
{
	lv_tabview_set_tab_text(tv, idx, name);
}

void hmi_tabs_set_callback(lv_obj_t *tv, hmi_tabs_cb_t cb, void *user_data)
{
	hmi_tabs_t *t = tabs_find(tv);
	if (t == NULL) {
		return;
	}
	t->cb = cb;
	t->user_data = user_data;
}

hmi_tabs_t *hmi_tabs_get(lv_obj_t *tv)
{
	return tabs_find(tv);
}

static hmi_tabs_t *tabs_find(lv_obj_t *tv)
{
	for (int i = 0; i < HMI_MAX_TABS; i++) {
		if (g_tabs[i].used && g_tabs[i].tv == tv) {
			return &g_tabs[i];
		}
	}
	return NULL;
}

static void tabs_style_bar(lv_obj_t *tv)
{
	lv_obj_t *tab_bar = lv_tabview_get_tab_bar(tv);
	int i = 0;
	lv_obj_t *button = lv_obj_get_child_by_type(tab_bar, i, &lv_button_class);
	while (button) {
		lv_obj_set_style_radius(button, 0, 0);
		lv_obj_set_style_bg_color(button, lv_color_hex(colorscheme[6]), 0);
		lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(button, 0, 0);
		lv_obj_set_style_bg_color(button, lv_color_hex(colorscheme[13]), LV_STATE_CHECKED);
		lv_obj_set_style_bg_color(button, lv_color_hex(colorscheme[12]),
					  LV_STATE_CHECKED | LV_STATE_PRESSED);
		lv_obj_set_style_bg_color(button, lv_color_hex(colorscheme[7]), LV_STATE_PRESSED);
		lv_obj_set_style_pad_all(button, 0, 0);

		lv_obj_t *label = lv_obj_get_child_by_type(button, 0, &lv_label_class);
		if (label) {
			lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
			lv_obj_set_style_text_color(label, lv_color_hex(colorscheme[22]), 0);
			lv_obj_set_style_text_color(label, lv_color_white(), LV_STATE_CHECKED);
		}

		i++;
		button = lv_obj_get_child_by_type(tab_bar, i, &lv_button_class);
	}
}

static void tabs_style_page(lv_obj_t *page)
{
	lv_obj_set_style_bg_color(page, lv_color_hex(colorscheme[0]), 0);
	lv_obj_set_style_radius(page, 0, 0);
	lv_obj_set_style_border_width(page, 0, 0);
	lv_obj_set_style_pad_all(page, 0, 0);
	lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
}

static void tabs_value_changed_cb(lv_event_t *e)
{
	lv_obj_t *tv = lv_event_get_target_obj(e);
	hmi_tabs_t *t = tabs_find(tv);
	if (t == NULL || t->cb == NULL) {
		return;
	}
	t->cb(tv, lv_tabview_get_tab_active(tv), t->user_data);
}