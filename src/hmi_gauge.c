#include <lvgl.h>
#include <stdbool.h>
#include <stdint.h>

#include <hmi/hmi_gauge.h>

typedef struct {
    lv_obj_t * container;
    lv_subject_t * subject;
    float * value;
    int32_t min, max;
    bool used;      /* slot occupied by a gauge */
    bool active;    /* a value pointer is attached */
} hmi_gauge_t;

static hmi_gauge_t g_gauge[HMI_MAX_GAUGES];
static lv_timer_t * g_gauge_timer;

static hmi_gauge_t * gauge_find(lv_obj_t *gauge);
static void gauge_refresh_cb(lv_timer_t *timer);

uint32_t colorscheme[] = {
    /* Background */
    0x000025,   //  0  Deep Black
    0x05051d,   //  1  Graphite
    0x0c0b23,   //  2  Dark Navy
    0x141833,   //  3  Charcoal

    /* Surface */
    0x24233a,   //  4  Panel
    0x272943,   //  5  Raised Panel
    0x1c1c34,   //  6  Surface
    0x363747,   //  7  Surface Highlight

    /* Neutral */
    0x16162a,   //  8  Border
    0x252727,   //  9  Divider
    0x373839,   // 10  Disabled
    0x171718,   // 11  Neutral

    /* Accent */
    0x0c93f3,   // 12  Deep Blue
    0x1394f2,   // 13  Electric Blue
    0x346aa1,   // 14  Steel Blue
    0x9acbfe,   // 15  Sky Blue

    /* Status */
    0x3b8f63,   // 16  Green
    0x4caf72,   // 17  Green Highlight
    0xc9852f,   // 18  Warning
    0xc94a4a,   // 19  Fault

    /* Information / Text */
    0x22365a,   // 20  Industrial Blue
    0x346aa1,   // 21  Blue Highlight
    0x9acbfe,   // 22  Primary Text
    0x2b3a5b    // 23  Secondary Text
};

static float gauge_test_value = 1450;

float hmi_gauge_demo_get(void)
{
    return gauge_test_value;
}

void hmi_gauge_demo_set(float value)
{
    gauge_test_value = value;
}

float *hmi_gauge_demo_value(void)
{
    return &gauge_test_value;
}

void hmi_gauge_show_ui(lv_obj_t *parent)
{
    lv_obj_t *gauge = hmi_ui_gauge_create(parent, 160, "MOTOR", "rpm");
    hmi_ui_widget_set_pos(gauge, 24, 24);
    hmi_ui_gauge_set_range(gauge, 0, 3000);
    hmi_ui_gauge_bind_value(gauge, &gauge_test_value);
}

