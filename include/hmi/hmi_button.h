#ifndef HMI_BUTTON_H
#define HMI_BUTTON_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#define HMI_MAX_BUTTONS 16

typedef enum {
	HMI_BUTTON_NORMAL,
	HMI_BUTTON_TOGGLE
} hmi_button_type_t;

typedef enum {
	HMI_BUTTON_ICON_NONE,
	HMI_BUTTON_ICON_LEFT,
	HMI_BUTTON_ICON_TOP
} hmi_button_layout_t;

typedef void (*hmi_button_cb_t)(lv_obj_t *btn, bool checked, void *user_data);

typedef struct {
	lv_obj_t *btn;
	hmi_button_type_t type;
	hmi_button_layout_t layout;
	lv_obj_t *icon;
	lv_obj_t *label;
	hmi_button_cb_t cb;
	void *user_data;
	bool used;
} hmi_button_t;

lv_obj_t *hmi_button_create(lv_obj_t *parent, int x, int y, int width, int height);
void hmi_button_set_label(lv_obj_t *btn, const char *text);
void hmi_button_set_icon(lv_obj_t *btn, const char *symbol);
void hmi_button_set_layout(lv_obj_t *btn, hmi_button_layout_t layout);
void hmi_button_set_type(lv_obj_t *btn, hmi_button_type_t type);
void hmi_button_set_checked(lv_obj_t *btn, bool checked);
bool hmi_button_is_checked(lv_obj_t *btn);
void hmi_button_set_callback(lv_obj_t *btn, hmi_button_cb_t cb, void *user_data);
void hmi_button_set_enabled(lv_obj_t *btn, bool enabled);
hmi_button_t *hmi_button_get(lv_obj_t *btn);

#endif /* HMI_BUTTON_H */