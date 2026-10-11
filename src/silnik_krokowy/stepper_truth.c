/*
 * silnik_krokowy / stepper_truth — implementacja jedynego zrodla prawdy.
 *
 * Layout flash (ostatnia strona 1 KB @ 0x0800FC00):
 *   slot A @ +0x00 (32 B), slot B @ +0x20 (32 B), reszta strony wolna.
 * Slot: [magic u32][seq u32][pos i32][span i32][homeSpeed u16][homeAccel u16]
 *        [rsvd u16][crc u16] = 24 B programowane jako 12 half-wordow.
 * CRC16-CCITT (poly 0x1021, init 0xFFFF) liczone po bajtach [magic..rsvd].
 *
 * Zapis: na przemian A/B; jezeli docelowy slot nie jest wyczyszczony
 * (magic != 0xFFFFFFFF) -> erase calej strony, potem zapis slotu A.
 * Erase co najwyzej co drugi zapis; zapis tylko przy zmianie pozycji.
 */

#include "silnik_krokowy/stepper_truth.h"
#include "stm32f1xx_hal.h"

volatile uint32_t g_truth_saves      = 0U;
volatile uint32_t g_truth_save_fail  = 0U;
volatile uint32_t g_truth_rejected   = 0U;

#define TRUTH_FLASH_ADDR  0x0800FC00U
#define TRUTH_SLOT_A      (TRUTH_FLASH_ADDR + 0x00U)
#define TRUTH_SLOT_B      (TRUTH_FLASH_ADDR + 0x20U)
#define TRUTH_MAGIC       0x54525448UL  /* "TRTH" */
#define TRUTH_SAVE_DELAY_MS 2000U
#define TRUTH_MIN_SPAN    64L

/* Predkosc autonomicznego powrotu: ostatnia z SET_SPEED (persist we flash).
 * Przyspieszenie stale — bezpieczny kompromis jak kGuitionAccel w ZEGAR-ze. */
#define TRUTH_HOME_ACCEL  1000U
#define TRUTH_SPEED_MIN   1U
#define TRUTH_SPEED_MAX   1000U
#define TRUTH_SPEED_DEF   400U

typedef struct {
    uint32_t magic;
    uint32_t seq;
    int32_t  pos;
    int32_t  span;
    uint16_t home_speed;
    uint16_t home_accel;
    uint16_t home_deg;   /* cel powrotu po resecie 0..360 (wczesniej rsvd) */
    uint16_t crc;
} truth_slot_t;

static uint32_t s_seq = 0U;
static int32_t  s_span = 0L;
static uint8_t  s_cal_ok = 0U;
static uint16_t s_home_speed = TRUTH_SPEED_DEF; /* nadpisuje flash w init */
static uint16_t s_home_deg = 0U; /* cel powrotu po resecie (nadpisuje flash) */
static int32_t  s_last_saved_pos = 0L;
static uint8_t  s_have_saved_base = 0U;

/* Uzbrojenie zapisu po krawedzi koniec-ruchu. */
static uint8_t  s_save_armed = 0U;
static int32_t  s_save_pos = 0L;
static uint32_t s_save_deadline = 0;

/* Jednorazowa flaga auto-home po starcie. */
static uint8_t s_home_needed = 0U;

static uint16_t crc16_ccitt(const uint8_t *data, uint32_t len)
{
    uint16_t crc = 0xFFFFU;
    for (uint32_t i = 0U; i < len; ++i) {
        crc ^= (uint16_t)((uint16_t)data[i] << 8);
        for (uint8_t b = 0U; b < 8U; ++b) {
            if (crc & 0x8000U) crc = (uint16_t)((crc << 1) ^ 0x1021U);
            else               crc = (uint16_t)(crc << 1);
        }
    }
    return crc;
}

static uint16_t slot_crc(const truth_slot_t *s)
{
    return crc16_ccitt((const uint8_t *)s,
                       (uint32_t)((const uint8_t *)&s->crc - (const uint8_t *)s));
}

static void slot_read(uint32_t addr, truth_slot_t *out)
{
    const uint8_t *p = (const uint8_t *)addr;
    uint8_t *d = (uint8_t *)out;
    for (uint32_t i = 0U; i < sizeof(truth_slot_t); ++i) d[i] = p[i];
}

static uint8_t slot_valid(const truth_slot_t *s)
{
    if (s->magic != TRUTH_MAGIC) return 0U;
    return slot_crc(s) == s->crc ? 1U : 0U;
}

static uint8_t slot_erased(uint32_t addr)
{
    truth_slot_t s;
    slot_read(addr, &s);
    return s.magic == 0xFFFFFFFFUL ? 1U : 0U;
}

