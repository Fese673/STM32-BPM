#include "radar/ld2410_radar_wrapper.h"
#include "radar/ld2410_radar.h"
#include "radar/radar_out.h"
#include "comm/uart2.h"
#include "stm32f1xx_hal.h"
#include "comm/frame_protocol.h"

static ld2410_radar s_radar;
static bool s_initialised = false;
static bool s_radar_zombie = false;
static uint32_t s_last_ok_ms = 0U;
static uint32_t s_radar_frames_ok = 0U;
static uint32_t s_radar_frames_bad = 0U;

/* Presety czulosci OUT (progi bramek 0..8; 0-100, WYZSZY = mniej czuly).
 * Fabryczne: ruch 50,50,40,30,20,15,15,15,15 / postoj 0,0,40,40,30,30,20,20,20.
 *  kod 0 SREDNIA (default): fabryczne x2 (dotychczasowe "o polowe mniej")
 *  kod 1 WYSOKA:  fabryczne (najczulsza)
 *  kod 2 MALA:    fabryczne x3 (clamp 100)
 * Bramki 0/1 postoj zostaja 0 (fabrycznie wylaczone). */
static const uint8_t kSensMov[3][9] = {
    {100, 100,  80, 60, 40, 30, 30, 30, 30},  /* 0 SREDNIA */
    { 50,  50,  40, 30, 20, 15, 15, 15, 15},  /* 1 WYSOKA  */
    {100, 100, 100, 90, 60, 45, 45, 45, 45},  /* 2 MALA    */
};
static const uint8_t kSensStat[3][9] = {
    {0, 0,  80, 80, 60, 60, 40, 40, 40},  /* 0 SREDNIA */
    {0, 0,  40, 40, 30, 30, 20, 20, 20},  /* 1 WYSOKA  */
    {0, 0, 100, 100, 90, 90, 60, 60, 60},  /* 2 MALA    */
};

static uint8_t s_sens_code = 0U;        /* aktywny preset (RAM; truth we flash) */
static uint8_t s_sens_pending = 0xFFU;  /* 0xFF = brak zadania zmiany */
static uint32_t s_sens_request_ms = 0U;

static uint8_t sens_sanitize(uint8_t code)
{
    return (code <= 2U) ? code : 0U;
}

static int uart_available(void)
{
    return (int)uart2_available();
}static int uart_read(void)
{
    return uart2_read_byte();
}

static void uart_write(uint8_t byte)
{
    uart2_write_byte(byte);
}

void ld2410_radar_init(uint8_t sens_code)
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

    /* Tryb engineering (energie 9 bramek) do ekranu radaru na Guition.
     * Jednorazowo przy starcie; przy niepowodzeniu radar dalej strumieniuje
     * basic (bramki = 0). Nie jest trwaly w module — po restarcie samego
     * radaru wlacza sie ponownie dopiero przy restarcie STM32. */
    (void)s_radar.requestStartEngineeringMode();

    /* Czulosc z presetu (truth flash; main.c podaje truth_radar_sens()).
     * Jedna sesja config (~150 ms). Tylko gdy radar odpowiedzial
     * (martwy nie opoznia startu). */
    s_sens_code = sens_sanitize(sens_code);
    if (s_radar.isConnected()) {
        (void)s_radar.setGateSensitivityThresholds(kSensMov[s_sens_code],
                                                   kSensStat[s_sens_code]);
    }
}

/* Zmiana presetu czulosci z UI (komenda 0x0F -> truth). Aplikacja w poll:
 * jednorazowa sesja config gdy radar zywy; brak lacza -> czekaj do 30 s. */
void ld2410_radar_sens_request(uint8_t code)
{
    s_sens_pending = sens_sanitize(code);
    s_sens_request_ms = HAL_GetTick();
}

/* Bare-metal poll — wolac co iteracje superloop. Wewnetrzny throttle
 * 50 ms (radar zyje) / 500 ms (zombie), jak wczesniej vTaskDelay.
 * Heartbeat 0xA3 do ZEGAR: przy zmianie stanu lub co 250 ms, ZAWSZE gdy
 * LD2410_ENABLE (takze przy martwym radarze — wtedy presence=0x80 LINK-BAD). */
