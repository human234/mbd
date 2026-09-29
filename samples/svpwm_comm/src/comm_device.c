/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * comm_device.c -- HMI comm device peer (see comm_device.h). Answers host
 * requests with PONG/INFO/ACK, and originates SET_SVPWM for the on-screen
 * buttons; the SET_GAUGE value drives the on-screen needle through the demo
 * gauge value shared with the HMI library.
 *
 * No periodic traffic: the 500 ms telemetry stream is gone so that the
 * device->host path carries only what a button press asked for. That makes
 * each press one countable frame out and one ACK back, which is what the
 * sent/acked counters below are for.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <hmi/comm/hmi_comm.h>
#include <hmi/comm/hmi_comm_proto.h>
#include <hmi/hmi_gauge.h>

#include "comm_device.h"

LOG_MODULE_REGISTER(comm_device, LOG_LEVEL_INF);

#define INFO_MAJOR           0u
#define INFO_MINOR           2u

/* --- peer message handlers (comm RX thread context) --- */

static void on_ping(uint8_t id, const uint8_t *data, size_t len)
{
    ARG_UNUSED(id);
    printk("pong\n");
    /* Echo the host's payload back as the PONG. */
    if (hmi_comm_send(HMI_MSG_PONG, data, len) == 0) {
        LOG_INF("PING rx -> PONG (%u B)", (uint32_t)len);
    }
}

static void on_get_info(uint8_t id, const uint8_t *data, size_t len)
{
    ARG_UNUSED(id);
    static const char name[] = "svpwm_comm device";
    uint8_t info[HMI_COMM_MAX_PAYLOAD];

    ARG_UNUSED(data);
    ARG_UNUSED(len);

    info[0] = INFO_MAJOR;
    info[1] = INFO_MINOR;
    memcpy(&info[2], name, sizeof(name)); /* incl. NUL */
    if (hmi_comm_send(HMI_MSG_INFO, info, 2 + sizeof(name)) == 0) {
        LOG_INF("GET_INFO rx -> INFO");
    }
}

static void on_set_gauge(uint8_t id, const uint8_t *data, size_t len)
{
    ARG_UNUSED(id);
    uint8_t ack[2];

    if (len != 5) {  /* u8 idx + u32 value */
        LOG_WRN("SET_GAUGE bad len %u", (uint32_t)len);
        ack[0] = HMI_MSG_SET_GAUGE;
        ack[1] = 1;  /* status error */
        hmi_comm_send(HMI_MSG_ACK, ack, sizeof(ack));
        return;
    }

    hmi_gauge_demo_set((float)hmi_comm_get_u32(&data[1]));

    ack[0] = HMI_MSG_SET_GAUGE;
    ack[1] = 0;  /* status ok */
    if (hmi_comm_send(HMI_MSG_ACK, ack, sizeof(ack)) == 0) {
        LOG_INF("SET_GAUGE idx %u value %u -> ACK",
            data[0], hmi_comm_get_u32(&data[1]));
    }
}

/* --- SET_SVPWM round trip (host echo of what it applied) --- */

/* Written on the comm RX thread by on_ack(), read by the UI thread through
 * comm_device_get_svpwm_ack(). The fields are plain scalars with no lock: a
 * read that races an arriving ack can mix an old value with a new counter.
 * That is harmless here (the UI re-reads every 100 ms and self-corrects) and
 * is far cheaper than locking a display update against a comm ISR path.
 */
static struct {
	uint32_t sent;
	uint32_t acked;
	uint16_t freq_x10;
	uint16_t volt_x100;
	uint8_t  status;
} svpwm_ack;

static void on_ack(uint8_t id, const uint8_t *data, size_t len)
{
	ARG_UNUSED(id);

	if (len < HMI_ACK_SIZE || data[0] != HMI_MSG_SET_SVPWM) {
		return;   /* not ours (e.g. a SET_GAUGE ack echo) */
	}

	if (len >= HMI_ACK_SVPWM_SIZE) {
		svpwm_ack.freq_x10  = hmi_comm_get_u16(&data[2]);
		svpwm_ack.volt_x100 = hmi_comm_get_u16(&data[4]);
	}
	svpwm_ack.status = data[1];
	svpwm_ack.acked++;

	LOG_INF("SET_SVPWM ack st %u -> f=%u.%u Hz v=%u.%02u V (%u/%u)",
	    data[1],
	    svpwm_ack.freq_x10 / 10, svpwm_ack.freq_x10 % 10,
	    svpwm_ack.volt_x100 / 100, svpwm_ack.volt_x100 % 100,
	    svpwm_ack.acked, svpwm_ack.sent);
}

void comm_device_send_svpwm(uint16_t freq_x10, uint16_t volt_x100)
{
	uint8_t payload[HMI_SVPWM_PAYLOAD_SIZE];

	hmi_comm_put_u16(&payload[0], freq_x10);
	hmi_comm_put_u16(&payload[2], volt_x100);

	if (hmi_comm_send(HMI_MSG_SET_SVPWM, payload, sizeof(payload)) == 0) {
		svpwm_ack.sent++;
		LOG_INF("ui -> SET_SVPWM f=%u.%u Hz v=%u.%02u V (%u)",
		    freq_x10 / 10, freq_x10 % 10,
		    volt_x100 / 100, volt_x100 % 100,
		    svpwm_ack.sent);
	} else {
		LOG_WRN("SET_SVPWM send failed");
	}
}

void comm_device_get_svpwm_ack(comm_device_svpwm_ack_t *out)
{
	if (out == NULL) {
		return;
	}

	out->sent      = svpwm_ack.sent;
	out->acked     = svpwm_ack.acked;
	out->freq_x10  = svpwm_ack.freq_x10;
	out->volt_x100 = svpwm_ack.volt_x100;
	out->status    = svpwm_ack.status;
	out->valid     = svpwm_ack.acked > 0;
}

/* --- start --- */

void comm_device_start(void)
{
	if (hmi_comm_init() < 0) {
		LOG_ERR("comm init failed");
		return;
	}

	hmi_comm_set_handler(HMI_MSG_PING, on_ping);
	hmi_comm_set_handler(HMI_MSG_GET_INFO, on_get_info);
	hmi_comm_set_handler(HMI_MSG_SET_GAUGE, on_set_gauge);
	hmi_comm_set_handler(HMI_MSG_ACK, on_ack);

	LOG_INF("comm device started: lpuart1 on Arduino D1(PB6)/D0(PB7) @115200");
}

