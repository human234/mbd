#ifndef HMI_POLAR_H
#define HMI_POLAR_H

#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#define HMI_MAX_POLARS        4
#define HMI_POLAR_MAX_VECTORS 6
#define HMI_POLAR_NAME_LEN    16
#define HMI_POLAR_RINGS       4

typedef enum {
	HMI_POLAR_MODE_POLAR,   /* magnitude + angle (degrees)     */
	HMI_POLAR_MODE_DQ       /* d + q components                */
} hmi_polar_mode_t;

typedef struct {
	lv_obj_t *panel;
	lv_obj_t *title;
	lv_obj_t *legend;
	lv_obj_t *ring_label[HMI_POLAR_RINGS];
	uint32_t color_idx[HMI_POLAR_MAX_VECTORS];
	const char *name[HMI_POLAR_MAX_VECTORS];
	hmi_polar_mode_t mode[HMI_POLAR_MAX_VECTORS];
	float *mag[HMI_POLAR_MAX_VECTORS];      /* polar mode: magnitude         */
	float *angle[HMI_POLAR_MAX_VECTORS];    /* polar mode: angle in degrees  */
	float *d[HMI_POLAR_MAX_VECTORS];        /* dq mode: d component          */
	float *q[HMI_POLAR_MAX_VECTORS];        /* dq mode: q component          */
	float own_mag[HMI_POLAR_MAX_VECTORS];
	float own_angle[HMI_POLAR_MAX_VECTORS];
	float own_d[HMI_POLAR_MAX_VECTORS];
	float own_q[HMI_POLAR_MAX_VECTORS];
	float mag_max;
	uint32_t vector_count;
	lv_timer_t *timer;
	bool used;
} hmi_polar_t;

lv_obj_t *hmi_polar_create(lv_obj_t *parent, int x, int y, int size, const char *title);
uint32_t hmi_polar_add_vector(lv_obj_t *polar, uint32_t color_idx, const char *name,
			      hmi_polar_mode_t mode);
void hmi_polar_set_magnitude_range(lv_obj_t *polar, float max);
void hmi_polar_bind_polar(lv_obj_t *polar, uint32_t vec, float *mag, float *angle_deg);
void hmi_polar_bind_dq(lv_obj_t *polar, uint32_t vec, float *d, float *q);
void hmi_polar_set_vector(lv_obj_t *polar, uint32_t vec, float mag, float angle_deg);
void hmi_polar_set_vector_dq(lv_obj_t *polar, uint32_t vec, float d, float q);
hmi_polar_t *hmi_polar_get(lv_obj_t *polar);

#endif /* HMI_POLAR_H */