/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * svpwm_comm role dispatch. One board runs each side of the comm link (see
 * Kconfig): the G474 runs SVPWM + the comm host peer, the H743 runs the
 * comm device handlers on top of the full display stack.
 */

#ifdef CONFIG_SVPWM_COMM_ROLE_DEVICE

#include <stdio.h>

#include <zephyr/kernel.h>

#include <lvgl.h>

#include <hmi/comm/hmi_comm_proto.h>
#include <hmi/hmi.h>

#include "comm_device.h"

/* --- screen geometry (320x480 panel) --- */
#define GAUGE_SIZE          240
#define GAUGE_Y             6
#define BTN_H               54
#define BTN_ROW1_Y          254
#define BTN_ROW2_Y          316
#define BTN_X0              10
#define BTN_X1              165
#define BTN_W               145
#define READOUT_Y           380
#define ACK_Y               406
#define LOSS_Y              430

/* --- SVPWM setpoint, mirrored from control.c's defaults --- */
#define SVPWM_FREQ_INIT_X10     600u    /* 60.0 Hz */
#define SVPWM_VOLT_INIT_X100    400u    /* 4.00 V  */
#define SVPWM_FREQ_STEP_X10     50u     /* 5.0 Hz  */
#define SVPWM_VOLT_STEP_X100    25u     /* 0.25 V  */

/* colourscheme[] indices from hmi_gauge.c */
#define COLOR_TEXT             colorscheme[22]
#define COLOR_OK               colorscheme[16]
#define COLOR_FAULT            colorscheme[19]

/* Requested setpoint. Written by the button callbacks, which run inside
 * lv_timer_handler() on the LVGL thread, so the readout label can be
 * updated straight from them.
 */
static uint16_t svpwm_freq_x10 = SVPWM_FREQ_INIT_X10;
static uint16_t svpwm_volt_x100 = SVPWM_VOLT_INIT_X100;

static lv_obj_t * readout_lbl;
static lv_obj_t * ack_lbl;
static lv_obj_t * loss_lbl;

static lv_obj_t * make_label(lv_obj_t *parent, int y, const char *text)
{
    lv_obj_t *lbl = lv_label_create(parent);

    lv_label_set_text(lbl, text);
    lv_obj_set_width(lbl, 300);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(COLOR_TEXT), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y);

    return lbl;
}

static void send_svpwm(void)
{
    comm_device_send_svpwm(svpwm_freq_x10, svpwm_volt_x100);
}

/* --- button callbacks (LVGL thread) --- */

static void step_freq(lv_obj_t *btn, bool checked, void *user_data)
{
    intptr_t dir = (intptr_t)user_data;

    ARG_UNUSED(btn);
    ARG_UNUSED(checked);

    if (dir < 0) {
        svpwm_freq_x10 = (svpwm_freq_x10 > SVPWM_FREQ_STEP_X10) ?
            (uint16_t)(svpwm_freq_x10 - SVPWM_FREQ_STEP_X10) : 0u;
    } else {
        svpwm_freq_x10 = (svpwm_freq_x10 + SVPWM_FREQ_STEP_X10 >
                  HMI_SVPWM_FREQ_MAX_X10) ?
            0u : (uint16_t)(svpwm_freq_x10 + SVPWM_FREQ_STEP_X10);
    }

    send_svpwm();
}

static void step_volt(lv_obj_t *btn, bool checked, void *user_data)
{
    intptr_t dir = (intptr_t)user_data;

    ARG_UNUSED(btn);
    ARG_UNUSED(checked);

    if (dir < 0) {
        svpwm_volt_x100 = (svpwm_volt_x100 > SVPWM_VOLT_STEP_X100) ?
            (uint16_t)(svpwm_volt_x100 - SVPWM_VOLT_STEP_X100) : 0u;
    } else {
        svpwm_volt_x100 = (svpwm_volt_x100 + SVPWM_VOLT_STEP_X100 >
                  HMI_SVPWM_VOLT_MAX_X100) ?
            0u : (uint16_t)(svpwm_volt_x100 + SVPWM_VOLT_STEP_X100);
    }

    send_svpwm();
}

/* --- ACK status sync (LVGL thread) ---
 *
 * The ack lands on the comm RX thread, so this timer is the only thing that
 * touches the labels. Same marshal pattern the gauge uses: the comm side
 * writes plain variables, an LVGL timer publishes them.
 */
