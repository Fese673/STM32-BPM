#ifndef UART2_H
#define UART2_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern UART_HandleTypeDef huart2;

void uart2_init(void);

void uart2_enable_rx(void);

void uart2_irq_handler(void);

int uart2_read_byte(void);

uint16_t uart2_available(void);

void uart2_write_byte(uint8_t byte);
bool uart2_write_byte_timeout(uint8_t byte, uint32_t timeout_ms);

uint32_t uart2_get_tx_timeouts(void);
uint32_t uart2_get_rx_drops(void);

#ifdef __cplusplus
}
#endif

#endif
