/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Communication core: byte rings, frame parser, RX thread, dispatch and
 * backend selection. Transport-agnostic — see hmi_comm_backend.h.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <hmi/comm/hmi_comm.h>
#include <hmi/comm/hmi_comm_backend.h>
#include <hmi/comm/hmi_comm_proto.h>

LOG_MODULE_REGISTER(hmi_comm, CONFIG_HMI_COMM_LOG_LEVEL);

/* -------------------------------------------------------------------------
 * Compile-time backend list. Add new backends here (gated by Kconfig).
 * ----------------------------------------------------------------------- */
#if defined(CONFIG_HMI_COMM_UART)
extern const struct hmi_comm_backend hmi_comm_uart_backend;
#endif

static const struct hmi_comm_backend *backends[] = {
#if defined(CONFIG_HMI_COMM_UART)
    &hmi_comm_uart_backend,
#endif
};

static const struct hmi_comm_backend *active_backend;

/* -------------------------------------------------------------------------
 * Lock-free-safe byte rings (spinlock; usable from ISR and threads).
 * ----------------------------------------------------------------------- */
struct byte_ring {
    struct k_spinlock lock;
    size_t rd;
    size_t wr;
    size_t cnt;
    size_t cap;
    uint8_t *buf;
};

static struct byte_ring rx_ring;
static struct byte_ring tx_ring;

static uint8_t rx_bytes[CONFIG_HMI_COMM_RX_RING_SIZE];
static uint8_t tx_bytes[CONFIG_HMI_COMM_TX_RING_SIZE];

static void ring_init(struct byte_ring *r, uint8_t *buf, size_t cap)
{
    r->buf = buf;
    r->cap = cap;
}

static size_t ring_put(struct byte_ring *r, const uint8_t *data, size_t len)
{
    k_spinlock_key_t key = k_spin_lock(&r->lock);
    size_t i = 0;

    while (i < len && r->cnt < r->cap) {
        r->buf[r->wr] = data[i];
        r->wr = (r->wr + 1) % r->cap;
        r->cnt++;
        i++;
    }
    k_spin_unlock(&r->lock, key);

    return i;
}

static size_t ring_get(struct byte_ring *r, uint8_t *data, size_t len)
{
    k_spinlock_key_t key = k_spin_lock(&r->lock);
    size_t i = 0;

    while (i < len && r->cnt > 0) {
        data[i] = r->buf[r->rd];
        r->rd = (r->rd + 1) % r->cap;
        r->cnt--;
        i++;
    }
    k_spin_unlock(&r->lock, key);

    return i;
}

static bool ring_empty(struct byte_ring *r)
{
    k_spinlock_key_t key = k_spin_lock(&r->lock);
    bool empty = (r->cnt == 0);

    k_spin_unlock(&r->lock, key);

    return empty;
}

/* -------------------------------------------------------------------------
 * Frame parser (single caller: RX thread).
 * ----------------------------------------------------------------------- */
enum fstate {
    FS_SYNC,  /* hunting for 0xA5 */
    FS_LEN,   /* payload length byte */
    FS_DATA,  /* id + payload */
    FS_CRC,   /* two CRC bytes (little-endian) */
};

static enum fstate fstate;
static uint8_t fw_len;         /* payload length byte */
static uint8_t fw_pos;         /* bytes of id+payload collected */
static uint16_t fw_crc;        /* running CRC over len+id+payload */
static uint16_t fw_crc_rx;     /* received CRC */
static uint8_t fw_ncrc;        /* CRC bytes collected (0..2) */
static uint8_t frame_data[HMI_COMM_MAX_PAYLOAD + 1]; /* id + payload */

/* Temporary bring-up diagnostics (remove once the link is characterised). */
static volatile uint32_t diag_frames_ok;
static volatile uint32_t diag_frames_crc;
static volatile uint32_t diag_dispatched;
static volatile uint32_t diag_no_handler;
static volatile uint32_t diag_len_reject;

static void parser_reset(void)
{
    fstate = FS_SYNC;
    fw_len = 0;
    fw_pos = 0;
    fw_crc = HMI_COMM_CRC_INIT;
    fw_crc_rx = 0;
    fw_ncrc = 0;
}

/* -------------------------------------------------------------------------
 * Handler table (dispatched from RX thread context).
 * ----------------------------------------------------------------------- */
struct comm_handler {
    uint8_t id;
    void (*cb)(uint8_t id, const uint8_t *data, size_t len);
};

static struct comm_handler handlers[CONFIG_HMI_COMM_MAX_HANDLERS];

static void dispatch(uint8_t id, const uint8_t *data, size_t len)
{
    int i;

    diag_dispatched++;
    for (i = 0; i < CONFIG_HMI_COMM_MAX_HANDLERS; i++) {
        if (handlers[i].cb != NULL && handlers[i].id == id) {
            handlers[i].cb(id, data, len);
            return;
        }
    }
    diag_no_handler++;
    LOG_DBG("no handler for msg 0x%02x", id);
}

/* -------------------------------------------------------------------------
 * RX path.
 * ----------------------------------------------------------------------- */
static struct k_sem rx_sem;

int hmi_comm_rx(const uint8_t *data, size_t len)
{
    size_t put = ring_put(&rx_ring, data, len);

    if (put != len) {
        /* Headroom loss only; the parser resyncs on the next sync byte. */
        LOG_WRN("rx ring overflow (%u bytes dropped)",
            (unsigned int)(len - put));
    }
    k_sem_give(&rx_sem);

    return 0;
}

