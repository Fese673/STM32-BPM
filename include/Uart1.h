#ifndef UART1_H
#define UART1_H

#include "stm32f1xx_hal.h"
#include <stdint.h>

/**
 * @brief UART1 Handler (extern - zdefiniowany w main.c)
 */
extern UART_HandleTypeDef huart1;

/**
 * @brief Initialize UART1 for TX/RX with interrupts
 * 
 * Call this once in main() after MX_USART1_UART_Init()
 * Sets up interrupt-driven reception
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