void ld2410_radar_poll(void)
{
    // throttling wysyłki do ZEGAR - zmiana stanu lub co 1 s
    static uint8_t last_presence = 0xFF;
    static uint32_t last_send_ms = 0U;
    static uint32_t s_last_poll_ms = 0U;

    uint32_t now = HAL_GetTick();

    // Okno diagnostyczne: ile bajtow slyszy PA3 miedzy heartbeatami.
    // (probkowane PRZED read(), ktory drenuje kolejke UART.)
    static uint32_t s_window_bytes = 0U;
    s_window_bytes += uart2_available();

    if (s_initialised) {
        if ((now - s_last_poll_ms) >= (s_radar_zombie ? 500U : 50U)) {
            s_last_poll_ms = now;

            bool got = s_radar.read();
            if (got && s_radar.isConnected()) {
                s_last_ok_ms = now;
                s_radar_zombie = false;
                ++s_radar_frames_ok;
            } else {
                if ((now - s_last_ok_ms) > 5000U) {
                    s_radar_zombie = true;
                }
                if (!got) ++s_radar_frames_bad;
            }
        }
    }

    /* Preset czulosci z UI (0x0F): jednorazowa sesja config, gdy radar zywy.
     * Brak lacza — zadanie czeka do 30 s (potem przepada; wartosc jest juz
     * w truth i wroci przy nastepnym starcie STM32). */
    if (s_sens_pending != 0xFFU) {
        if ((now - s_sens_request_ms) > 30000U) {
            s_sens_pending = 0xFFU;
        } else if (s_initialised && !s_radar_zombie && s_radar.isConnected()) {
            const uint8_t code = s_sens_pending;
            s_sens_pending = 0xFFU;
            if (s_radar.setGateSensitivityThresholds(kSensMov[code],
                                                     kSensStat[code])) {
                s_sens_code = code;
            }
        }
    }

    // Diagnostyka lacza: wysylaj zawsze, zywy radar = bity 0..2,
    // martwy = bit 7 LINK-BAD + bit 6 GARBAGE (cos przylatuje na PA3,
    // ale brak poprawnych ramek: prog >=4 B/okno odcina pojedyncze
    // szumy wiszącego pinu).
    bool link_ok = s_initialised && !s_radar_zombie && s_radar.isConnected();
    uint8_t presence = 0U;
    uint16_t movDist = 0U, statDist = 0U, detDist = 0U;
    uint8_t movEnergy = 0U, statEnergy = 0U;
    uint8_t movGates[9] = {0U};
    uint8_t statGates[9] = {0U};
    if (link_ok) {
        if (s_radar.presenceDetected()) presence |= 0x01U;
        if (s_radar.movingTargetDetected()) presence |= 0x02U;
        if (s_radar.stationaryTargetDetected()) presence |= 0x04U;
        movDist   = s_radar.movingTargetDistance();
        movEnergy = s_radar.movingTargetEnergy();
        statDist  = s_radar.stationaryTargetDistance();
        statEnergy = s_radar.stationaryTargetEnergy();
        detDist   = s_radar.detectionDistance();
        for (uint8_t g = 0U; g < 9U; g++) {
            movGates[g]  = s_radar.movingEnergyAtGate(g);
            statGates[g] = s_radar.stationaryEnergyAtGate(g);
        }
    } else {
        presence = 0x80U;
        if (s_window_bytes >= 4U) presence |= 0x40U;
    }

    /* Bit 3 = sprzetowy OUT (PB5), niezalezny od UART: HIGH = ktos jest.
     * Doklejany zawsze (takze przy LINK-BAD) — do automatu ekranu na Guitionie. */
    if (radar_out_present()) presence |= 0x08U;

    /* 250 ms (nie 1 s): slupki bramek na ekranie maja byc plynne.
     * ~37 B na drucie 4x/s przy 19200 = ~8% pasma. */
    if ((presence != last_presence) || ((now - last_send_ms) >= 250U)) {
        (void)frame_tx_radar(presence, movDist, movEnergy,
                             statDist, statEnergy, detDist,
                             movGates, statGates);
        last_presence = presence;
        last_send_ms = now;
        s_window_bytes = 0U;
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
