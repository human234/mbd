#pragma once

/**
 * uart_comm.h -- HMI comm host peer (NUCLEO-G474RE)
 *
 * Talks to the H743 svpwm_comm device role (comm_device.c) using the real
 * HMI comm subsystem backend (hmi_comm.c + hmi_comm_uart.c from the HMI
 * module). Both sides connect on their Arduino D0/D1 header serial:
 *   G474 usart1 D1(PC4 TX)/D0(PC5 RX) <-> H743 lpuart1 D1(PB6 TX)/D0(PB7 RX).
 * This side acts as the host: drives the HMI gauge with a swept SET_GAUGE,
 * probes with PING/GET_INFO and watches the H743's telemetry stream.
 */

int uart_comm_start(void);