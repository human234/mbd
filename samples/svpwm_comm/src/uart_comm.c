/*
 * uart_comm.c -- HMI comm host peer (NUCLEO-G474RE) against the H743
 * svpwm_comm device role (comm_device.c).
 *
 * Two-board comm backend test over a real wire. Both sides use their Arduino
 * D0/D1 header serial: this side usart1 D1(PC4 TX)/D0(PC5 RX), the H743
 * lpuart1 D1(PB6 TX)/D0(PB7 RX), 115200, shared GND. This side runs the SAME
 * comm core and UART backend as the H743, so a clean exchange exercises the
 * backend fully in both directions.
 *
 * Both boards are button-driven, and nothing is sent until a button is
 * pressed, so one frame pair on the wire is one button press:
 *
 *   G474 B1 press -> SET_GAUGE (v += 100, 0..1500, wrap) -> ACK
 *                    the H743 handler moves the centered gauge needle
 *
 *   H743 tap      -> SET_SVPWM (freq_x10 + volt_x100)    -> ACK with echo
 *                    applied here by set_frequency()/set_magnitude()
 *
 * There is no periodic traffic of any kind: no PING, no GET_INFO, no stats
 * snapshot, no telemetry. The PONG/INFO handlers stay registered so a
 * hand-built PING still round-trips without a rebuild.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>

#include <hmi/comm/hmi_comm.h>
#include <hmi/comm/hmi_comm_proto.h>

#include "control.h"
#include "uart_comm.h"

LOG_MODULE_REGISTER(uart_comm, LOG_LEVEL_INF);

#define GAUGE_MIN      0u
#define GAUGE_MAX      1500u
#define GAUGE_STEP     100u

static uint32_t stat_sent_setgauge;
static uint32_t stat_ack;

/* Next value to send; starts at the minimum and steps up, wrapping to
 * GAUGE_MIN one step past GAUGE_MAX.
 */
static uint32_t gauge_next = GAUGE_MIN;

/* --- peer message handlers (comm RX thread context) --- */

static void on_pong(uint8_t id, const uint8_t *data, size_t len)
{
    ARG_UNUSED(id);
    ARG_UNUSED(data);
    LOG_INF("PONG rx, payload %u B", (uint32_t)len);
}

static void on_info(uint8_t id, const uint8_t *data, size_t len)
{
    ARG_UNUSED(id);
    if (len >= 2) {
        LOG_INF("INFO peer ver %u.%u name '%.*s'", data[0], data[1],
            (int)(len - 2), (const char *)&data[2]);
    } else {
        LOG_INF("INFO peer (no payload)");
    }
}

static void on_ack(uint8_t id, const uint8_t *data, size_t len)
{
    ARG_UNUSED(id);
    if (len < HMI_ACK_SIZE) {
        LOG_INF("ACK rx (short %u B)", (uint32_t)len);
        return;
    }

    stat_ack++;

    if (data[0] == HMI_MSG_SET_SVPWM && len >= HMI_ACK_SVPWM_SIZE) {
        uint16_t f10  = hmi_comm_get_u16(&data[2]);
        uint16_t v100 = hmi_comm_get_u16(&data[4]);

        LOG_INF("ACK id 0x%02X status %u (applied f=%u.%u Hz v=%u.%02u V)",
            data[0], data[1],
            f10 / 10, f10 % 10, v100 / 100, v100 % 100);
    } else {
        LOG_INF("ACK for id 0x%02X status %u", data[0], data[1]);
    }
}

/* SET_SVPWM arrives from the H743's on-screen buttons: apply it to the
 * running SVPWM reference and echo the values back so the device can display
 * what this board really latched, not merely what it asked for.
 */
static void on_set_svpwm(uint8_t id, const uint8_t *data, size_t len)
{
    uint8_t ack[HMI_ACK_SVPWM_SIZE];
    uint16_t f10;
    uint16_t v100;

    ARG_UNUSED(id);

    ack[0] = HMI_MSG_SET_SVPWM;

    if (len != HMI_SVPWM_PAYLOAD_SIZE) {
        LOG_WRN("SET_SVPWM bad len %u", (uint32_t)len);
        ack[1] = 1;
        hmi_comm_send(HMI_MSG_ACK, ack, HMI_ACK_SIZE);
        return;
    }

    f10  = hmi_comm_get_u16(&data[0]);
    v100 = hmi_comm_get_u16(&data[2]);

    if (f10 > HMI_SVPWM_FREQ_MAX_X10 || v100 > HMI_SVPWM_VOLT_MAX_X100) {
        LOG_WRN("SET_SVPWM out of range f_x10=%u v_x100=%u", f10, v100);
        ack[1] = 1;
        hmi_comm_send(HMI_MSG_ACK, ack, HMI_ACK_SIZE);
        return;
    }

    set_frequency((float)f10 / 10.0f);
    set_magnitude((float)v100 / 100.0f);

    ack[1] = 0;
    hmi_comm_put_u16(&ack[2], f10);
    hmi_comm_put_u16(&ack[4], v100);
    if (hmi_comm_send(HMI_MSG_ACK, ack, sizeof(ack)) == 0) {
        LOG_INF("SET_SVPWM f=%u.%u Hz v=%u.%02u V -> ACK",
            f10 / 10, f10 % 10, v100 / 100, v100 % 100);
    }
}


/* --- user button (input thread context) --- */

static void send_next_gauge(void)
{
    uint8_t set[5];

    set[0] = 0; /* gauge index */
    hmi_comm_put_u32(&set[1], gauge_next);

    if (hmi_comm_send(HMI_MSG_SET_GAUGE, set, sizeof(set)) == 0) {
        stat_sent_setgauge++;
        LOG_INF("btn -> SET_GAUGE v=%u", gauge_next);
    }

    gauge_next += GAUGE_STEP;
    if (gauge_next > GAUGE_MAX) {
        gauge_next = GAUGE_MIN;
    }
}

static void on_button(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    if (evt->type != INPUT_EV_KEY || evt->code != INPUT_KEY_0) {
        return;
    }
    if (evt->value == 0) {   /* act on the press edge only */
        return;
    }

    send_next_gauge();
}

INPUT_CALLBACK_DEFINE(NULL, on_button, NULL);

int uart_comm_start(void)
{
    if (hmi_comm_init() < 0) {
        LOG_ERR("comm init failed");
        return -EIO;
    }

    hmi_comm_set_handler(HMI_MSG_PONG, on_pong);
    hmi_comm_set_handler(HMI_MSG_INFO, on_info);
    hmi_comm_set_handler(HMI_MSG_ACK, on_ack);
    hmi_comm_set_handler(HMI_MSG_SET_SVPWM, on_set_svpwm);

    LOG_INF("uart_comm ready: B1 sends SET_GAUGE, H743 buttons send SET_SVPWM, "
        "host on Arduino D1(PC4)/D0(PC5) @115200");
    return 0;
}
