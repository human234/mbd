#pragma once

#include <stdbool.h>
#include <stdint.h>

/**
 * comm_device.h -- HMI comm device peer (NUCLEO-H743ZI, svpwm_comm device)
 *
 * The other end of the uart_comm host peer. It answers the host's requests
 * and originates one of its own:
 *
 *   PING       -> PONG      (payload echoed)
 *   GET_INFO   -> INFO      (ver major/minor + name string)
 *   SET_GAUGE  -> ACK       (for_id + status) and drives the demo gauge
 *   SET_SVPWM  -> ACK       (for_id + status + the applied freq/volt echo)
 *
 * The gauge value backing the needle is shared with the HMI display via
 * hmi_gauge_demo_get()/hmi_gauge_demo_set().
 *
 * There is no periodic traffic: SET_SVPWM goes out only when the on-screen
 * buttons ask for it, so one frame pair on the wire is one button press.
 */

/* Result of the most recent SET_SVPWM round trip, as echoed by the host.
 * Written by the comm RX thread, so the UI must poll it with
 * comm_device_get_svpwm_ack() rather than being called back.
 */
typedef struct {
	uint32_t sent;        /* SET_SVPWM frames handed to the UART */
	uint32_t acked;       /* SET_SVPWM acks received from the host */
	uint16_t freq_x10;    /* frequency the host reports it applied */
	uint16_t volt_x100;   /* magnitude the host reports it applied */
	uint8_t  status;      /* 0 = applied, non-zero = rejected */
	bool     valid;       /* false until the first ack arrives */
} comm_device_svpwm_ack_t;

void comm_device_start(void);

void comm_device_send_svpwm(uint16_t freq_x10, uint16_t volt_x100);
void comm_device_get_svpwm_ack(comm_device_svpwm_ack_t *out);