/* Fizyczny zapis slotu. Kontekst: stepper_poll, postoj. Zwraca 1 gdy OK. */
static uint8_t flash_write_slot(uint32_t addr, const truth_slot_t *s)
{
    const uint16_t *hw = (const uint16_t *)s;
    HAL_StatusTypeDef st;

    HAL_FLASH_Unlock();

    if (!slot_erased(addr)) {
        /* Slot zajety — erase calej strony (oba sloty przepadaja, seq w RAM
         * rosnie monotonicznie, wiec nastepny zapis i tak wygra). */
        FLASH_EraseInitTypeDef ei;
        uint32_t page_err = 0U;
        ei.TypeErase   = FLASH_TYPEERASE_PAGES;
        ei.Banks       = FLASH_BANK_1;
        ei.PageAddress = TRUTH_FLASH_ADDR;
        ei.NbPages     = 1U;
        if (HAL_FLASHEx_Erase(&ei, &page_err) != HAL_OK) {
            HAL_FLASH_Lock();
            return 0U;
        }
        addr = TRUTH_SLOT_A;  /* po erase piszemy od A */
    }

    for (uint32_t i = 0U; i < sizeof(truth_slot_t) / 2U; ++i) {
        st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                               addr + i * 2U, (uint64_t)hw[i]);
        if (st != HAL_OK) {
            HAL_FLASH_Lock();
            return 0U;
        }
    }
    HAL_FLASH_Lock();

    /* Weryfikacja: re-read + CRC. */
    {
        truth_slot_t v;
        slot_read(addr, &v);
        if (!slot_valid(&v)) return 0U;
        if (v.seq != s->seq || v.pos != s->pos || v.span != s->span) return 0U;
    }
    return 1U;
}

static uint8_t truth_save_now(int32_t pos)
{
    truth_slot_t a, b;
    slot_read(TRUTH_SLOT_A, &a);
    slot_read(TRUTH_SLOT_B, &b);
    const uint8_t va = slot_valid(&a);
    const uint8_t vb = slot_valid(&b);

    ++s_seq;
    truth_slot_t s;
    s.magic       = TRUTH_MAGIC;
    s.seq         = s_seq;
    s.pos         = pos;
    s.span        = s_span;
    s.home_speed  = s_home_speed;
    s.home_accel  = TRUTH_HOME_ACCEL;
    s.home_deg    = s_home_deg;
    s.crc         = slot_crc(&s);

    /* Cel: slot niewazny; gdy oba wazne — starszy (mniejszy seq). */
    uint32_t addr;
    if (!va)      addr = TRUTH_SLOT_A;
    else if (!vb) addr = TRUTH_SLOT_B;
    else          addr = (a.seq <= b.seq) ? TRUTH_SLOT_A : TRUTH_SLOT_B;

    if (flash_write_slot(addr, &s)) {
        ++g_truth_saves;
        s_last_saved_pos = pos;
        s_have_saved_base = 1U;
        return 1U;
    }
    ++g_truth_save_fail;
    return 0U;
}

void truth_init(int32_t *pos_out, int32_t *span_out, uint8_t *cal_ok_out)
{
    truth_slot_t a, b;
    slot_read(TRUTH_SLOT_A, &a);
    slot_read(TRUTH_SLOT_B, &b);
    const uint8_t va = slot_valid(&a);
    const uint8_t vb = slot_valid(&b);

    const truth_slot_t *best = NULL;
    if (va && vb)      best = (a.seq >= b.seq) ? &a : &b;
    else if (va)       best = &a;
    else if (vb)       best = &b;

    if (best != NULL) {
        s_seq = best->seq;
        s_span = best->span;
        s_cal_ok = (best->span >= TRUTH_MIN_SPAN) ? 1U : 0U;
        /* Predkosc powrotu z flash (walidacja — obrona przed corruption). */
        uint16_t hs = best->home_speed;
        if (hs < TRUTH_SPEED_MIN || hs > TRUTH_SPEED_MAX) hs = TRUTH_SPEED_DEF;
        if (hs == 0U || hs == 0xFFFFU) hs = TRUTH_SPEED_DEF; /* erase-widmo */
        s_home_speed = hs;
        /* Cel powrotu po resecie (stare sloty maja tu 0 = klasyczne zero). */
        uint16_t hd = best->home_deg;
        if (hd == 0xFFFFU) hd = 0U; /* erase-widmo (0xFFFF > 360 i tak by padlo) */
        if (hd > 360U) hd = 0U;
        s_home_deg = hd;
        s_last_saved_pos = best->pos;
        s_have_saved_base = 1U;
        if (pos_out != NULL)    *pos_out = best->pos;
        if (span_out != NULL)   *span_out = best->span;
        if (cal_ok_out != NULL) *cal_ok_out = s_cal_ok;
        /* Auto-home: skalibrowany i nie stoi na celu powrotu. */
        {
            int64_t ht = s_cal_ok ? (((int64_t)s_home_deg * (int64_t)s_span) / 90LL) : 0LL;
            s_home_needed = (uint8_t)(s_cal_ok && (best->pos != (int32_t)ht));
        }
    } else {
        s_seq = 0U;
        s_span = 0L;
        s_cal_ok = 0U;
        s_have_saved_base = 0U;
        s_home_needed = 0U;
        if (pos_out != NULL)    *pos_out = 0L;
        if (span_out != NULL)   *span_out = 0L;
        if (cal_ok_out != NULL) *cal_ok_out = 0U;
    }
    s_save_armed = 0U;
}

