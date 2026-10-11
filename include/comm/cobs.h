#ifndef COBS_H
#define COBS_H

/*
 * COBS (Consistent Overhead Byte Stuffing) + CRC-16-CCITT.
 *
 * Identyczne algorytmy jak na ZEGAR (EsptoGuitionCobs.cpp) i GUTION
 * (CobsFraming.cpp) — binarna zgodność z kanałem ZEGAR↔GUTION.
 *
 * Zero allokacji dynamicznej. CRC bitwise (bez 256-bajtowej tabeli
 * → oszczędność ~512B flash + 512B RAM). Wystarczająco szybkie
 * dla ramek < 16B przychodzących z prędkością < 100Hz.
 *
 * Konwencja: wyjście cobsEncode/cobsDecode NIE zawiera delimiterów
 * 0x00. Te są dodawane przez warstwę frame_protocol.
 */

#include <stddef.h>
#include <stdint.h>

/* CRC-16-CCITT (init 0xFFFF, poly 0x1021, "normal" / non-reflected).
 * Bitwise, identyczny z CobsFraming.cpp po stronie GUTION. */
uint16_t cobs_crc16(const uint8_t *data, size_t length);

/* COBS encode. Zwraca liczbę bajtów zapisanych do output.
 * Output musi mieć co najmniej length + length/254 + 1 bajtów. */
size_t cobs_encode(const uint8_t *input, size_t length, uint8_t *output);

/* COBS decode. Zwraca liczbę bajtów zdekodowanych, lub 0 przy błędzie. */
size_t cobs_decode(const uint8_t *input, size_t length, uint8_t *output);

#endif /* COBS_H */
