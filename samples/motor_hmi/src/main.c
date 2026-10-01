/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Motor Control HMI -- the professor's four screens, composed from the
 * HMI widget set on the 480x320 landscape panel (rotation 270, Phase B).
 *
 *   MAIN      read  Speed / Torque / Id / Iq / Vdc / status
 *             edit  RUN-STOP, speed command
 *   CONTROL   read  speed, Id, Iq setpoints + mode
 *             edit  speed ref, torque ref, Id ref, mode
 *   TUNING    read  current Kp/Ki per loop
 *             edit  the six gains
 *   MONITOR   read  Vdc / Ia / Ib / Ic / Id / Iq / speed / fault
 *             edit  fault reset
 *
 * The tab bar is 40 px, so every page is a fixed 480 x 280 canvas. Each
 * page is a plain absolute layout: widgets are placed by top-left corner,
 * and the widths were chosen so hmi_readout/hmi_slider derive the same
 * font (and therefore the same height) for every cell in a row. The
 * numbers below are the contract -- moving one means re-deriving the rest:
 *
 *   readout height  = 23 + value line height
 *                     (w>=200 -> 32/35 -> 58, >=150 -> 28/30 -> 53,
 *                      >=110 -> 22/24 -> 47, >=80 -> 18/21 -> 44,
 *                      else   16/18 -> 41)
 *   slider height   = value line height + 16 + 24
 *                     (w>=240 -> 70, >=180 -> 64, >=120 -> 61, else 58)
 *
 * There is no comm link on this pass (Phase 4): a 100 ms timer stands in
 * for the drive, so the readouts move, the gains visibly change the
 * response, and pushing the load into the overcurrent region trips and
 * latches protection exactly as the real firmware would.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include <lvgl.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>
#include <hmi/hmi.h>

LOG_MODULE_REGISTER(motor_hmi, LOG_LEVEL_INF);

#define SCR_W 480
#define SCR_H 320
#define TABS_H 40
#define PAGE_H (SCR_H - TABS_H)

#define TWO_PI_F 6.2831853f
#define DQ_2PI_3 2.0943951f

/* Torque constant of the simulated machine, Nm per ampere of Iq. */
#define KT 1.5f

/* Mean |phase current| above which protection trips. With Id <= 5 A and
 * Torque Ref <= 25 Nm (Iq <= 16.7 A) the balanced-set mean reaches about
 * 11 A, so this sits high enough to need a deliberate over-range command
 * and still trips before the slider hits its end.
 */
#define LOAD_TRIP_A 9.0f

enum {
    MODE_SPEED = 0,
    MODE_TORQUE,
    MODE_ID,
};

enum {
    RUN_IDX_RUN = 0,
    RUN_IDX_STOP,
};

static const char *const g_tab_name[] = { "MAIN", "CONTROL", "TUNING",
                      "MONITOR" };
static const char *const g_mode_name[] = { "Speed", "Torque", "Id" };

/* ---- setpoints (driven by the UI) ---------------------------------- */
static float ref_speed = 1200.0f;   /* rpm */
static float ref_torque = 5.0f;     /* Nm  */
static float ref_id = 0.0f;         /* A   */
static float ref_iq = 0.0f;         /* A, derived from ref_torque */

/* ---- machine state (driven by the simulator) ----------------------- */
static float m_speed;               /* rpm */
static float m_torque;              /* Nm  */
static float m_id;                  /* A   */
static float m_iq;                  /* A   */
static float m_vdc = 48.0f;         /* V   */
static float m_ia;
static float m_ib;
static float m_ic;
static float m_phase;               /* rad */

/* ---- gains (driven by the TUNING screen) --------------------------- */
static float kp_speed = 120.0f;
static float ki_speed = 40.0f;
static float kp_id = 4.0f;
static float ki_id = 80.0f;
static float kp_iq = 4.0f;
static float ki_iq = 80.0f;

/* ---- run state ----------------------------------------------------- */
static bool running;
static bool fault_latched;
static const char *fault_str = "None";
static int mode = MODE_SPEED;
static int last_announced = -1;     /* -1 unknown, 0 stop, 1 run, 2 trip */

