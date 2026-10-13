#ifndef FRAME_PROTOCOL_H
#define FRAME_PROTOCOL_H

/*
 * Frame protocol dla kanału ZEGAR↔STM32.
 *
 * Wire format (identyczny z ZEGAR↔GUTION):
 *   0x00 | COBS([type][seq][len_lo][len_hi][payload...][crc_lo][crc_hi]) | 0x00
 *
 * CRC: 16-bit CCITT, init 0xFFFF, poly 0x1021, obliczany nad
 *      bajtami [type..payload_ostatni] (bez samych bajtów CRC).
 *
 * Typy ramek dla kanału STM32 (oddzielne od ZEGAR↔GUTION, prefix 0xA0..0xBF):
 *   kStmTypePpg         0xA0  STM32→ZEGAR  payload=[diff_lo, diff_hi]
 *   kStmTypeBpm         0xA1  STM32→ZEGAR  payload=[bpm, spo2]
 *   kStmTypeAck         0xA2  STM32→ZEGAR  payload=[ok, relatedType]
 *   kStmTypeRadar       0xA3  STM32→ZEGAR  payload 28 B (append-only wzgledem 26 B):
 *                                          [presence,mov_lo,mov_hi,movE,stat_lo,stat_hi,statE,detect,
 *                                           movGate0..8,statGate0..8, detect_lo,detect_hi]
 *                                          [7] = legacy dolny bajt detect (max 600 cm
 *                                          nie miesci sie w 1 B); pelny dystans u16 LE
 *                                          na [26..27]. Stare parsery czytajace 26 B
 *                                          dzialaja bez zmian (off 0..25 nietkniete).
 *                                          presence: b0=ktos,b1=ruch,b2=stan,b3=OUT(PB5),
 *                                          b7=LINK-BAD (radar martwy),
 *                                          b6=GARBAGE (bajty na PA3, brak ramek: zly baud?)
 *                                          wysylka: przy zmianie stanu lub co 250 ms, zawsze gdy LD2410_ENABLE
 *   kStmTypeBrightness  0xB0  ZEGAR→STM32  payload=[value 0..100]
 *
 * Maksymalny rozmiar payload: 128B (kMaxPayloadBytes).
 */

#include "comm/uart1.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_HEADER_BYTES  4U
#define FRAME_CRC_BYTES     2U
#define FRAME_MAX_PAYLOAD   128U

/* Typy ramek — kanał STM32↔ZEGAR */
#define FRAME_TYPE_PPG         0xA0U
#define FRAME_TYPE_BPM         0xA1U
#define FRAME_TYPE_ACK         0xA2U
#define FRAME_TYPE_RADAR       0xA3U
#define FRAME_TYPE_BRIGHTNESS  0xB0U

/* Handler wywoływany z main loop po zweryfikowaniu ramki.
 * `payload` wskazuje na wewnętrzny bufor RX, ważny tylko w trakcie handlera. */
typedef void (*frame_handler_t)(uint8_t type, uint8_t seq,
                                const uint8_t *payload, uint16_t payload_len);

/* Inicjalizacja modułu (zeruje stan RX). Wywołać raz w main(). */
void frame_protocol_init(void);

/* Rejestracja handlera dla danego typu ramki. */
void frame_protocol_register(uint8_t type, frame_handler_t handler);

/* Przetworzenie bufora RX (drain z ISR ringa, parsowanie ramek).
 * WYWOŁYWAĆ w main loop co iterację — funkcja nieblokująca. */
void frame_rx_process(void);

/* Callback z USART1 ISR — odłożył bajt do ringa RX. */
void frame_rx_on_byte(uint8_t b);

/* === TX: buduje ramkę, COBS-encode, wysyła przez uart1_write. === */

/* Niskopoziomowy nadajnik ramki. Zwraca true jeśli zakolejkowano.
 *  seq = 0 (STM32 nie śledzi sekwencji). */
bool frame_tx_send(uint8_t type, const uint8_t *payload, uint16_t len);

/* Pomocnicze dla STM32 → ZEGAR */
bool frame_tx_ppg(int16_t diff);
bool frame_tx_bpm(uint8_t bpm, uint8_t spo2);
bool frame_tx_radar(uint8_t presence, uint16_t movDist, uint8_t movEnergy,
                    uint16_t statDist, uint8_t statEnergy, uint16_t detectDist,
                    const uint8_t *movGates, const uint8_t *statGates);
bool frame_tx_ack(uint8_t ok, uint8_t related_type);

/* Statystyki (do debug, aktualizowane tylko w main-loop/task context) */
extern volatile uint32_t g_frame_rx_frames_ok;
extern volatile uint32_t g_frame_rx_frames_bad;
extern volatile uint32_t g_frame_tx_frames;

#ifdef __cplusplus
}
#endif

#endif /* FRAME_PROTOCOL_H */
