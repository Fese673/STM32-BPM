/**
 * ******************************************************************************
 * File Name          : uart1.c
 * Description        : UART1 full-duplex driver.
 *                      PA9(TX) -> ZEGAR GPIO16, 19200 baud
 *                      PA10(RX) <- ZEGAR GPIO17
 *
 *                      TX: ring buffer + ISR (TXE).
 *                      RX: ISR permanent (RXNEIE always on, no re-arm).
 *
 * Warstwa wyżej (frame_protocol.c) buduje ramki COBS+CRC16 i wywołuje
 * uart1_write(). Tutaj jest tylko transport bajtowy — nic nie wie
 * o protokole.
 * ******************************************************************************
 */

#include "comm/uart1.h"
#include "comm/frame_protocol.h"
#include "stm32f1xx_hal.h"
#include <string.h>

/* ============================================================================ */
/* TX RING BUFFER (ISR-safe)                                                   */
/* ============================================================================ */

#define TX_QUEUE_SIZE  1024U
#define TX_QUEUE_MASK  (TX_QUEUE_SIZE - 1U)

static uint8_t            tx_queue[TX_QUEUE_SIZE];
static volatile uint16_t  tx_head;   /* write: main loop */
static volatile uint16_t  tx_tail;   /* read : ISR       */
static volatile uint8_t   tx_busy;   /* 1 = ISR currently sending */

static void uart1_tx_kick(void);

/* ============================================================================ */
/* PUBLIC API                                                                   */
/* ============================================================================ */

void uart1_init(void)
{
    tx_head  = 0;
    tx_tail  = 0;
    tx_busy  = 0;

    /* Gate RX at boot – uart1_enable_rx() unlocks later. */
    USART1->CR1 &= ~(uint32_t)USART_CR1_RE;
    USART1->CR3 &= ~(uint32_t)USART_CR3_EIE;
    (void)USART1->SR;
    (void)USART1->DR;
}

void uart1_enable_rx(void)
{
    if (USART1->CR1 & USART_CR1_RXNEIE) return;
    (void)USART1->SR;
    (void)USART1->DR;
    /* Enable receiver + RXNE interrupt. RXNEIE stays on permanently:
     * each byte triggers ISR -> DR -> frame_rx_on_byte -> no re-arm needed. */
    USART1->CR1 |= USART_CR1_RE | USART_CR1_RXNEIE;
}

bool uart1_write(const uint8_t *data, uint16_t len)
{
    if (data == NULL || len == 0U) return true;

    bool ok = false;
    /* Save/restore PRIMASK: nie wlaczamy przerwan na slepo, gdy ktos
     * wolal uart1_write z zagniezdzonego krytycznego odcinka. */
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    {
        /* ALL-OR-NOTHING: najpierw sprawdzamy, czy cala ramka sie zmiesci.
         * Wczesniej przy prawie pelnym ringu kopiowal sie OBCIETY prefiks
         * (bez CRC/delimitera) — po drugiej stronie licznik frames_bad
         * i realna szansa na utrate ACK/DONE. Teraz za malo miejsca =
         * nic nie kopiujemy, caller dostaje false i sam decyduje
         * (DONE ma retry, ACK/REF nie sa krytyczne). */
        const uint16_t used = (uint16_t)((tx_head - tx_tail) & TX_QUEUE_MASK);
        const uint16_t free_slots = (uint16_t)(TX_QUEUE_SIZE - 1U - used);
        if (len <= free_slots) {
            for (uint16_t i = 0U; i < len; ++i) {
                const uint16_t h = tx_head;
                tx_queue[h] = data[i];
                tx_head = (uint16_t)((tx_head + 1U) & TX_QUEUE_MASK);
            }
            __DMB();  /* Dane widoczne przed kickiem ISR. */
            ok = true;
            uart1_tx_kick();
        }
    }
    if (primask == 0U) {
        __enable_irq();
    }
    return ok;
}

/* ============================================================================ */
/* TX ENGINE                                                                   */
/* ============================================================================ */

static void uart1_tx_kick(void)
{
    if (tx_busy) return;
    if (tx_head == tx_tail) return;
    tx_busy = 1;
    USART1->CR1 |= USART_CR1_TXEIE;
}

/* ============================================================================ */
/* ISR HANDLER – RXNE + TXE. RXNEIE stays on permanently (no re-arm).           */
/* ============================================================================ */

void uart1_irq_handler(void)
{
    uint32_t sr  = USART1->SR;

    /* RXNE – highest priority: read byte into ring immediately.
     * RXNEIE stays on – no re-arm. */
    if (sr & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)USART1->DR;
        frame_rx_on_byte(byte);
        return;
    }

    /* ORE – clear so RX doesn't stall */
    if (sr & USART_SR_ORE) {
        (void)USART1->SR;
        (void)USART1->DR;
        return;
    }

    uint32_t cr1 = USART1->CR1;

    /* TXE */
    if ((cr1 & USART_CR1_TXEIE) && (sr & USART_SR_TXE)) {
        if (tx_head != tx_tail) {
            USART1->DR = tx_queue[tx_tail];
            tx_tail = (uint16_t)((tx_tail + 1U) & TX_QUEUE_MASK);
        } else {
            USART1->CR1 &= ~(uint32_t)USART_CR1_TXEIE;
            tx_busy = 0;
            if (tx_head != tx_tail) {
                tx_busy = 1;
                USART1->CR1 |= USART_CR1_TXEIE;
            }
        }
        return;
    }

    /* TC */
    if ((cr1 & USART_CR1_TCIE) && (sr & USART_SR_TC)) {
        USART1->CR1 &= ~(uint32_t)USART_CR1_TCIE;
        return;
    }
}