/* ---- widget handles shared between screens and callbacks ------------ */
static lv_obj_t *ch_run;
static lv_obj_t *ch_mode;
static lv_obj_t *sl_speed_main;
static lv_obj_t *sl_speed_ctrl;
static lv_obj_t *ro_status;
static lv_obj_t *ro_mode;
static lv_obj_t *ro_fault;
static lv_obj_t *ro_trip;

/* ==================================================================== */
/* Layout helpers                                                        */
/* ==================================================================== */

static lv_obj_t *caption(lv_obj_t *parent, int x, int y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);

    lv_obj_set_pos(l, x, y);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(colorscheme[23]), 0);
    return l;
}

static lv_obj_t *readout(lv_obj_t *parent, int x, int y, int width,
             const char *title, const char *unit, float *bind,
             int precision)
{
    lv_obj_t *ro = hmi_readout_create(parent, x, y, width, title, unit);

    if (ro != NULL && bind != NULL) {
        hmi_readout_bind_value(ro, bind);
        hmi_readout_set_precision(ro, precision);
    }
    return ro;
}

static lv_obj_t *slider(lv_obj_t *parent, int x, int y, int width, double lo,
            double hi, double step, const char *unit, int precision,
            double initial)
{
    lv_obj_t *s = hmi_slider_create(parent, x, y, width);

    if (s == NULL) {
        return NULL;
    }
    /* set_range first: it recomputes the step and re-clamps the LVGL
     * position, which would otherwise discard a value set before it.
     */
    hmi_slider_set_range(s, lo, hi, step);
    hmi_slider_set_unit(s, unit);
    hmi_slider_set_precision(s, precision);
    hmi_slider_set_value(s, initial);
    return s;
}

/* A tabview page wants a fixed canvas: a stray drag must not pan the
 * layout away from where it was laid out.
 */
static lv_obj_t *tab_page(lv_obj_t *tv, const char *name)
{
    lv_obj_t *page = hmi_tabs_add_tab(tv, name);

    if (page != NULL) {
        lv_obj_remove_flag(page, LV_OBJ_FLAG_SCROLLABLE);
    }
    return page;
}

/* ==================================================================== */
/* Callbacks                                                             */
/* ==================================================================== */

/* The speed command on MAIN and the speed ref on CONTROL are the same
 * setpoint shown on two screens, so moving either moves the other.
 * hmi_slider_set_value() goes through lv_bar_set_value(), which does not
 * emit VALUE_CHANGED, so this cannot recurse -- the flag is belt and
 * braces in case that ever changes.
 */
static bool g_syncing;

static void speed_ref_cb(lv_obj_t *sl, double value, void *user_data)
{
    LV_UNUSED(user_data);

    if (g_syncing) {
        return;
    }
    g_syncing = true;
    ref_speed = (float)value;

    lv_obj_t *peer = (sl == sl_speed_main) ? sl_speed_ctrl : sl_speed_main;
    if (peer != NULL) {
        hmi_slider_set_value(peer, value);
    }
    g_syncing = false;
}

static void scalar_ref_cb(lv_obj_t *sl, double value, void *user_data)
{
    LV_UNUSED(sl);
    *(float *)user_data = (float)value;
}

static void run_cb(lv_obj_t *ch, uint32_t active, void *user_data)
{
    LV_UNUSED(user_data);

    if (fault_latched) {
        /* Protection owns the drive until it is reset. */
        hmi_choice_set_active(ch, RUN_IDX_STOP, false);
        return;
    }
    running = (active == RUN_IDX_RUN);
    LOG_INF("run state -> %s", running ? "RUN" : "STOP");
}

static void mode_cb(lv_obj_t *ch, uint32_t active, void *user_data)
{
    LV_UNUSED(ch);
    LV_UNUSED(user_data);

    if (active > MODE_ID) {
        return;
    }
    mode = (int)active;
    if (ro_mode != NULL) {
        hmi_readout_set_text(ro_mode, g_mode_name[active]);
    }
    LOG_INF("control mode -> %s", g_mode_name[active]);
}

static void fault_reset_cb(lv_obj_t *btn, bool checked, void *user_data)
{
    LV_UNUSED(btn);
    LV_UNUSED(checked);
    LV_UNUSED(user_data);

    if (!fault_latched) {
        LOG_INF("fault reset with no active trip");
        return;
    }
    fault_latched = false;
    fault_str = "None";
    last_announced = -1;   /* force the status row to repaint */
    LOG_INF("trip cleared");
}