lv_obj_t * hmi_ui_gauge_create(lv_obj_t *parent, int size, const char *title, const char *unit)
{
    if(size <= 0) return NULL;

    // elements
    lv_obj_t *gauge_container = lv_obj_create(parent);
    lv_obj_t *arc = lv_arc_create(gauge_container);
    lv_obj_t *label = lv_label_create(arc);
    lv_obj_t *unit_label = lv_label_create(arc);
    lv_obj_t *title_label = lv_label_create(gauge_container);

    // data (subject allocated from the LVGL memory pool)
    lv_subject_t *gauge_value = lv_malloc(sizeof(lv_subject_t));
    if(gauge_value == NULL) return NULL;
    lv_subject_init_int(gauge_value, 0);

    // element scale
    int border = LV_MAX(size / 32, 3);
    int arc_size = size * 3 / 4;
    int arc_width = LV_MAX(size / 8, 6);
    int unit_y_offset = size / 8;

    const lv_font_t *font;
    if(size >= 220) font = &lv_font_montserrat_38;
    else if(size >= 180) font = &lv_font_montserrat_32;
    else if(size >= 150) font = &lv_font_montserrat_28;
    else if(size >= 120) font = &lv_font_montserrat_22;
    else if(size >= 90) font = &lv_font_montserrat_18;
    else font = &lv_font_montserrat_12;

    // container
    lv_obj_set_pos(gauge_container, 0, 0);
    lv_obj_set_size(gauge_container, size, size);
    lv_obj_set_style_border_width(gauge_container, border, 0);
    lv_obj_set_style_border_color(gauge_container, lv_color_hex(colorscheme[8]), 0);
    lv_obj_set_style_bg_color(gauge_container, lv_color_hex(colorscheme[4]), 0);
    lv_obj_set_style_radius(gauge_container, 0, 0);
    lv_obj_remove_flag(gauge_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(gauge_container, 0, 0);

    // arc
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 0);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_rotation(arc, 90);
    lv_arc_set_range(arc, 0, 100);
    lv_obj_set_style_arc_rounded(arc, false, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_size(arc, arc_size, arc_size);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc, lv_color_hex(colorscheme[23]), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, arc_width, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(colorscheme[13]), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc, arc_width, LV_PART_INDICATOR);
    lv_arc_bind_value(arc, gauge_value);

    // label
    lv_subject_set_int(gauge_value, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
    lv_label_bind_text(label, gauge_value, NULL);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(colorscheme[22]), 0);

    // unit label
    lv_obj_align(unit_label, LV_ALIGN_CENTER, 0, unit_y_offset);
    if(unit != NULL && unit[0] != '\0') {
        lv_label_set_text(unit_label, unit);
        lv_obj_set_style_text_font(unit_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(unit_label, lv_color_hex(colorscheme[22]), 0);
    }
    else {
        lv_obj_add_flag(unit_label, LV_OBJ_FLAG_HIDDEN);
    }

    // title label
    lv_obj_align(title_label, LV_ALIGN_TOP_LEFT, border, border);
    if(title != NULL && title[0] != '\0') {
        lv_label_set_text(title_label, title);
        lv_obj_set_style_text_font(title_label, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(title_label, lv_color_hex(colorscheme[22]), 0);
    }
    else {
        lv_obj_add_flag(title_label, LV_OBJ_FLAG_HIDDEN);
    }

    // register the gauge in the next free slot
    hmi_gauge_t *g = NULL;
    for(int i = 0; i < HMI_MAX_GAUGES; i++) {
        if(!g_gauge[i].used) { g = &g_gauge[i]; break; }
    }
    if(g) {
        g->used = true;
        g->active = false;
        g->value = NULL;
        g->container = gauge_container;
        g->subject = gauge_value;
        g->min = 0;
        g->max = 100;
    }

    if(g_gauge_timer == NULL)
        g_gauge_timer = lv_timer_create(gauge_refresh_cb, 100, NULL);

    return gauge_container;
}

void hmi_ui_widget_set_pos(lv_obj_t *widget, int x, int y)
{
    lv_obj_set_pos(widget, x, y);
}

void hmi_ui_gauge_set_range(lv_obj_t *gauge, int32_t min, int32_t max)
{
    if(min > max) { int32_t t = min; min = max; max = t; }
    hmi_gauge_t *g = gauge_find(gauge);
    if(g) { g->min = min; g->max = max; }
    lv_obj_t *arc = lv_obj_get_child(gauge, 0);
    if(arc) lv_arc_set_range(arc, min, max);
}

void hmi_ui_gauge_bind_value(lv_obj_t *gauge, float *value)
{
    hmi_gauge_t *g = gauge_find(gauge);
    if(g) { g->value = value; g->active = true; }
}

static hmi_gauge_t * gauge_find(lv_obj_t *gauge)
{
    for(int i = 0; i < HMI_MAX_GAUGES; i++) {
        if(g_gauge[i].used && g_gauge[i].container == gauge) return &g_gauge[i];
    }
    return NULL;
}

static void gauge_refresh_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);
    for(int i = 0; i < HMI_MAX_GAUGES; i++) {
        hmi_gauge_t *g = &g_gauge[i];
        if(!g->used || !g->active || g->value == NULL) continue;
        int32_t v = (int32_t)(*g->value);
        if(v < g->min) v = g->min;
        if(v > g->max) v = g->max;
        if(lv_subject_get_int(g->subject) != v)
            lv_subject_set_int(g->subject, v);
    }
}
