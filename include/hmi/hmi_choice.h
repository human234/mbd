#ifndef HMI_CHOICE_H
#define HMI_CHOICE_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * A single-row segmented selector for enumerated values (control mode,
 * unit system, ...) -- the case hmi_button cannot express, being binary,
 * and hmi_slider cannot, being continuous.
 *
 * Exactly one option is checked at a time. Add the options in order with
 * hmi_choice_add_option(), then read/write the selection with
 * hmi_choice_set_active()/hmi_choice_get_active().
 */

#define HMI_MAX_CHOICES       4
#define HMI_CHOICE_MAX_OPTIONS 6

typedef void (*hmi_choice_cb_t)(lv_obj_t *ch, uint32_t active, void *user_data);

typedef struct {
	lv_obj_t *obj;
	/* lv_buttonmatrix does not copy the map it is handed, so the string
	 * table has to live as long as the widget. Both are static pools.
	 */
	const char *map[HMI_CHOICE_MAX_OPTIONS + 2]; /* + spare slot + NULL */
	uint32_t option_count;
	uint32_t active;
	hmi_choice_cb_t cb;
	void *user_data;
	bool used;
} hmi_choice_t;

lv_obj_t *hmi_choice_create(lv_obj_t *parent, int x, int y, int width, int height);
int hmi_choice_add_option(lv_obj_t *ch, const char *name);
void hmi_choice_set_active(lv_obj_t *ch, uint32_t idx, bool animate);
uint32_t hmi_choice_get_active(lv_obj_t *ch);
void hmi_choice_set_callback(lv_obj_t *ch, hmi_choice_cb_t cb, void *user_data);
hmi_choice_t *hmi_choice_get(lv_obj_t *ch);

#endif /* HMI_CHOICE_H */