static void tab_cb(lv_obj_t *tv, uint32_t active, void *user_data)
{
    LV_UNUSED(tv);
    LV_UNUSED(user_data);

    if (active < ARRAY_SIZE(g_tab_name)) {
        LOG_INF("tab %s", g_tab_name[active]);
    }
}

/* ==================================================================== */
/* Status row                                                            */
/* ==================================================================== */

static void update_status(void)
{
    int now = fault_latched ? 2 : (running ? 1 : 0);

    if (now == last_announced) {
        return;
    }
    last_announced = now;

    if (ro_status != NULL) {
        hmi_readout_set_text(ro_status,
                     now == 2 ? "FAULT" :
                     now == 1 ? "RUNNING" : "STOPPED");
        hmi_readout_set_state(ro_status,
                      now == 2 ? HMI_READOUT_FAULT :
                      now == 1 ? HMI_READOUT_OK :
                      HMI_READOUT_NEUTRAL);
    }
    if (ro_fault != NULL) {
        hmi_readout_set_text(ro_fault, now == 2 ? "TRIP" : "None");
        hmi_readout_set_state(ro_fault,
                      now == 2 ? HMI_READOUT_FAULT :
                      now == 1 ? HMI_READOUT_OK :
                      HMI_READOUT_NEUTRAL);
    }
    if (ro_trip != NULL) {
        hmi_readout_set_text(ro_trip,
                     now == 2 ? fault_str : "No active trip");
        hmi_readout_set_state(ro_trip,
                      now == 2 ? HMI_READOUT_FAULT :
                      now == 1 ? HMI_READOUT_OK :
                      HMI_READOUT_NEUTRAL);
    }
    if (now == 2 && ch_run != NULL) {
        hmi_choice_set_active(ch_run, RUN_IDX_STOP, false);
    }
}

/* ==================================================================== */
/* Simulator                                                             */
/* ==================================================================== */

static void sim_cb(lv_timer_t *timer)
{
    LV_UNUSED(timer);

    float tgt_speed = 0.0f;
    float tgt_torque = 0.0f;
    float tgt_id = 0.0f;

    if (running && !fault_latched) {
        tgt_id = ref_id;
        switch (mode) {
        case MODE_TORQUE:
            tgt_torque = ref_torque;
            tgt_speed = ref_torque * 120.0f;
            break;
        case MODE_ID:
            break;
        case MODE_SPEED:
        default:
            tgt_speed = ref_speed;
            tgt_torque = ref_torque;
            break;
        }
    }

    /* The TUNING gains are not decorative: Kp/Ki of the speed loop set
     * the lag applied here, so raising them is visible on MAIN within
     * a couple of ticks.
     */
    float k = 0.02f + kp_speed / 500.0f * 0.10f +
          ki_speed / 100.0f * 0.01f;

    if (k > 0.30f) {
        k = 0.30f;
    }

    m_speed += (tgt_speed - m_speed) * k;
    m_torque += (tgt_torque - m_torque) * 0.15f;
    m_id += (tgt_id - m_id) * 0.25f;
    m_iq = m_torque / KT;
    ref_iq = ref_torque / KT;

    m_phase += m_speed * 0.0006f;
    while (m_phase >= TWO_PI_F) {
        m_phase -= TWO_PI_F;
    }

    float c0 = cosf(m_phase);
    float s0 = sinf(m_phase);
    float c1 = cosf(m_phase - DQ_2PI_3);
    float s1 = sinf(m_phase - DQ_2PI_3);
    float c2 = cosf(m_phase + DQ_2PI_3);
    float s2 = sinf(m_phase + DQ_2PI_3);

    m_ia = m_id * c0 - m_iq * s0;
    m_ib = m_id * c1 - m_iq * s1;
    m_ic = m_id * c2 - m_iq * s2;

    float load = (fabsf(m_ia) + fabsf(m_ib) + fabsf(m_ic)) / 3.0f;

    m_vdc = 48.0f - 0.4f * load + 0.10f * sinf(m_phase * 3.0f);

    if (!fault_latched && load > LOAD_TRIP_A) {
        fault_latched = true;
        fault_str = "Phase overcurrent";
        running = false;
        last_announced = -1;
        /* No %f here: CONFIG_CBPRINTF_FP_SUPPORT is off. */
        LOG_WRN("trip: %s (load %d.%d A)", fault_str, (int)load,
            (int)(load * 10.0f) % 10);
    }

    update_status();
}

