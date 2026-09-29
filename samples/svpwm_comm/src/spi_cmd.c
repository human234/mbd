/*
 * spi_cmd.c -- SPI command link (master side, NUCLEO-G474RE)
 *
 * Receives SVPWM reference commands (electrical frequency + voltage
 * magnitude) from the H7S78-DK SPI4 slave.
 *
 * Transport: SPI1 master (SCK=PA5, MISO=PA6, MOSI=PA7, CS=PB6 GPIO).  The
 * slave drives the SVPWM control by asserting a notification GPIO (PC7) when
 * it has a new command ready, at which point this module clocks an 8-byte
 * frame from the slave and applies it.
 *
 * Frame (both directions, master TX always 8 bytes of 0x00):
 *   [0]  magic   0xAA
 *   [1]  cmd     0x01 = set reference
 *   [2]  freq_hi freq*100 big-endian high byte
 *   [3]  freq_lo freq*100 big-endian low  byte
 *   [4]  mag_hi  mag*100  big-endian high byte
 *   [5]  mag_lo  mag*100  big-endian low  byte
 *   [6]  crc     XOR of bytes [0..5]
 *   [7]  tail    0x55
 */

#include <stdint.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <zephyr/sys/byteorder.h>

#include "control.h"
#include "spi_cmd.h"

LOG_MODULE_REGISTER(spi_cmd, LOG_LEVEL_INF);

#define SPI_DEV_NODE   DT_NODELABEL(spi1)
#define NOTIFY_NODE    DT_NODELABEL(gpioc)
#define NOTIFY_PIN     7u          /* PC7: slave PD12 -> master PC7 */

#define CMD_FRAME_LEN  8u
#define CMD_MAGIC      0xAAu
#define CMD_SET_REF    0x01u
#define CMD_START      0x02u
#define CMD_STOP       0x03u
#define CMD_TAIL       0x55u

#define CMD_STACK_SIZE 1024u
#define CMD_PRIO       1u

static const struct device *spi_dev;
static const struct device *notify_dev;

static struct gpio_callback notify_cb;
static K_SEM_DEFINE(cmd_sem, 0, 1);

K_THREAD_STACK_DEFINE(cmd_stack, CMD_STACK_SIZE);
static struct k_thread cmd_thread_data;

static uint8_t tx_buf[CMD_FRAME_LEN];
static uint8_t rx_buf[CMD_FRAME_LEN];
static struct spi_buf tx_spi_buf = { .buf = tx_buf, .len = CMD_FRAME_LEN };
static struct spi_buf rx_spi_buf = { .buf = rx_buf, .len = CMD_FRAME_LEN };
static const struct spi_buf_set tx = { .buffers = &tx_spi_buf, .count = 1 };
static const struct spi_buf_set rx = { .buffers = &rx_spi_buf, .count = 1 };

/* Master, 8-bit, MSB-first, mode 0 (CPOL=0, CPHA=0), GPIO chip-select PB6. */
static const struct spi_config spi_cfg = {
    .frequency = 1000000U,
    .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8) | SPI_TRANSFER_MSB,
    .slave = 0,
    .cs = {
        .gpio = {
            .port = DEVICE_DT_GET(DT_NODELABEL(gpiob)),
            .pin = 6,
            .dt_flags = GPIO_ACTIVE_LOW,
        },
    },
};

static void notify_cb_isr(const struct device *dev, struct gpio_callback *cb,
              uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    k_sem_give(&cmd_sem);
}

static uint8_t frame_crc(const uint8_t *f)
{
    uint8_t c = 0;
    for (int i = 0; i < 6; i++) {
        c ^= f[i];
    }
    return c;
}

static int handle_frame(const uint8_t *f)
{
    /* Validate envelope/CRC. */
    if (f[0] != CMD_MAGIC || f[7] != CMD_TAIL || f[6] != frame_crc(f)) {
        LOG_WRN("bad frame: magic=%02X tail=%02X crc=%02X/%02X",
            f[0], f[7], f[6], frame_crc(f));
        return -EINVAL;
    }

    /* Big-endian value*100. */
    uint16_t f100 = ((uint16_t)f[2] << 8) | f[3];
    uint16_t m100 = ((uint16_t)f[4] << 8) | f[5];

    float freq = (float)f100 / 100.0f;   /* Hz */
    float mag  = (float)m100  / 100.0f;  /* V */

    /* Sanity limits. */
    if (freq > 4000.0f || mag > 50.0f) {
        LOG_WRN("out-of-range freq=%u mag=%u", f100, m100);
        return -ERANGE;
    }

    /* Always apply the reference so START/STOP frames can carry it too. */
    set_frequency(freq);
    set_magnitude(mag);

    /* Act on the command byte. START/STOP are persistent per-loop frame state
     * from the slave, so they reliably toggle the timer on and off. */
    switch (f[1]) {
    case CMD_SET_REF:
    case CMD_START:
        svpwm_start();
        LOG_INF("ref start: f=%.2f Hz, v=%.2f V", freq, mag);
        break;

    case CMD_STOP:
        svpwm_stop();
        LOG_INF("ref stop: f=%.2f Hz, v=%.2f V", freq, mag);
        break;

    default:
        LOG_WRN("unknown cmd 0x%02X", f[1]);
        return -EINVAL;
    }

    return 0;
}

static void cmd_thread(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    /* Poison the TX payload; only RX carries data. */
    memset(tx_buf, 0, sizeof(tx_buf));

    while (1) {
        /* Wake immediately on a notify edge from the slave (PD12 -> PC7). */
        k_sem_take(&cmd_sem, K_FOREVER);

        int ret = spi_transceive(spi_dev, &spi_cfg, &tx, &rx);
        if (ret != 0) {
            LOG_ERR("spi_transceive failed: %d", ret);
            continue;
        }

        handle_frame(rx_buf);
    }
}

int spi_cmd_start(void)
{
    spi_dev = DEVICE_DT_GET(SPI_DEV_NODE);
    if (!device_is_ready(spi_dev)) {
        LOG_ERR("spi1 not ready");
        return -ENODEV;
    }

    notify_dev = DEVICE_DT_GET(NOTIFY_NODE);
    if (!device_is_ready(notify_dev)) {
        LOG_ERR("gpioc not ready");
        return -ENODEV;
    }

    /* PC7 input pulled down; interrupt on rising edge (slave assert). */
    int ret = gpio_pin_configure(notify_dev, NOTIFY_PIN,
                                 GPIO_INPUT | GPIO_PULL_DOWN);
    if (ret != 0) {
        LOG_ERR("notify pin config failed: %d", ret);
        return ret;
    }

    gpio_init_callback(&notify_cb, notify_cb_isr, BIT(NOTIFY_PIN));
    ret = gpio_add_callback(notify_dev, &notify_cb);
    if (ret != 0) {
        LOG_ERR("gpio callback add failed: %d", ret);
        return ret;
    }

    ret = gpio_pin_interrupt_configure(
        notify_dev, NOTIFY_PIN,
        GPIO_INT_EDGE_RISING | GPIO_INT_ENABLE);
    if (ret != 0) {
        LOG_ERR("notify irq config failed: %d", ret);
        return ret;
    }

    k_thread_create(&cmd_thread_data, cmd_stack, CMD_STACK_SIZE,
                    cmd_thread, NULL, NULL, NULL, CMD_PRIO, 0, K_NO_WAIT);

    LOG_INF("spi_cmd start: spi1 master + PC7 notify ready");
    return 0;
}
