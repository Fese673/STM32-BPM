/**
 * Frame protocol — RX state machine + TX encoder dla kanału ZEGAR↔STM32.
 *
 * Wire format: 0x00 | COBS([type][seq][len_lo][len_hi][payload...][crc16]) | 0x00
 * CRC: 16-bit CCITT nad bajtami [type..payload_ostatni].
 *
 * RX bajty: odbierane w ISR (frame_rx_on_byte) → ring buffer.
 * Ramki są dekodowane w frame_rx_process() wywoływanym z main loop.
 */

#include "comm/frame_protocol.h"
#include "comm/cobs.h"
#include <string.h>

/* ========================================================================== */
/* RX RING BUFFER (ISR -> main loop)                                          */
/* ========================================================================== */

#define RX_RING_SIZE  128U
#define RX_RING_MASK  (RX_RING_SIZE - 1U)

static volatile uint8_t  s_rx_ring[RX_RING_SIZE];
static volatile uint16_t s_rx_head;   /* write: ISR */
static volatile uint16_t s_rx_tail;   /* read : main */

/* ========================================================================== */
/* FRAME ASSEMBLY BUFFER (collect bytes until 0x00 delimiter)                */
/* ========================================================================== */

#define FRAME_MAX_RAW  (FRAME_HEADER_BYTES + FRAME_MAX_PAYLOAD + FRAME_CRC_BYTES)
#define FRAME_MAX_WIRE (FRAME_MAX_RAW + FRAME_MAX_RAW / 254U + 2U + 2U)

static uint8_t s_assembly[FRAME_MAX_WIRE];
static uint16_t s_assembly_len;

/* ========================================================================== */
/* HANDLERS                                                                   */
/* ========================================================================== */

#define FRAME_HANDLERS_COUNT 256U
static frame_handler_t s_handlers[FRAME_HANDLERS_COUNT];

/* ========================================================================== */
/* STATS                                                                      */
/* ========================================================================== */

volatile uint32_t g_frame_rx_frames_ok  = 0U;
volatile uint32_t g_frame_rx_frames_bad = 0U;
volatile uint32_t g_frame_tx_frames     = 0U;

/* ========================================================================== */
/* RX: ISR ENTRYPOINT                                                          */
/* ========================================================================== */

void frame_rx_on_byte(uint8_t b)
{
    uint16_t h  = s_rx_head;
    uint16_t nh = (uint16_t)(h + 1U);
    if (nh >= RX_RING_SIZE) nh = 0U;

    if (nh == s_rx_tail) {
        /* Ring overflow — odrzucamy bajt. */
        return;
    }

    s_rx_ring[h] = b;
    s_rx_head = nh;
}

/* ========================================================================== */
/* RX: PARSE ONE WIRE FRAME                                                    */
/* ========================================================================== */

/* Po udanym COBS decode wywołaj handler dla danego typu. */
static void dispatch_frame(const uint8_t *raw, uint16_t raw_len)
{
    if (raw_len < FRAME_HEADER_BYTES + FRAME_CRC_BYTES) {
        ++g_frame_rx_frames_bad;
        return;
    }

    uint8_t  type = raw[0];
    uint8_t  seq  = raw[1];
    uint16_t plen = (uint16_t)raw[2] | ((uint16_t)raw[3] << 8U);

    /* Oczekiwany rozmiar: 4 (header) + plen (payload) + 2 (CRC). */
    uint16_t expected = (uint16_t)(FRAME_HEADER_BYTES + plen + FRAME_CRC_BYTES);
    if (plen > FRAME_MAX_PAYLOAD || raw_len != expected) {
        ++g_frame_rx_frames_bad;
        return;
    }

    /* CRC: liczone nad bajtami [0..raw_len-3] (bez samych bajtów CRC). */
    uint16_t received_crc = (uint16_t)raw[raw_len - 2U]
                          | ((uint16_t)raw[raw_len - 1U] << 8U);
    uint16_t expected_crc = cobs_crc16(raw, (size_t)(raw_len - 2U));
    if (received_crc != expected_crc) {
        ++g_frame_rx_frames_bad;
        return;
    }

    ++g_frame_rx_frames_ok;

    /* Dispatch. */
    frame_handler_t h = s_handlers[type];
    if (h != NULL) {
        h(type, seq, &raw[FRAME_HEADER_BYTES], plen);
    }
}

/* Po napotkaniu delimiter 0x00 — spróbuj zdekodować assembly buffer. */
static void try_decode_assembly(void)
{
    if (s_assembly_len == 0U) {
        return;
    }

    /* Bajty [0..len-1] = ciało COBS (bez delimiterów). */
    uint8_t raw[FRAME_MAX_RAW];
    size_t  raw_len = cobs_decode(s_assembly, s_assembly_len, raw);
    if (raw_len == 0U) {
        ++g_frame_rx_frames_bad;
        return;
    }
    dispatch_frame(raw, (uint16_t)raw_len);
}

/* ========================================================================== */
/* RX: MAIN LOOP ENTRYPOINT                                                    */
/* ========================================================================== */