static void ui_sync_cb(lv_timer_t *timer)
{
    static char text[96];
    comm_device_svpwm_ack_t cur;
    uint32_t lv_color;

    LV_UNUSED(timer);

    comm_device_get_svpwm_ack(&cur);

    snprintf(text, sizeof(text), "F %u.%u Hz    V %u.%02u V",
        svpwm_freq_x10 / 10, svpwm_freq_x10 % 10,
        svpwm_volt_x100 / 100, svpwm_volt_x100 % 100);
    lv_label_set_text(readout_lbl, text);

    if (!cur.valid) {
        snprintf(text, sizeof(text), "no ACK yet (sent %u)", cur.sent);
        lv_color = COLOR_TEXT;
    } else if (cur.status != 0) {
        snprintf(text, sizeof(text), "host rejected (status %u)", cur.status);
        lv_color = COLOR_FAULT;
    } else {
        snprintf(text, sizeof(text), "ACK %u.%u Hz  %u.%02u V",
            cur.freq_x10 / 10, cur.freq_x10 % 10,
            cur.volt_x100 / 100, cur.volt_x100 % 100);
        lv_color = COLOR_OK;
    }
    lv_label_set_text(ack_lbl, text);
    lv_obj_set_style_text_color(ack_lbl, lv_color_hex(lv_color), 0);

    snprintf(text, sizeof(text), "sent %u   acked %u   lost %u",
        cur.sent, cur.acked, cur.sent - cur.acked);
    lv_label_set_text(loss_lbl, text);
}

static lv_obj_t * add_button(int x, int y, const char *label,
                 hmi_button_cb_t cb, intptr_t dir)
{
    lv_obj_t *btn = hmi_button_create(lv_screen_active(), x, y,
                       BTN_W, BTN_H);

    hmi_button_set_label(btn, label);
    hmi_button_set_layout(btn, HMI_BUTTON_ICON_NONE);
    hmi_button_set_type(btn, HMI_BUTTON_NORMAL);
    /* The direction rides as an integer inside the opaque pointer. The
     * callbacks cast it back and compare, so nothing is ever loaded
     * through it: dereferencing dir would fault, since -1 is 0xFFFFFFFF
     * and +1 is not a valid address on any target. */
    hmi_button_set_callback(btn, cb, (void *)dir);

    return btn;
}

static void show_single_gauge(void)
{
    lv_obj_t *gauge;

    /* Bound to the library demo value, so the SET_GAUGE handler in
     * comm_device.c drives the needle without a second copy of the state.
     */
    gauge = hmi_ui_gauge_create(lv_screen_active(), GAUGE_SIZE, "GAUGE", "rpm");
    hmi_ui_gauge_set_range(gauge, 0, 1500);
    hmi_ui_gauge_bind_value(gauge, hmi_gauge_demo_value());
    lv_obj_align(gauge, LV_ALIGN_TOP_MID, 0, GAUGE_Y);

    add_button(BTN_X0, BTN_ROW1_Y, "FREQ -5 Hz", step_freq, -1);
    add_button(BTN_X1, BTN_ROW1_Y, "FREQ +5 Hz", step_freq, +1);
    add_button(BTN_X0, BTN_ROW2_Y, "VOLT -0.25 V", step_volt, -1);
    add_button(BTN_X1, BTN_ROW2_Y, "VOLT +0.25 V", step_volt, +1);

    readout_lbl = make_label(lv_screen_active(), READOUT_Y, "F 60.0 Hz    V 4.00 V");
    ack_lbl     = make_label(lv_screen_active(), ACK_Y, "no ACK yet");
    loss_lbl    = make_label(lv_screen_active(), LOSS_Y, "sent 0   acked 0   lost 0");

    lv_timer_create(ui_sync_cb, 100, NULL);
}

int main(void)
{
    if (hmi_init() < 0) {
        return 0;
    }

    /* Start the comm device before the display loop: the PING/GET_INFO/
     * SET_GAUGE handlers begin immediately, even while the first frames
     * render. No SET_SVPWM goes out until a button is tapped.
     */
    comm_device_start();

    /* The library's demo default is 1450, which the first B1 press would
     * knock back to 0. Start at 0 so the needle matches the host's
     * sequence instead of jumping on the first press.
     */
    hmi_gauge_demo_set(0.0f);

    show_single_gauge();
    hmi_show();

    return 0;   /* unreachable: hmi_show() loops forever */
}

#else /* CONFIG_SVPWM_COMM_ROLE_HOST */

#include "control.h"
#include "spi_cmd.h"
#include "uart_comm.h"

int main(void)
{
    init_tim1_pwm();
    init_tim1_irq();
    spi_cmd_start();
    uart_comm_start();

    return 0;   /* the input/SVPWM threads keep the app running */
}

#endif
