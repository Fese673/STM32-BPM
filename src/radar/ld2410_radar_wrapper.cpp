#include "radar/ld2410_radar_wrapper.h"
#include "radar/ld2410_radar.h"
#include "comm/uart2.h"
#include "stm32f1xx_hal.h"
#include "comm/frame_protocol.h"

static ld2410_radar s_radar;
static bool s_initialised = false;
static bool s_radar_zombie = false;
static uint32_t s_last_ok_ms = 0U;
static uint32_t s_radar_frames_ok = 0U;
static uint32_t s_radar_frames_bad = 0U;

static int uart_available(void)
{
    return (int)uart2_available();
}

static int uart_read(void)
{
    return uart2_read_byte();
}

static void uart_write(uint8_t byte)
{
    uart2_write_byte(byte);
}

void ld2410_radar_init(void)
{
    // MX_USART2_UART_Init() jest już wołany w main.c przed tym init.
    // Nie wołamy ponownie aby nie re-inicjalizować HAL-UART w locie.
    uart2_init();

    ld2410_uart_t uart_if;
    uart_if.available = uart_available;
    uart_if.read      = uart_read;
    uart_if.write     = uart_write;

    if (s_radar.begin(uart_if, false)) {
        s_initialised = true;
    }
    uart2_enable_rx();
}

/* Bare-metal poll — wolac co iteracje superloop. Wewnetrzny throttle
 * 50 ms (radar zyje) / 500 ms (zombie), jak wczesniej vTaskDelay. */
void ld2410_radar_poll(void)
{
    if (!s_initialised) {
        return;
    }

    static uint32_t s_last_poll_ms = 0U;
    uint32_t now_ms = HAL_GetTick();
    if ((now_ms - s_last_poll_ms) < (s_radar_zombie ? 500U : 50U)) {
        return;
    }
    s_last_poll_ms = now_ms;

    // throttling wysyłki do ZEGAR - tylko przy zmianie lub co 1s
    static uint8_t last_presence = 0xFF;
    static uint32_t last_send_ms = 0U;

    bool got = s_radar.read();
    if (got && s_radar.isConnected()) {
        s_last_ok_ms = HAL_GetTick();
        s_radar_zombie = false;
        ++s_radar_frames_ok;
    } else {
        if ((HAL_GetTick() - s_last_ok_ms) > 5000U) {
            s_radar_zombie = true;
        }
        if (!got) ++s_radar_frames_bad;
    }

    // Wysyłka do ZEGAR tylko jeśli radar żyje i (zmiana stanu lub 1s timeout)
    if (!s_radar_zombie) {
        uint8_t presence = 0U;
        if (s_radar.presenceDetected()) presence |= 0x01U;
        if (s_radar.movingTargetDetected()) presence |= 0x02U;
        if (s_radar.stationaryTargetDetected()) presence |= 0x04U;

        uint32_t now = HAL_GetTick();
        bool changed = (presence != last_presence);
        bool periodic = (now - last_send_ms) >= 1000U;

        if ((changed || periodic) && s_radar.isConnected()) {
            (void)frame_tx_radar(
                presence,
                s_radar.movingTargetDistance(),
                s_radar.movingTargetEnergy(),
                s_radar.stationaryTargetDistance(),
                s_radar.stationaryTargetEnergy(),
                s_radar.detectionDistance());
            last_presence = presence;
            last_send_ms = now;
        }
    }
}

bool ld2410_radar_is_zombie(void) { return s_radar_zombie; }
uint32_t ld2410_radar_frames_ok(void) { return s_radar_frames_ok; }
uint32_t ld2410_radar_frames_bad(void) { return s_radar_frames_bad; }

bool ld2410_radar_is_connected(void)
{
    return s_initialised && s_radar.isConnected();
}

bool ld2410_radar_presence_detected(void)
{
    return s_radar.presenceDetected();
}

bool ld2410_radar_moving_detected(void)
{
    return s_radar.movingTargetDetected();
}

uint16_t ld2410_radar_moving_distance(void)
{
    return s_radar.movingTargetDistance();
}

uint8_t ld2410_radar_moving_energy(void)
{
    return s_radar.movingTargetEnergy();
}

bool ld2410_radar_stationary_detected(void)
{
    return s_radar.stationaryTargetDetected();
}

uint16_t ld2410_radar_stationary_distance(void)
{
    return s_radar.stationaryTargetDistance();
}

uint8_t ld2410_radar_stationary_energy(void)
{
    return s_radar.stationaryTargetEnergy();
}

uint16_t ld2410_radar_detection_distance(void)
{
    return s_radar.detectionDistance();
}