void frame_rx_process(void)
{
    while (s_rx_head != s_rx_tail) {
        uint8_t b = s_rx_ring[s_rx_tail];
        s_rx_tail = (uint16_t)((s_rx_tail + 1U) & RX_RING_MASK);

        if (b == 0x00U) {
            /* Delimiter — koniec ramki. */
            try_decode_assembly();
            s_assembly_len = 0U;
        } else {
            /* Bajt danych. */
            if (s_assembly_len < (uint16_t)sizeof(s_assembly)) {
                s_assembly[s_assembly_len++] = b;
            } else {
                /* Assembly overflow — zacznij od nowa. */
                s_assembly_len = 0U;
                ++g_frame_rx_frames_bad;
            }
        }
    }
}

/* ========================================================================== */
/* TX: ENCODE + SEND                                                           */
/* ========================================================================== */

bool frame_tx_send(uint8_t type, const uint8_t *payload, uint16_t len)
{
    if (len > FRAME_MAX_PAYLOAD) return false;
    if (len > 0U && payload == NULL) return false;

    /* 1. Zbuduj raw frame: header (4B) + payload + CRC (2B). */
    uint8_t raw[FRAME_MAX_RAW];
    uint8_t *p = raw;
    *p++ = type;
    *p++ = 0U;  /* seq: STM32 nie śledzi numerów sekwencyjnych */
    *p++ = (uint8_t)(len & 0xFFU);
    *p++ = (uint8_t)((len >> 8U) & 0xFFU);
    if (len > 0U) {
        memcpy(p, payload, len);
        p += len;
    }
    uint16_t crc = cobs_crc16(raw, (size_t)(p - raw));
    *p++ = (uint8_t)(crc & 0xFFU);
    *p++ = (uint8_t)((crc >> 8U) & 0xFFU);
    size_t raw_len = (size_t)(p - raw);

    /* 2. COBS encode. */
    uint8_t cobs_buf[FRAME_MAX_WIRE];
    size_t  cobs_len = cobs_encode(raw, raw_len, cobs_buf);

    /* 3. Dodaj delimitery 0x00. */
    uint8_t wire[FRAME_MAX_WIRE];
    wire[0] = 0x00U;
    memcpy(&wire[1U], cobs_buf, cobs_len);
    wire[1U + cobs_len] = 0x00U;
    size_t wire_len = cobs_len + 2U;

    /* 4. Wyślij przez uart1 (TX ring buffer). */
    bool ok = uart1_write(wire, (uint16_t)wire_len);
    if (ok) {
        ++g_frame_tx_frames;
    }
    return ok;
}

/* ========================================================================== */
/* TX: HELPERS                                                                 */
/* ========================================================================== */

bool frame_tx_ppg(int16_t diff)
{
    uint8_t payload[2];
    payload[0] = (uint8_t)(diff & 0xFFU);
    payload[1] = (uint8_t)((diff >> 8U) & 0xFFU);
    return frame_tx_send(FRAME_TYPE_PPG, payload, 2U);
}

bool frame_tx_bpm(uint8_t bpm, uint8_t spo2)
{
    uint8_t payload[2];
    payload[0] = bpm;
    payload[1] = spo2;
    return frame_tx_send(FRAME_TYPE_BPM, payload, 2U);
}

bool frame_tx_radar(uint8_t presence, uint16_t movDist, uint8_t movEnergy,
                    uint16_t statDist, uint8_t statEnergy, uint16_t detectDist,
                    const uint8_t *movGates, const uint8_t *statGates)
{
    uint8_t payload[26];
    payload[0] = presence;
    payload[1] = (uint8_t)(movDist & 0xFFU);
    payload[2] = (uint8_t)((movDist >> 8U) & 0xFFU);
    payload[3] = movEnergy;
    payload[4] = (uint8_t)(statDist & 0xFFU);
    payload[5] = (uint8_t)((statDist >> 8U) & 0xFFU);
    payload[6] = statEnergy;
    payload[7] = (uint8_t)(detectDist & 0xFFU);  /* detect < 800 cm, 1 B wystarcza */
    for (uint8_t g = 0U; g < 9U; g++) {
        payload[8U + g]  = (movGates  != NULL) ? movGates[g]  : 0U;
        payload[17U + g] = (statGates != NULL) ? statGates[g] : 0U;
    }
    return frame_tx_send(FRAME_TYPE_RADAR, payload, sizeof(payload));
}

bool frame_tx_ack(uint8_t ok, uint8_t related_type)
{
    uint8_t payload[2];
    payload[0] = ok;
    payload[1] = related_type;
    return frame_tx_send(FRAME_TYPE_ACK, payload, 2U);
}

/* ========================================================================== */
/* INIT / REGISTER                                                             */
/* ========================================================================== */

void frame_protocol_init(void)
{
    s_rx_head      = 0U;
    s_rx_tail      = 0U;
    s_assembly_len = 0U;
    g_frame_rx_frames_ok  = 0U;
    g_frame_rx_frames_bad = 0U;
    g_frame_tx_frames     = 0U;
    memset(s_handlers, 0, sizeof(s_handlers));
    memset((void *)s_rx_ring, 0, sizeof(s_rx_ring));
    memset(s_assembly, 0, sizeof(s_assembly));
}

void frame_protocol_register(uint8_t type, frame_handler_t handler)
{
    s_handlers[type] = handler;
}