/* ==================================================================== */
/* Screens                                                               */
/* ==================================================================== */

/* 3 x 2 grid of 149 px cells (font22 -> h47), then RUN-STOP and the
 * speed command. Ends at y=256 of 280.
 */
static void build_main(lv_obj_t *page)
{
    readout(page, 8, 8, 149, "Speed", "rpm", &m_speed, 0);
    readout(page, 165, 8, 149, "Torque", "Nm", &m_torque, 1);
    readout(page, 322, 8, 149, "Id", "A", &m_id, 1);
    readout(page, 8, 63, 149, "Iq", "A", &m_iq, 1);
    readout(page, 165, 63, 149, "Vdc", "V", &m_vdc, 1);
    ro_status = readout(page, 322, 63, 149, "Status", NULL, NULL, 0);

    caption(page, 8, 118, "COMMAND");

    ch_run = hmi_choice_create(page, 8, 136, 464, 40);
    hmi_choice_add_option(ch_run, "RUN");
    hmi_choice_add_option(ch_run, "STOP");
    hmi_choice_set_active(ch_run, RUN_IDX_STOP, false);
    hmi_choice_set_callback(ch_run, run_cb, NULL);

    sl_speed_main = slider(page, 8, 186, 464, 0.0, 3000.0, 10.0, "rpm", 0,
                   ref_speed);
    hmi_slider_set_callback(sl_speed_main, speed_ref_cb, NULL);
}

/* 4 x 1 read row (font22 -> h47), three setpoint sliders (font18 -> h61)
 * and the mode selector. Ends at y=230 of 280.
 */
static void build_control(lv_obj_t *page)
{
    readout(page, 8, 8, 110, "Speed ref", "rpm", &ref_speed, 0);
    readout(page, 126, 8, 110, "Id ref", "A", &ref_id, 1);
    readout(page, 244, 8, 110, "Iq ref", "A", &ref_iq, 1);
    ro_mode = readout(page, 362, 8, 110, "Mode", NULL, NULL, 0);
    if (ro_mode != NULL) {
        hmi_readout_set_text(ro_mode, g_mode_name[mode]);
    }

    caption(page, 8, 66, "SETPOINTS");

    sl_speed_ctrl = slider(page, 8, 88, 150, 0.0, 3000.0, 10.0, "rpm", 0,
                   ref_speed);
    hmi_slider_set_callback(sl_speed_ctrl, speed_ref_cb, NULL);

    lv_obj_t *s = slider(page, 166, 88, 150, 0.0, 25.0, 0.5, "Nm", 1,
                 ref_torque);

    hmi_slider_set_callback(s, scalar_ref_cb, &ref_torque);

    s = slider(page, 324, 88, 150, -5.0, 5.0, 0.1, "A", 1, ref_id);
    hmi_slider_set_callback(s, scalar_ref_cb, &ref_id);

    caption(page, 8, 161, "CONTROL MODE");

    ch_mode = hmi_choice_create(page, 8, 178, 464, 52);
    hmi_choice_add_option(ch_mode, g_mode_name[MODE_SPEED]);
    hmi_choice_add_option(ch_mode, g_mode_name[MODE_TORQUE]);
    hmi_choice_add_option(ch_mode, g_mode_name[MODE_ID]);
    hmi_choice_set_active(ch_mode, MODE_SPEED, false);
    hmi_choice_set_callback(ch_mode, mode_cb, NULL);
}

/* 3 x 2 gain matrix: row label, then Kp and Ki columns (font22 -> h64).
 * Ends at y=240 of 280.
 */
