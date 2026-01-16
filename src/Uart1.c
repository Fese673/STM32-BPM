/**
 * ******************************************************************************
 * File Name          : uart1.c
 * Description        : UART1 driver for STM32F1 (debug/vitals transmission)
 *                      - Interrupt-driven TX (non-blocking)
 *                      - Interrupt-driven RX (non-blocking)
 *                      - BPM/SpO2 vitals transmission
 * ******************************************************************************
 */

#include "uart1.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================ */
/* PRIVATE VARIABLES                                                            */
/* ============================================================================ */

/**
 * RX Buffer: stores incoming UART1 data
 * Size: 100 bytes (enough for commands)
 */
static uint8_t rx_buffer[100];
static uint16_t rx_index = 0;

/**
 * TX Buffer: stores outgoing data
 * Size: 64 bytes (enough for vitals + margin)
 */
static char tx_buffer[64];

/* ============================================================================ */
/* PUBLIC FUNCTIONS                                                             */
/* ============================================================================ */

/**
 * @brief Initialize UART1 reception in interrupt mode
 * 
 * This function should be called once in main() after MX_USART1_UART_Init()
 * and MX_NVIC_Init() have been called.
 * 
 * Sets up interrupt-driven reception for the first byte.
 */
void uart1_init(void)
{
    // Start interrupt-driven reception (wait for first byte)
    HAL_UART_Receive_IT(&huart1, &rx_buffer[0], 1);
}

/**
 * @brief Send BPM and SpO2 values over UART1
 * 
 * Formats and sends vitals in format: "BPM:XXX,SPO2:XX\r\n"
 * Uses interrupt-driven transmission (non-blocking)
 * 
 * @param bpm Heart rate (0-255)
 * @param spo2 Blood oxygen saturation (0-100)
 */
void uart1_send_vitals(uint8_t bpm, uint8_t spo2)
{
    int len = snprintf(tx_buffer, sizeof(tx_buffer), "BPM:%d,SPO2:%d\r\n", bpm, spo2);
    
    if (len > 0 && len < (int)sizeof(tx_buffer)) {
        // Send using interrupt-driven transmission (non-blocking)
        HAL_UART_Transmit_IT(&huart1, (uint8_t *)tx_buffer, (uint16_t)len);
    }
}

/**
 * @brief Send raw string over UART1
 * 
 * Uses interrupt-driven transmission (non-blocking)
 * 
 * @param str Pointer to null-terminated string
 * @param len Length of string (without null terminator)
 */
void uart1_send_string(const char *str, uint16_t len)
{
    if (str != NULL && len > 0 && len <= sizeof(tx_buffer)) {
        HAL_UART_Transmit_IT(&huart1, (uint8_t *)str, len);
    }
}

/**
 * @brief Send single byte over UART1
 * 
 * Uses interrupt-driven transmission (non-blocking)
 * 
 * @param byte Byte to send
 */
void uart1_send_byte(uint8_t byte)
{
    HAL_UART_Transmit_IT(&huart1, &byte, 1);
}

/* ============================================================================ */
/* INTERRUPT CALLBACKS (HAL)                                                    */
/* ============================================================================ */

/**
 * @brief UART1 RX Complete Callback
 * 
 * Called by HAL when a byte is received.
 * Echoes the byte and continues waiting for more.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        // Get the received byte
        uint8_t received_byte = rx_buffer[rx_index];

        // Echo back to sender (useful for debugging)
        HAL_UART_Transmit_IT(&huart1, &received_byte, 1);

        // Check for end-of-line
        if (received_byte == '\n' || received_byte == '\r') {
            // Reset buffer for next command
            rx_index = 0;
        } else {
            // Move to next buffer position
            rx_index++;
            if (rx_index >= sizeof(rx_buffer)) {
                rx_index = 0;  // Wrap around (buffer overflow protection)
            }
        }

        // Continue waiting for next byte (interrupt-driven)
        HAL_UART_Receive_IT(&huart1, &rx_buffer[rx_index], 1);
    }
}

/**
 * @brief UART1 TX Complete Callback (Optional)
 * 
 * Called by HAL when transmission is complete.
 * Useful for status tracking or LED indicators.
 */
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        // Transmission complete
        // You can do something here if needed (e.g., toggle LED)
    }
}

/**
 * @brief UART1 Error Callback
 * 
 * Called by HAL if an error occurs (overrun, framing, etc.)
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        // Handle UART error (optional)
        // For now, just restart reception
        HAL_UART_Receive_IT(&huart1, &rx_buffer[0], 1);
        rx_index = 0;
    }
}

/* ============================================================================ */
/* END OF FILE                                                                  */
/* ============================================================================ */