int32_t truth_zero(void) { return 0L; }
int32_t truth_span(void) { return s_span; }
uint8_t truth_calibrated(void) { return s_cal_ok; }

uint8_t truth_set_zero(void)
{
    /* Adopcja: licznik fizycznie zeruje WYWOLUJACY (motion_set_pos 0).
     * Tu tylko uniewazniamy kalibracje do czasu SET_SPAN (stary span
     * nie pasuje do nowej ramy) — zapis od razu, kalibracja jest rzadka. */
    s_span = 0L;
    s_cal_ok = 0U;
    s_save_armed = 0U;  /* pozycja 0 zapisze sie normalnym cyklem po ruchu */
    return truth_save_now(0L);
}

uint8_t truth_set_span(int32_t pos90)
{
    int32_t span = (pos90 >= 0L) ? pos90 : -pos90;
    if (span < TRUTH_MIN_SPAN) {
        ++g_truth_rejected;
        return 0U;
    }
    s_span = span;
    s_cal_ok = 1U;
    s_save_armed = 0U;
    return truth_save_now(pos90);
}

int32_t truth_deg_to_steps(uint16_t deg)
{
    int64_t steps = ((int64_t)deg * (int64_t)s_span) / 90LL;
    if (steps > 32768LL)  steps = 32768LL;
    if (steps < -32768LL) steps = -32768LL;
    return (int32_t)steps;
}

void truth_on_motion_end(int32_t pos)
{
    if (s_have_saved_base && (pos == s_last_saved_pos)) {
        s_save_armed = 0U;  /* stoi gdzie stalo — nie zuzywamy flash */
        return;
    }
    s_save_armed = 1U;
    s_save_pos = pos;
    s_save_deadline = HAL_GetTick() + (TRUTH_SAVE_DELAY_MS);
}

void truth_task_tick(int32_t cur_pos, uint8_t busy)
{
    if (!s_save_armed) return;
    if (busy) {
        /* Ruszyl znowu przed zapisem — przeuzbroj na nowsza pozycje. */
        s_save_pos = cur_pos;
        s_save_deadline = HAL_GetTick() + (TRUTH_SAVE_DELAY_MS);
        return;
    }
    if ((int32_t)(HAL_GetTick() - s_save_deadline) < 0) return;
    s_save_armed = 0U;
    truth_save_now(s_save_pos);
}

uint8_t truth_home_needed(void)
{
    uint8_t need = s_home_needed;
    s_home_needed = 0U;  /* jednorazowe — konsument startuje ruch */
    return need;
}

uint16_t truth_home_speed(void) { return s_home_speed; }
uint16_t truth_home_accel(void) { return TRUTH_HOME_ACCEL; }
uint16_t truth_home_deg(void) { return s_home_deg; }

int32_t truth_home_steps(void)
{
    if (!s_cal_ok) return 0L;
    return truth_deg_to_steps(s_home_deg);
}

void truth_set_home(uint16_t deg, int32_t cur_pos, uint8_t busy)
{
    if (deg > 360U) deg = 360U;
    if (deg == s_home_deg) return; /* bez zmian — nie zuzywamy flash */
    s_home_deg = deg;
    if (busy) {
        s_save_armed = 1U;
        s_save_pos = cur_pos;
        s_save_deadline = HAL_GetTick() + (TRUTH_SAVE_DELAY_MS);
        return;
    }
    if (s_have_saved_base) {
        truth_save_now(s_last_saved_pos);
    } else {
        truth_save_now(0L);
    }
}

void truth_set_speed(uint16_t speed, int32_t cur_pos, uint8_t busy)
{
    if (speed < TRUTH_SPEED_MIN) speed = TRUTH_SPEED_MIN;
    if (speed > TRUTH_SPEED_MAX) speed = TRUTH_SPEED_MAX;
    if (speed == s_home_speed) return; /* bez zmian — nie zuzywamy flash */
    s_home_speed = speed;
    if (busy) {
        /* W ruchu: nie stallujemy erase'em — uzbrajamy zapis na postoj
         * (tick dopisze 2 s po krawedzi; pozycje i tak odswieza). */
        s_save_armed = 1U;
        s_save_pos = cur_pos;
        s_save_deadline = HAL_GetTick() + (TRUTH_SAVE_DELAY_MS);
        return;
    }
    if (s_have_saved_base) {
        truth_save_now(s_last_saved_pos);
    } else {
        truth_save_now(0L);
    }
}