static void build_tuning(lv_obj_t *page)
{
    static const struct {
        int x;
        const char *name;
    } col[] = { { 58, "Kp" }, { 270, "Ki" } };
    static const struct {
        int y;
        const char *name;
        float *kp;
        float *ki;
        double kp_lo, kp_hi, kp_step, kp_init;
        double ki_lo, ki_hi, ki_step, ki_init;
    } row[] = {
        { 32, "Speed", &kp_speed, &ki_speed,
          0.0, 500.0, 1.0, 120.0, 0.0, 100.0, 1.0, 40.0 },
        { 104, "Id", &kp_id, &ki_id,
          0.0, 20.0, 0.1, 4.0, 0.0, 200.0, 1.0, 80.0 },
        { 176, "Iq", &kp_iq, &ki_iq,
          0.0, 20.0, 0.1, 4.0, 0.0, 200.0, 1.0, 80.0 },
    };

    for (size_t i = 0; i < ARRAY_SIZE(col); i++) {
        lv_obj_t *l = lv_label_create(page);

        lv_obj_set_pos(l, col[i].x, 8);
        lv_label_set_text(l, col[i].name);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(colorscheme[23]), 0);
    }

    for (size_t i = 0; i < ARRAY_SIZE(row); i++) {
        lv_obj_t *l = lv_label_create(page);

        lv_obj_set_pos(l, 8, row[i].y + 14);
        lv_label_set_text(l, row[i].name);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, lv_color_hex(colorscheme[22]), 0);

        lv_obj_t *s = slider(page, col[0].x, row[i].y, 204,
                     row[i].kp_lo, row[i].kp_hi,
                     row[i].kp_step, NULL, 1, row[i].kp_init);

        hmi_slider_set_callback(s, scalar_ref_cb, row[i].kp);

        s = slider(page, col[1].x, row[i].y, 204, row[i].ki_lo,
               row[i].ki_hi, row[i].ki_step, NULL, 0,
               row[i].ki_init);
        hmi_slider_set_callback(s, scalar_ref_cb, row[i].ki);
    }
}

/* 4 x 2 grid of 110 px cells (font22 -> h47), then the trip banner
 * (font32 -> h58) and the reset button. Ends at y=270 of 280.
 */
static void build_monitor(lv_obj_t *page)
{
    readout(page, 8, 8, 110, "Vdc", "V", &m_vdc, 1);
    readout(page, 126, 8, 110, "Ia", "A", &m_ia, 1);
    readout(page, 244, 8, 110, "Ib", "A", &m_ib, 1);
    readout(page, 362, 8, 110, "Ic", "A", &m_ic, 1);
    readout(page, 8, 63, 110, "Id", "A", &m_id, 1);
    readout(page, 126, 63, 110, "Iq", "A", &m_iq, 1);
    readout(page, 244, 63, 110, "Speed", "rpm", &m_speed, 0);
    ro_fault = readout(page, 362, 63, 110, "Fault", NULL, NULL, 0);

    caption(page, 8, 124, "PROTECTION");

    ro_trip = readout(page, 8, 146, 464, "Trip detail", NULL, NULL, 0);
    if (ro_trip != NULL) {
        hmi_readout_set_text(ro_trip, "No active trip");
    }

    lv_obj_t *btn = hmi_button_create(page, 8, 214, 464, 56);

    hmi_button_set_label(btn, "Fault Reset");
    hmi_button_set_icon(btn, LV_SYMBOL_REFRESH);
    hmi_button_set_layout(btn, HMI_BUTTON_ICON_LEFT);
    hmi_button_set_type(btn, HMI_BUTTON_NORMAL);
    hmi_button_set_callback(btn, fault_reset_cb, NULL);
}

static void build_hmi(lv_obj_t *parent)
{
    lv_obj_t *tv = hmi_tabs_create(parent, 0, 0, SCR_W, SCR_H);

    hmi_tabs_set_callback(tv, tab_cb, NULL);

    printk("HMI: tabs\n");
    build_main(tab_page(tv, g_tab_name[0]));
    printk("HMI: MAIN ok\n");
    build_control(tab_page(tv, g_tab_name[1]));
    printk("HMI: CONTROL ok\n");
    build_tuning(tab_page(tv, g_tab_name[2]));
    printk("HMI: TUNING ok\n");
    build_monitor(tab_page(tv, g_tab_name[3]));
    printk("HMI: MONITOR ok\n");

    update_status();
    printk("HMI: build done\n");
}

int main(void)
{
    printk("HMI: main enter\n");
    if (hmi_init() < 0) {
        printk("HMI: hmi_init FAILED\n");
        return 0;
    }
    printk("HMI: hmi_init ok\n");

    build_hmi(lv_screen_active());
    printk("HMI: build_hmi returned\n");
    lv_timer_create(sim_cb, 100, NULL);
    printk("HMI: timer ok\n");

    hmi_show();
    printk("HMI: hmi_show returned\n");

    return 0;   /* unreachable: hmi_show() loops forever */
}
