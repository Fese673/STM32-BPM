#include "comm/uart2.h"

#define RX_RING_SIZE  128U
#define RX_RING_MASK  (RX_RING_SIZE - 1U)

static uint8_t            rx_ring[RX_RING_SIZE];
static volatile uint16_t  rx_head;
static volatile uint16_t  rx_tail;
static volatile uint32_t g_uart2_tx_timeouts = 0U;
static volatile uint32_t g_uart2_rx_drops = 0U;

void uart2_init(void)
{
    rx_head = 0U;
    rx_tail = 0U;

    USART2->CR1 &= ~(uint32_t)USART_CR1_RE;
    USART2->CR3 &= ~(uint32_t)USART_CR3_EIE;
    (void)USART2->SR;
    (void)USART2->DR;
}

void uart2_enable_rx(void)
{
    if (USART2->CR1 & USART_CR1_RXNEIE) return;
    (void)USART2->SR;
    (void)USART2->DR;
    USART2->CR1 |= USART_CR1_RE | USART_CR1_RXNEIE;
}

void uart2_irq_handler(void)
{
    uint32_t sr = USART2->SR;

    if (sr & USART_SR_RXNE) {
        uint8_t byte = (uint8_t)USART2->DR;
        uint16_t h  = rx_head;
        uint16_t nh = (uint16_t)(h + 1U) & RX_RING_MASK;
        if (nh != rx_tail) {
            rx_ring[h] = byte;
            rx_head = nh;
        } else {
            ++g_uart2_rx_drops;
        }
        return;
    }

    if (sr & USART_SR_ORE) {
        (void)USART2->SR;
        (void)USART2->DR;
        return;
    }
}

int uart2_read_byte(void)
{
    if (rx_head == rx_tail) return -1;
    uint8_t byte = rx_ring[rx_tail];
    rx_tail = (uint16_t)((rx_tail + 1U) & RX_RING_MASK);
    return (int)byte;
}

uint16_t uart2_available(void)
{
    return (uint16_t)((rx_head - rx_tail) & RX_RING_MASK);
}

// Timeout TXE 10ms @ 72MHz (~720k cykli) - nie blokuje RTOS na zawsze jeśli UART padł
static bool uart2_wait_txe(uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while (!(USART2->SR & USART_SR_TXE)) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            ++g_uart2_tx_timeouts;
            return false;
        }
    }
    return true;
}

bool uart2_write_byte_timeout(uint8_t byte, uint32_t timeout_ms)
{
    if (!uart2_wait_txe(timeout_ms)) return false;
    USART2->DR = byte;
    return true;
}

void uart2_write_byte(uint8_t byte)
{
    (void)uart2_write_byte_timeout(byte, 10U);
}

uint32_t uart2_get_tx_timeouts(void) { return g_uart2_tx_timeouts; }
uint32_t uart2_get_rx_drops(void) { return g_uart2_rx_drops; }