static void feed_byte(uint8_t b)
{
    switch (fstate) {
    case FS_SYNC:
        if (b == HMI_COMM_SYNC) {
            fstate = FS_LEN;
        }
        break;
    case FS_LEN:
        if (b > HMI_COMM_MAX_PAYLOAD) {
            diag_len_reject++;
            parser_reset();
            break;
        }
        fw_len = b;
        fw_pos = 0;
        fw_crc = HMI_COMM_CRC_INIT;
        fw_crc = hmi_comm_crc16(fw_crc, &b, 1);
        fstate = FS_DATA;
        break;
    case FS_DATA:
        if (fw_pos < sizeof(frame_data)) {
            frame_data[fw_pos] = b;
        }
        fw_pos++;
        fw_crc = hmi_comm_crc16(fw_crc, &b, 1);
        if (fw_pos >= (uint8_t)(fw_len + 1)) {
            fw_ncrc = 0;
            fw_crc_rx = 0;
            fstate = FS_CRC;
        }
        break;
    case FS_CRC:
        if (fw_ncrc == 0) {
            fw_crc_rx = b;
        } else {
            fw_crc_rx |= (uint16_t)b << 8;
        }
        fw_ncrc++;
        if (fw_ncrc == 2) {
            if (fw_crc_rx == fw_crc) {
                diag_frames_ok++;
                dispatch(frame_data[0], &frame_data[1], fw_len);
            } else {
                diag_frames_crc++;
                LOG_DBG("crc mismatch (0x%04x != 0x%04x)",
                    fw_crc_rx, fw_crc);
            }
            parser_reset();
        }
        break;
    }
}

static void rx_entry(void *p1, void *p2, void *p3)
{
    uint8_t chunk[32];
    size_t n;
    size_t i;

    while (1) {
        k_sem_take(&rx_sem, K_FOREVER);
        while (!ring_empty(&rx_ring)) {
            n = ring_get(&rx_ring, chunk, sizeof(chunk));
            for (i = 0; i < n; i++) {
                feed_byte(chunk[i]);
            }
        }
    }
}

K_THREAD_STACK_DEFINE(rx_stack, CONFIG_HMI_COMM_RX_THREAD_STACK);
static struct k_thread rx_thread;

/* -------------------------------------------------------------------------
 * TX path.
 * ----------------------------------------------------------------------- */
size_t hmi_comm_tx_pop(uint8_t *buf, size_t len)
{
    return ring_get(&tx_ring, buf, len);
}

bool hmi_comm_tx_empty(void)
{
    return ring_empty(&tx_ring);
}

/* Temporary bring-up diagnostics (remove once the link is characterised).
 * Kept short: printing holds the printk lock with interrupts disabled. */
void hmi_comm_diag_log(void)
{
    LOG_INF("diag2 ok=%u crc=%u lenrej=%u",
            diag_frames_ok, diag_frames_crc, diag_len_reject);
}

int hmi_comm_send(uint8_t id, const void *payload, size_t len)
{
    uint8_t frame[HMI_COMM_MAX_FRAME];
    uint16_t crc;
    size_t total;

    if (len > HMI_COMM_MAX_PAYLOAD) {
        return -EMSGSIZE;
    }
    if (active_backend == NULL) {
        return -ENODEV;
    }

    frame[0] = HMI_COMM_SYNC;
    frame[1] = (uint8_t)len;
    frame[2] = id;
    if (len > 0 && payload != NULL) {
        memcpy(&frame[3], payload, len);
    }
    crc = hmi_comm_crc16(HMI_COMM_CRC_INIT, &frame[1], len + 2);
    frame[len + 3] = (uint8_t)crc;
    frame[len + 4] = (uint8_t)(crc >> 8);
    total = len + 5;

    if (ring_put(&tx_ring, frame, total) != total) {
        LOG_WRN("tx ring overflow, frame dropped");
        return -ENOBUFS;
    }

    active_backend->tx_kick();

    return 0;
}

/* -------------------------------------------------------------------------
 * Handlers.
 * ----------------------------------------------------------------------- */
int hmi_comm_set_handler(uint8_t id,
             void (*cb)(uint8_t id, const uint8_t *data, size_t len))
{
    int i;
    int free_slot = -1;

    for (i = 0; i < CONFIG_HMI_COMM_MAX_HANDLERS; i++) {
        if (handlers[i].cb == NULL) {
            if (free_slot < 0) {
                free_slot = i;
            }
            continue;
        }
        if (handlers[i].id == id) {
            handlers[i].cb = cb;
            return 0;
        }
    }
    if (cb == NULL) {
        return 0;   /* unregistering a handler that was not set */
    }
    if (free_slot < 0) {
        return -ENOSPC;
    }
    handlers[free_slot].id = id;
    handlers[free_slot].cb = cb;

    return 0;
}

/* -------------------------------------------------------------------------
 * Init.
 * ----------------------------------------------------------------------- */
int hmi_comm_init(void)
{
    int i;

    ring_init(&rx_ring, rx_bytes, sizeof(rx_bytes));
    ring_init(&tx_ring, tx_bytes, sizeof(tx_bytes));
    k_sem_init(&rx_sem, 0, 1);
    parser_reset();
    memset(handlers, 0, sizeof(handlers));

    k_thread_create(&rx_thread, rx_stack, sizeof(rx_stack),
            rx_entry, NULL, NULL, NULL,
            CONFIG_HMI_COMM_RX_THREAD_PRIO, K_INHERIT_PERMS, K_NO_WAIT);

    for (i = 0; i < ARRAY_SIZE(backends); i++) {
        if (backends[i]->init() == 0) {
            active_backend = backends[i];
            LOG_INF("comm backend active: %s", active_backend->name);
            return 0;
        }
    }

    LOG_ERR("no comm backend found (set zephyr,comm in the devicetree)");

    return -ENODEV;
}
