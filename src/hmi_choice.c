#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>

#include <hmi/hmi_choice.h>
#include <hmi/hmi_gauge.h>

#define HMI_CHOICE_CTRL_BASE (LV_BUTTONMATRIX_CTRL_CHECKABLE | \
			      LV_BUTTONMATRIX_CTRL_CLICK_TRIG)

static hmi_choice_t g_choice[HMI_MAX_CHOICES];

static hmi_choice_t *choice_find(lv_obj_t *ch);
static void choice_sync_ctrl(hmi_choice_t *c);
static void choice_value_changed_cb(lv_event_t *e);

lv_obj_t *hmi_choice_create(lv_obj_t *parent, int x, int y, int width, int height)
{
	if (width <= 0 || height <= 0) {
		return NULL;
	}

	hmi_choice_t *c = NULL;
	for (int i = 0; i < HMI_MAX_CHOICES; i++) {
		if (!g_choice[i].used) {
			c = &g_choice[i];
			break;
		}
	}
	if (c == NULL) {
		return NULL;
	}

	c->used = true;
	c->obj = NULL;
	c->option_count = 0;
	c->active = UINT32_MAX;
	c->cb = NULL;
	c->user_data = NULL;
	/* LV_WIDGETS_HAS_DEFAULT_VALUE gives a fresh buttonmatrix a "Btn1..
	 * Btn5" map. One blank button replaces it and is swapped for the
	 * first real option; an all-empty map would make LVGL malloc(0).
	 */
	c->map[0] = " ";
	c->map[1] = NULL;

	lv_obj_t *obj = lv_buttonmatrix_create(parent);
	lv_obj_set_pos(obj, x, y);
	lv_obj_set_size(obj, width, height);
	lv_obj_set_style_radius(obj, 0, 0);
	lv_obj_set_style_border_width(obj, 1, 0);
	lv_obj_set_style_border_color(obj, lv_color_hex(colorscheme[8]), 0);
	lv_obj_set_style_bg_color(obj, lv_color_hex(colorscheme[6]), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(obj, 0, 0);
	lv_obj_set_style_pad_column(obj, 2, 0);
	lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_buttonmatrix_set_one_checked(obj, true);

	lv_obj_set_style_text_font(obj, &lv_font_montserrat_16, LV_PART_ITEMS);
	lv_obj_set_style_text_color(obj, lv_color_hex(colorscheme[22]), LV_PART_ITEMS);
	lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_bg_color(obj, lv_color_hex(colorscheme[6]), LV_PART_ITEMS);
	lv_obj_set_style_bg_color(obj, lv_color_hex(colorscheme[7]), LV_PART_ITEMS | LV_STATE_PRESSED);
	lv_obj_set_style_bg_color(obj, lv_color_hex(colorscheme[13]), LV_PART_ITEMS | LV_STATE_CHECKED);
	lv_obj_set_style_border_width(obj, 0, LV_PART_ITEMS);
	lv_obj_set_style_radius(obj, 0, LV_PART_ITEMS);
	lv_obj_set_style_pad_all(obj, 6, LV_PART_ITEMS);

	lv_buttonmatrix_set_map(obj, c->map);
	lv_obj_add_event_cb(obj, choice_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

	c->obj = obj;

	return obj;
}

int hmi_choice_add_option(lv_obj_t *ch, const char *name)
{
	hmi_choice_t *c = choice_find(ch);
	if (c == NULL) {
		return -1;
	}
	if (c->option_count >= HMI_CHOICE_MAX_OPTIONS) {
		return -1;
	}

	uint32_t idx = c->option_count;

	c->map[idx] = name != NULL ? name : "";
	c->option_count++;
	c->map[c->option_count] = NULL;
	lv_buttonmatrix_set_map(c->obj, c->map);

	/* set_map reallocates and zeroes ctrl_bits whenever the button count
	 * changes, so the per-button flags are re-applied to every option.
	 */
	if (c->active == UINT32_MAX) {
		c->active = idx;
	}
	choice_sync_ctrl(c);

	return (int)idx;
}

void hmi_choice_set_active(lv_obj_t *ch, uint32_t idx, bool animate)
{
	hmi_choice_t *c = choice_find(ch);
	if (c == NULL || idx >= c->option_count) {
		return;
	}
	LV_UNUSED(animate);   /* a buttonmatrix selection does not animate */

	c->active = idx;
	choice_sync_ctrl(c);
}

uint32_t hmi_choice_get_active(lv_obj_t *ch)
{
	hmi_choice_t *c = choice_find(ch);
	if (c == NULL) {
		return UINT32_MAX;
	}
	for (uint32_t i = 0; i < c->option_count; i++) {
		if (lv_buttonmatrix_has_button_ctrl(c->obj, i,
						    LV_BUTTONMATRIX_CTRL_CHECKED)) {
			c->active = i;
			return i;
		}
	}
	return c->active;
}

void hmi_choice_set_callback(lv_obj_t *ch, hmi_choice_cb_t cb, void *user_data)
{
	hmi_choice_t *c = choice_find(ch);
	if (c == NULL) {
		return;
	}
	c->cb = cb;
	c->user_data = user_data;
}

hmi_choice_t *hmi_choice_get(lv_obj_t *ch)
{
	return choice_find(ch);
}

static hmi_choice_t *choice_find(lv_obj_t *ch)
{
	for (int i = 0; i < HMI_MAX_CHOICES; i++) {
		if (g_choice[i].used && g_choice[i].obj == ch) {
			return &g_choice[i];
		}
	}
	return NULL;
}

static void choice_sync_ctrl(hmi_choice_t *c)
{
	for (uint32_t i = 0; i < c->option_count; i++) {
		lv_buttonmatrix_set_button_ctrl(c->obj, i, HMI_CHOICE_CTRL_BASE);
		if (i == c->active) {
			lv_buttonmatrix_set_button_ctrl(c->obj, i,
							LV_BUTTONMATRIX_CTRL_CHECKED);
		} else {
			lv_buttonmatrix_clear_button_ctrl(c->obj, i,
							  LV_BUTTONMATRIX_CTRL_CHECKED);
		}
	}
}

static void choice_value_changed_cb(lv_event_t *e)
{
	lv_obj_t *obj = lv_event_get_target_obj(e);
	hmi_choice_t *c = choice_find(obj);
	if (c == NULL || c->cb == NULL) {
		return;
	}
	uint32_t idx = lv_buttonmatrix_get_selected_button(obj);
	if (idx == LV_BUTTONMATRIX_BUTTON_NONE || idx >= c->option_count) {
		return;
	}
	c->active = idx;
	c->cb(obj, idx, c->user_data);
}
