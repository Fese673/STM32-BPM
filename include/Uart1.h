#ifndef UART1_H
#define UART1_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief UART1 Handler (extern - zdefiniowany w main.c)
 */
extern UART_HandleTypeDef huart1;

/**
 * @brief Initialize UART1 for interrupt-driven TX only
 * 
 * Call once in main() after MX_USART1_UART_Init() and MX_NVIC_Init().
 * RX disabled — PA9 (TX) → ZEGAR ESP32 GPIO16, no CH340.
 */
void uart1_init(void);

/**
 * @brief Send BPM and SpO2 values over UART1
 * 
 * Non-blocking: uses interrupt-driven transmission
 * Format: "BPM:XXX,SPO2:XX\r\n"
 * 
 * @param bpm Heart rate (0-255)
 * @param spo2 Blood oxygen saturation (0-100)
 */
void uart1_send_vitals(uint8_t bpm, uint8_t spo2);

/**
 * @brief Send PPG diff sample as binary frame over UART1
 *
 * Frame format: 0xAA + int16_t little-endian + 0x0A (4 bytes)
 * Non-blocking: uses interrupt-driven transmission (same queue as vitals)
 *
 * Called every MAX30102 sample (~50Hz) to stream the PPG waveform.
 *
 * @param diff eachSampleDiff from max30102 sensor
 */
void uart1_send_ppg_diff(int16_t diff);

/**
 * @brief Queue raw bytes for UART1 transmission
 *
 * Non-blocking: data is copied into an internal TX queue and sent from the
 * UART interrupt path.
 *
 * @param data Pointer to bytes to send
 * @param len Number of bytes to queue
 * @return true if all bytes were queued, false if the queue filled up
 */
bool uart1_write(const uint8_t *data, uint16_t len);

/**
 * @brief Send raw string over UART1
 * 
 * Non-blocking: uses interrupt-driven transmission
 * 
 * @param str Null-terminated string
 * @param len Length of string (without null terminator)
 */
void uart1_send_string(const char *str, uint16_t len);

/**
 * @brief Send single byte over UART1
 * 
 * Non-blocking
 * 
 * @param byte Byte to send
 */
void uart1_send_byte(uint8_t byte);

#endif // UART1_H