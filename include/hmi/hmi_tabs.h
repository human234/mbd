#ifndef HMI_TABS_H
#define HMI_TABS_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#define HMI_MAX_TABS 8

typedef void (*hmi_tabs_cb_t)(lv_obj_t *tv, uint32_t active, void *user_data);

typedef struct {
	lv_obj_t *tv;
	hmi_tabs_cb_t cb;
	void *user_data;
	bool used;
} hmi_tabs_t;

lv_obj_t *hmi_tabs_create(lv_obj_t *parent, int x, int y, int width, int height);
lv_obj_t *hmi_tabs_add_tab(lv_obj_t *tv, const char *name);
void hmi_tabs_set_active(lv_obj_t *tv, uint32_t idx, bool animate);
uint32_t hmi_tabs_get_active(lv_obj_t *tv);
void hmi_tabs_rename(lv_obj_t *tv, uint32_t idx, const char *name);
void hmi_tabs_set_callback(lv_obj_t *tv, hmi_tabs_cb_t cb, void *user_data);
hmi_tabs_t *hmi_tabs_get(lv_obj_t *tv);

#endif /* HMI_TABS_H */