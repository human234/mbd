/*
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HMI communication wire protocol (transport-agnostic).
 *
 * Frame:  0xA5 | len(1) | id(1) | payload(len) | crc16-ccitt(2, little-endian)
 *
 * The CRC covers len + id + payload (init 0xFFFF, poly 0x1021, non-reflected).
 * Response message ids mirror command ids with HMI_COMM_RSP_BIT set, so new
 * commands only require one new id each way.
 */

#define HMI_COMM_SYNC          0xA5u
#define HMI_COMM_MAX_PAYLOAD   64u
#define HMI_COMM_MAX_FRAME     (1u + 1u + 1u + HMI_COMM_MAX_PAYLOAD + 2u)
#define HMI_COMM_CRC_INIT      0xFFFFu
#define HMI_COMM_CRC_POLY      0x1021u

#define HMI_COMM_RSP(id)       ((uint8_t)((id) | 0x80u))

/* Message ids. 0x01..0x3F requests, 0x80+ responses. The range is NOT a
 * direction marker: either peer may originate a request, and each board only
 * registers the ids it expects to receive. HMI_MSG_SET_GAUGE is G474->H743,
 * HMI_MSG_SET_SVPWM is H743->G474.
 */
enum {
	HMI_MSG_PING       = 0x01,
	HMI_MSG_GET_INFO   = 0x02,
	HMI_MSG_SET_GAUGE  = 0x03,
	HMI_MSG_SET_SVPWM  = 0x04,

	HMI_MSG_PONG       = HMI_COMM_RSP(HMI_MSG_PING),      /* 0x81 */
	HMI_MSG_INFO       = HMI_COMM_RSP(HMI_MSG_GET_INFO),  /* 0x82 */
	HMI_MSG_ACK        = 0x83,

	HMI_MSG_TELEMETRY  = 0x80,   /* reserved, unused by the svpwm_comm sample */
};

/* TELEMETRY payload on the wire: u8 gauge_idx | u32 uptime_ms | u32 value */
#define HMI_TELEMETRY_SIZE 9u

/* SET_SVPWM payload: u16 freq_x10 (Hz * 10) | u16 volt_x100 (V * 100).
 * Frequency is scaled by 10 rather than 100 so the full 4000 Hz range fits a
 * u16 (40000); scaling by 100 would overflow at 400000.
 */
#define HMI_SVPWM_PAYLOAD_SIZE 4u

/* Accepted SET_SVPWM ranges, enforced by both peers. The voltage ceiling is
 * the inverter DC bus divided by two, which keeps the SVPWM modulation index
 * (2 * v_ref / vdc) at or below 1.
 */
#define HMI_SVPWM_FREQ_MAX_X10   40000u   /* 4000.0 Hz */
#define HMI_SVPWM_VOLT_MAX_X100  500u     /* 5.00 V */

/* ACK payload: u8 for_id | u8 status. A SET_SVPWM ack appends the values the
 * host actually applied, so the device can display the host's own echo rather
 * than merely the value it asked for.
 */
#define HMI_ACK_SIZE          2u
#define HMI_ACK_SVPWM_SIZE    (HMI_ACK_SIZE + HMI_SVPWM_PAYLOAD_SIZE)

static inline uint16_t hmi_comm_crc16(uint16_t crc, const uint8_t *data, size_t len)
{
	size_t i;
	int bit;

	for (i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (bit = 0; bit < 8; bit++) {
			crc = (crc & 0x8000U) ? ((crc << 1) ^ HMI_COMM_CRC_POLY) : (crc << 1);
		}
	}

	return crc;
}

static inline void hmi_comm_put_u16(uint8_t *buf, uint16_t v)
{
	buf[0] = (uint8_t)v;
	buf[1] = (uint8_t)(v >> 8);
}

static inline void hmi_comm_put_u32(uint8_t *buf, uint32_t v)
{
	buf[0] = (uint8_t)v;
	buf[1] = (uint8_t)(v >> 8);
	buf[2] = (uint8_t)(v >> 16);
	buf[3] = (uint8_t)(v >> 24);
}

static inline uint16_t hmi_comm_get_u16(const uint8_t *buf)
{
	return (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

static inline uint32_t hmi_comm_get_u32(const uint8_t *buf)
{
	return ((uint32_t)buf[0]) |
	       ((uint32_t)buf[1] << 8) |
	       ((uint32_t)buf[2] << 16) |
	       ((uint32_t)buf[3] << 24);
}

static inline int32_t hmi_comm_get_i32(const uint8_t *buf)
{
	return (int32_t)hmi_comm_get_u32(buf);
}

#ifdef __cplusplus
}
#endif