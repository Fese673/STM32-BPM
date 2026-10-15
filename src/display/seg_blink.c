/**
 * seg_blink.c — blink jasnosci 7-seg (alarm), ramka 0xB5 z ZEGARA.
 *
 * Cyfry generuje ZEGAR (SPI), STM32 wlada tylko linia BLANK (PWM PA6).
 * Alarm pulsuje jasnoscia: miedzy jasnoscia BAZOWA (user / dim radaru)
 * a SEG_BLINK_LOW_PCT. Warstwa nadpisuje wylacznie "zastosowana" wartosc
 * PWM (PWM_Reg_Commit) — baza (PWM_Reg_SetDuty) zostaje nietknieta, wiec
 * dim od radaru i jasnosc uzytkownika dzialaja bez zmian pod spodem,
 * a dim layer nie przepisuje PWM w kolko (porownuje z baza).
 *
 * Zabezpieczenia:
 *  - TTL 5 s: brak odswiezenia (ZEGAR padl/restart/link) = auto-OFF + baza,
 *  - po starcie STM32 blink OFF,
 *  - bledna ramka 0xB5 = NACK i brak zmiany stanu (fail-safe: zostaje jak bylo),
 *  - faza OFF = 2% (nie 0%): ekran zostaje "zywy", blink wyglada jak alarm,
 *    nie jak awaria ekranu — dokladnie o to chodzilo produktowo.
 */

#include "display/seg_blink.h"
#include "comm/frame_protocol.h"
#include "pwm/pwm_reg.h"
#include "stm32f1xx_hal.h"

#define SEG_BLINK_PERIOD_MS 500U   /* pelny cykl 1 Hz (pol okresu na faze) */
#define SEG_BLINK_LOW_PCT   2U     /* faza OFF — ten sam poziom co dim radaru */
#define SEG_BLINK_MIN_ON_PCT 25U   /* dolna granica fazy ON: alarm musi byc
                                    * widoczny takze gdy baza stoi na 2%
                                    * (ekran "zamkniety" po zjezdzie radaru)
                                    * albo uzytkownik zgasil jasnosc — alarm
                                    * ma priorytet nad jasnoscia. */
#define SEG_BLINK_TTL_MS    5000U  /* 2x heartbeat 2 s z ZEGARA + margines */
#define SEG_BLINK_POLL_MS   20U    /* bramka superloop (tania) */

static bool     s_enabled = false;
static bool     s_on_phase = true;
static uint32_t s_last_refresh_ms = 0U;
static uint32_t s_last_toggle_ms = 0U;
static uint32_t s_last_poll_ms = 0U;

static void seg_blink_dispatch(uint8_t type, uint8_t seq,
                               const uint8_t *payload, uint16_t len)
{
    (void)type;
    (void)seq;

    if (len != 1U || payload == NULL || payload[0] > 1U) {
        (void)frame_tx_ack(0U, FRAME_TYPE_SEG_BLINK); /* NACK, stan bez zmian */
        return;
    }

    seg_blink_set(payload[0] != 0U);
    (void)frame_tx_ack(1U, FRAME_TYPE_SEG_BLINK);
}

void seg_blink_init(void)
{
    frame_protocol_register(FRAME_TYPE_SEG_BLINK, seg_blink_dispatch);
}

void seg_blink_set(bool en)
{
    const uint32_t now = HAL_GetTick();
    s_last_refresh_ms = now;

    if (en == s_enabled) {
        return; /* idempotentnie — duplikaty ramek tylko odswiezaja TTL */
    }

    s_enabled = en;
    if (en) {
        /* Start od fazy ON (jasnosc bazowa), pelny okres od teraz. */
        s_on_phase = true;
        s_last_toggle_ms = now;
        PWM_Reg_Commit(PWM_Reg_LastDuty());
    } else {
        /* OFF jawne: natychmiast przywroc jasnosc bazowa. */
        PWM_Reg_Commit(PWM_Reg_LastDuty());
    }
}

void seg_blink_poll(void)
{
    const uint32_t now = HAL_GetTick();
    if ((uint32_t)(now - s_last_poll_ms) < SEG_BLINK_POLL_MS) {
        return;
    }
    s_last_poll_ms = now;

    const uint8_t base = PWM_Reg_LastDuty();

    if (!s_enabled) {
        /* Warstwa nieaktywna: gdyby po blinie zostala faza OFF (np. po
         * TTL), dociagnij zastosowana wartosc do bazy. */
        if (PWM_Reg_LastApplied() != base) {
            PWM_Reg_Commit(base);
        }
        return;
    }

    /* TTL: brak heartbeat z ZEGARA = alarm nie dzwoni juz (albo link padl). */
    if ((uint32_t)(now - s_last_refresh_ms) > SEG_BLINK_TTL_MS) {
        s_enabled = false;
        PWM_Reg_Commit(base);
        return;
    }

    if ((uint32_t)(now - s_last_toggle_ms) >= SEG_BLINK_PERIOD_MS) {
        s_last_toggle_ms = now;
        s_on_phase = !s_on_phase;
    }

    /* Faza ON ma dolna granice (SEG_BLINK_MIN_ON_PCT): alarm musi byc
     * widoczny takze przy bazie 2% (ekran zamkniety po zjezdzie radaru)
     * lub 0% (uzytkownik zgasil) — sygnalizacja alarmu ma priorytet.
     * Faza OFF: 2%, a przy bazie <= 2% zostaje baza (i tak widac puls). */
    uint8_t want;
    if (s_on_phase) {
        want = (base < SEG_BLINK_MIN_ON_PCT) ? SEG_BLINK_MIN_ON_PCT : base;
    } else {
        want = (base > SEG_BLINK_LOW_PCT) ? SEG_BLINK_LOW_PCT : base;
    }

    if (PWM_Reg_LastApplied() != want) {
        PWM_Reg_Commit(want);
    }
}

bool seg_blink_active(void)
{
    return s_enabled;
}
