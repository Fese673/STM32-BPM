/**
 * ******************************************************************************
 * File Name          : uart1.c
 * Description        : UART1 TX-only driver (lock-free ring buffer, Cortex-M3)
 *                      PA9(TX) → ZEGAR GPIO16, 115200 baud, 50Hz PPG stream
 *                      PA10(RX) → ZEGAR GPIO17 (pull-up, receiver gated)
 *                      No __disable_irq() — uses volatile + M3 atomic 16-bit
 * ******************************************************************************
 */

#include "uart1.h"
#include <stdio.h>
#include <string.h>

/* ============================================================================ */
/* PRIVATE VARIABLES                                                            */
/* ============================================================================ */

#define UART1_TX_QUEUE_SIZE 1024U
#define UART1_TX_QUEUE_MASK (UART1_TX_QUEUE_SIZE - 1U)

static uint8_t tx_queue[UART1_TX_QUEUE_SIZE];
static volatile uint16_t tx_head;
static volatile uint16_t tx_tail;
static volatile uint16_t tx_active_length;
static volatile uint8_t tx_busy;

/* ============================================================================ */
/* FORWARD DECLARATIONS                                                         */
/* ============================================================================ */

static void uart1_tx_kick(void);
static void uart1_tx_complete_isr(void);

/* ============================================================================ */
/* PUBLIC API                                                                   */
/* ============================================================================ */

void uart1_init(void)
{
    tx_head = 0;
    tx_tail = 0;
    tx_active_length = 0;
    tx_busy = 0;

    // Gate USART1 receiver — disable RE to prevent noise on PA10 from
    // generating overrun errors. Also clear CR3 EIE (error interrupt enable).
    // TX path is unaffected (TE remains set from MX init).
    USART1->CR1 &= ~(uint32_t)USART_CR1_RE;
    USART1->CR3 &= ~(uint32_t)USART_CR3_EIE;
    // Flush any stale status flags
    (void)USART1->SR;
    (void)USART1->DR;
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
    char tx_buffer[64];
    int len = snprintf(tx_buffer, sizeof(tx_buffer), "BPM:%d,SPO2:%d\r\n", bpm, spo2);

    if (len > 0 && len < (int)sizeof(tx_buffer)) {
        (void)uart1_write((const uint8_t *)tx_buffer, (uint16_t)len);
    }
}

void uart1_send_ppg_diff(int16_t diff)
{
    uint8_t frame[4];
    frame[0] = 0xAA;
    frame[1] = (uint8_t)(diff & 0xFF);
    frame[2] = (uint8_t)((diff >> 8) & 0xFF);
    frame[3] = 0x0A;
    uart1_write(frame, 4);
}

bool uart1_write(const uint8_t *data, uint16_t len)
{
    uint16_t copied = 0;

    if (data == NULL || len == 0U) {
        return true;
    }

    // Lock-free ring buffer push.
    // Cortex-M3: volatile 16-bit reads/writes are atomic, no __disable_irq.
    // Writer: data first → head second (strict ordering prevents ISR
    // from reading stale data after head advances).
    while (copied < len)
    {
        uint16_t h = tx_head;
        uint16_t nh = (uint16_t)(h + 1U);
        if (nh >= UART1_TX_QUEUE_SIZE) {
            nh = 0U;
        }

        // Check buffer full (one slot sentinel).
        // Reading tx_tail is a single 16-bit volatile load = atomic.
        if (nh == tx_tail) {
            break;
        }

        tx_queue[h] = data[copied];
        copied++;

        // Publish the byte: data must be visible before head advances.
        // On M3, volatile stores are not reordered by the CPU.
        // DSB not needed: ISR runs after this instruction completes.
        tx_head = nh;
    }

    // If TX is idle and there is data to send, start the engine.
    // Race: ISR might set tx_busy=0 and find head==tail between our
    // check and kick. That's fine — tx_kick re-checks both.
    if (tx_busy == 0U && tx_head != tx_tail)
    {
        uart1_tx_kick();
    }

    return (copied == len);
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
    if (str != NULL && len > 0) {
        (void)uart1_write((const uint8_t *)str, len);
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
    (void)uart1_write(&byte, 1);
}

/* ============================================================================ */
/* PRIVATE HELPERS                                                              */
/* ============================================================================ */

static void uart1_tx_kick(void)
{
    uint16_t tail;
    uint16_t head;
    uint16_t chunk_length;

    if (tx_busy != 0U || tx_head == tx_tail)
    {
        return;
    }

    tail = tx_tail;
    head = tx_head;

    if (head > tail)
    {
        chunk_length = (uint16_t)(head - tail);
    }
    else
    {
        chunk_length = (uint16_t)(UART1_TX_QUEUE_SIZE - tail);
    }

    tx_active_length = chunk_length;
    tx_busy = 1U;

    if (HAL_UART_Transmit_IT(&huart1, &tx_queue[tail], chunk_length) != HAL_OK)
    {
        tx_busy = 0U;
        tx_active_length = 0U;
    }
}

static void uart1_tx_complete_isr(void)
{
    if (tx_active_length != 0U)
    {
        tx_tail = (uint16_t)((tx_tail + tx_active_length) % UART1_TX_QUEUE_SIZE);
        tx_active_length = 0U;
    }

    tx_busy = 0U;
    uart1_tx_kick();
}

/* ============================================================================ */
/* INTERRUPT CALLBACKS (HAL)                                                    */
/* ============================================================================ */

/**
 * @brief UART1 RX Complete Callback
 * 
 * UART1 is TX-only. RX is disabled. No echo, no re-arm.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    (void)huart;
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
        uart1_tx_complete_isr();
    }
}

/**
 * @brief UART1 Error Callback
 * 
 * Called by HAL if an error occurs (overrun, framing, etc.)
 * TX-only mode — just reset TX state.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == USART1)
    {
        tx_busy = 0U;
        uart1_tx_kick();
    }
}

/* ============================================================================ */
/* END OF FILE                                                                  */
/* ============================================================================ */