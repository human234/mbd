/*
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * HMI communication subsystem — transport-agnostic message transport.
 *
 * The core does framing (see hmi_comm_proto.h), a TX byte ring, an RX byte
 * ring drained by a dedicated thread, and dispatch of complete frames to
 * registered handlers by message id. Hardware is abstracted behind the
 * backend interface (hmi_comm_backend.h), selected by the `zephyr,comm`
 * chosen devicetree node.
 */

int hmi_comm_init(void);

/* Queue a frame (id + payload) for the active backend. Never blocks; the
 * backend drains the TX ring from interrupt context.
 */
int hmi_comm_send(uint8_t id, const void *payload, size_t len);

/* Register/unregister the handler for a message id. Replaces any previous
 * handler for the same id. cb is invoked from the comm RX thread.
 */
int hmi_comm_set_handler(uint8_t id,
			 void (*cb)(uint8_t id, const uint8_t *data, size_t len));

/* --- called by backends only --- */

/* Deliver received bytes to the core. Safe from ISR context. */
int hmi_comm_rx(const uint8_t *data, size_t len);

/* Drain up to len bytes from the TX ring (safe from ISR context).
 * Returns the number of bytes actually removed.
 */
size_t hmi_comm_tx_pop(uint8_t *buf, size_t len);

/* True when no TX bytes are queued. */
bool hmi_comm_tx_empty(void);

/* Temporary bring-up diagnostics (remove once the link is characterised). */
void hmi_comm_diag_log(void);

#ifdef __cplusplus
}
#endif