/**
 * COBS encode/decode + CRC-16-CCITT (bitwise).
 *
 * Algorytmy przeniesione w 1:1 z GUTION CobsFraming.cpp.
 * Binarna kompatybilność z protokołem ZEGAR↔GUTION.
 */

#include "cobs.h"

uint16_t cobs_crc16(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFFU;
    for (size_t i = 0; i < length; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = (uint16_t)((crc << 1) ^ 0x1021U);
            } else {
                crc = (uint16_t)(crc << 1);
            }
        }
    }
    return crc;
}

size_t cobs_encode(const uint8_t *input, size_t length, uint8_t *output)
{
    size_t read_index  = 0U;
    size_t write_index = 1U;
    size_t code_index  = 0U;
    uint8_t code       = 1U;

    while (read_index < length) {
        if (input[read_index] == 0U) {
            output[code_index] = code;
            code = 1U;
            code_index = write_index++;
            read_index++;
        } else {
            output[write_index++] = input[read_index++];
            code++;
            if (code == 0xFFU) {
                output[code_index] = code;
                code = 1U;
                code_index = write_index++;
            }
        }
    }
    output[code_index] = code;
    return write_index;
}

size_t cobs_decode(const uint8_t *input, size_t length, uint8_t *output)
{
    size_t read_index  = 0U;
    size_t write_index = 0U;
    uint8_t code;
    uint8_t i;

    while (read_index < length) {
        code = input[read_index];
        if (read_index + code > length && code != 1U) {
            return 0U;
        }
        read_index++;
        for (i = 1U; i < code; i++) {
            output[write_index++] = input[read_index++];
        }
        if (code != 0xFFU && read_index != length) {
            output[write_index++] = 0U;
        }
    }
    return write_index;
}
