#ifndef HMI_LABEL_H
#define HMI_LABEL_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

/*
 * A labelled numeric readout: a small caption on top, a large value below,
 * and an optional unit appended to the value ("12.3 rpm").
 *
 * Values are refreshed from a float pointer by one module-wide timer, the
 * same way hmi_chart pulls its series -- so an application never has to own
 * a refresh timer of its own. Use hmi_readout_set_text() instead for values
 * that are strings rather than numbers (status lines, fault names).
 *
 * The panel height is derived from its width (the width also picks the
 * value font), so readouts of equal width are equal height and tile into a
 * grid without the caller computing anything. Query the result with
 * lv_obj_get_height() if the layout needs it.
 */

/* 24: the Motor Control HMI needs 19 (MAIN 6 + CONTROL 4 + MONITOR 9).
 * Slots are ~36 B each, so the extra headroom costs ~288 B of .bss.
 */
#define HMI_MAX_READOUTS 24

typedef enum {
	HMI_READOUT_NEUTRAL,  /* primary text; the default              */
	HMI_READOUT_OK,       /* green                                  */
	HMI_READOUT_WARN,     /* amber                                  */
	HMI_READOUT_FAULT     /* red                                    */
} hmi_readout_state_t;

typedef struct {
	lv_obj_t *panel;
	lv_obj_t *title;
	lv_obj_t *value;
	float *bound;
	const char *unit;
	int precision;
	hmi_readout_state_t state;
	bool used;
} hmi_readout_t;

lv_obj_t *hmi_readout_create(lv_obj_t *parent, int x, int y, int width,
			     const char *title, const char *unit);
void hmi_readout_bind_value(lv_obj_t *ro, float *value);
void hmi_readout_set_precision(lv_obj_t *ro, int decimals);
void hmi_readout_set_text(lv_obj_t *ro, const char *text);
void hmi_readout_set_state(lv_obj_t *ro, hmi_readout_state_t state);
hmi_readout_t *hmi_readout_get(lv_obj_t *ro);

#endif /* HMI_LABEL_H */
