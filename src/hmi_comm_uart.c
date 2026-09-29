/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * UART backend for the HMI communication subsystem.
 *
 * Active when the `zephyr,comm` chosen node has compatible
 * "hmi,comm-uart". The UART device is taken from the `uart` phandle
 * property of that node.
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>

#include <hmi/comm/hmi_comm.h>
#include <hmi/comm/hmi_comm_backend.h>

LOG_MODULE_REGISTER(hmi_comm_uart, CONFIG_HMI_COMM_LOG_LEVEL);

#define COMM_NODE       DT_CHOSEN(zephyr_comm)
#define COMM_UART_NODE  DT_PHANDLE(COMM_NODE, uart)

static const struct device *uart_dev;

/* --- temporary bring-up diagnostics (remove once the link is closed) ---
 *
 * Kept deliberately cheap: printing a line takes the printk lock, which
 * disables interrupts for the whole format-and-transmit. At 115200 a 300
 * character line is ~26 ms of interrupts off, so heavy logging can starve the
 * UART receive interrupt and cause the very overruns we are looking for.
 * Report every DIAG_PERIOD_MS and log almost nothing.
 */
#define DIAG_PERIOD_MS 5000u
#define USART_ISR_OFF  0x1cu
#define USART_ICR_OFF  0x20u
#define USART_ERR_MASK 0x0fu   /* PE | FE | NE | ORE */
#define USART_ORE      0x08u
#define USART_FENE     0x06u   /* FE | NE */

static volatile uint32_t diag_rx_bytes;
static volatile uint32_t diag_tx_bytes;
static volatile uint32_t diag_ore;
static volatile uint32_t diag_fene;
static volatile uint32_t diag_max_gap_ms;
static uint32_t diag_last_irq_ms;

static void diag_log(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(diag_work, diag_log);

static void diag_log(struct k_work *work)
{
    uint32_t base = (uint32_t)DT_REG_ADDR(COMM_UART_NODE);

    ARG_UNUSED(work);

    /* Error flags are sampled and cleared in the ISR, not here: clearing them
     * on a timer would erase overruns before the ISR could count them.
     */
    LOG_INF("diag rxb=%u txb=%u ore=%u fene=%u gap=%ums ISR=%08x",
            diag_rx_bytes, diag_tx_bytes, diag_ore, diag_fene,
            diag_max_gap_ms, sys_read32(base + USART_ISR_OFF));

    hmi_comm_diag_log();

    k_work_schedule(&diag_work, K_MSEC(DIAG_PERIOD_MS));
}

/* Pop and transmit bytes one at a time, only while the UART accepts them.
 * On FIFO-less STM32 UARTs/LPUARTs uart_fifo_fill() takes a single byte per
 * TXE window, so popping a whole frame chunk into a local buffer and getting
 * a partial fill would silently drop the un-written bytes. Popping exactly
 * one byte per TXE window keeps every byte in the ring until it is shifted
 * out; the next TXE interrupt drains the remainder (TX stays enabled while
 * the ring is non-empty).
 */
static void uart_tx_drain(const struct device *dev)
{
    uint8_t b;
    int filled = 0;

    while (uart_irq_tx_ready(dev)) {
        if (hmi_comm_tx_pop(&b, 1) != 1) {
            uart_irq_tx_disable(dev);
            break;
        }
        filled += uart_fifo_fill(dev, &b, 1);
    }

    diag_tx_bytes += filled;
}

static void uart_irq_handler(const struct device *dev, void *user_data)
{
    uint32_t base = (uint32_t)DT_REG_ADDR(COMM_UART_NODE);
    uint32_t isr;
    uint32_t now;
    uint8_t buf[32];
    int n;

    (void)user_data;

    /* At ~52 byte/s the RX interrupt should recur every 1-2 ms. A much larger
     * gap means this ISR was not getting CPU, which starves the receive
     * register and loses bytes.
     */
    now = (uint32_t)k_uptime_get();
    if (diag_last_irq_ms != 0u) {
        uint32_t gap = now - diag_last_irq_ms;

        if (gap > diag_max_gap_ms) {
            diag_max_gap_ms = gap;
        }
    }
    diag_last_irq_ms = now;

    /* Sample before uart_fifo_read(): the driver clears ORE inside its read
     * loop, so an overrun is only observable here. ORE means a byte was
     * received while RDR was still full, i.e. a byte was genuinely lost.
     */
    isr = sys_read32(base + USART_ISR_OFF);
    if ((isr & USART_ERR_MASK) != 0u) {
        if ((isr & USART_ORE) != 0u) {
            diag_ore++;
        }
        if ((isr & USART_FENE) != 0u) {
            diag_fene++;
        }
        sys_write32(USART_ERR_MASK, base + USART_ICR_OFF);
    }

    uart_irq_update(dev);

    if (uart_irq_rx_ready(dev)) {
        while ((n = uart_fifo_read(dev, buf, sizeof(buf))) > 0) {
            diag_rx_bytes += n;
            hmi_comm_rx(buf, n);
        }
    }

    uart_tx_drain(dev);
}

static void backend_tx_kick(void)
{
    if (uart_dev == NULL) {
        return;
    }

    /* Only arm the TX interrupt here. hmi_comm_send() is called from the RX
     * thread and from the telemetry timer ISR, so draining the ring inline
     * would race the driver ISR: the TXE check and the TDR write are not
     * atomic across contexts, and a byte written while TXE is clear is
     * overwritten in TDR. Letting the driver ISR do the draining keeps the
     * UART owned by a single context.
     */
    uart_irq_tx_enable(uart_dev);
}

static int backend_init(void)
{
    if (!DT_NODE_HAS_COMPAT(COMM_NODE, hmi_comm_uart)) {
        return -ENODEV;
    }

    uart_dev = DEVICE_DT_GET(COMM_UART_NODE);
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("comm uart device %s not ready", uart_dev->name);
        return -ENODEV;
    }

    uart_irq_callback_user_data_set(uart_dev, uart_irq_handler, NULL);
    uart_irq_rx_enable(uart_dev);

    k_work_schedule(&diag_work, K_MSEC(DIAG_PERIOD_MS));

    LOG_INF("comm on %s", uart_dev->name);

    return 0;
}

const struct hmi_comm_backend hmi_comm_uart_backend = {
    .name = "uart",
    .init = backend_init,
    .tx_kick = backend_tx_kick,
};
