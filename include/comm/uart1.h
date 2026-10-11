#ifndef UART1_H
#define UART1_H

#include "stm32f1xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief UART1 Handle (extern — zdefiniowany w main.c).
 */
extern UART_HandleTypeDef huart1;

/**
 * @brief Inicjalizacja UART1 dla TX w trybie ISR (ring buffer).
 *
 * RX jest zablokowany (RE=0) do czasu uart1_enable_rx().
 * Wywołać raz w main() po MX_USART1_UART_Init() i MX_NVIC_Init().
 */
void uart1_init(void);

/**
 * @brief Odblokowanie RX (RE + RXNEIE). Idempotentne.
 *
 * Włącza odbiornik PA10. Bajty odbiera ISR (uart1_irq_handler)
 * i przekazuje do frame_protocol.
 */
void uart1_enable_rx(void);

/**
 * @brief Handler ISR USART1 (TXE + RXNE + błędy).
 * Wywoływany z USART1_IRQHandler w stm32f1xx_it.c.
 */
void uart1_irq_handler(void);

/**
 * @brief Zakolejkowanie surowych bajtów do wysłania.
 *
 * Non-blocking: dane kopiowane do wewnętrznego ringa TX, wysyłane z ISR.
 *
 * @param data Wskaźnik na bajty
 * @param len  Liczba bajtów
 * @return true jeśli wszystkie zakolejkowane; false jeśli ring się zapełnił
 */
bool uart1_write(const uint8_t *data, uint16_t len);

/**
 * @brief Wyślij C-string (bez null-terminatora).
 */
void uart1_send_string(const char *str, uint16_t len);

/**
 * @brief Wyślij jeden bajt.
 */
void uart1_send_byte(uint8_t byte);

#endif /* UART1_H */
