/*
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Communication backend interface.
 *
 * A backend owns a devicetree node found via the `zephyr,comm` chosen node and
 * selected by its *compatible* string (e.g. "hmi,comm-uart"). The core calls
 * init() on every compiled-in backend; the first one that returns 0 owns the
 * link. Only compiled-in backends are probed, so adding a backend is just a
 * Kconfig + a new .c file.
 *
 * Byte transport duties:
 *  - give RX bytes to the core by calling hmi_comm_rx() (ISR-safe);
 *  - call tx_kick() when new TX data is queued so it can start draining it
 *    with hmi_comm_tx_pop() / hmi_comm_tx_empty().
 */

struct hmi_comm_backend {
	const char *name;
	int (*init)(void);            /* 0 if this backend owns the chosen node */
	void (*tx_kick)(void);        /* start pushing queued TX bytes out */
};

#ifdef __cplusplus
}
#endif