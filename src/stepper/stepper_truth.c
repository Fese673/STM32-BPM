/*
 * stepper / stepper_truth — implementacja jedynego zrodla prawdy.
 *
 * Layout flash (ostatnia strona 1 KB @ 0x0800FC00):
 *   slot A @ +0x00 (32 B), slot B @ +0x20 (32 B), reszta strony wolna.
 * Slot v3: [magic u32][seq u32][pos i32][span i32][homeSpeed u16][flags u16]
 *          [homeDeg u16][targetDeg u16][autoCloseS u16][crc u16] = 28 B.
 * Starsze sloty (v2 26 B / v1 24 B) czytane TYLKO przy migracji po
 * aktualizacji firmware — zachowuja kalibracje/home/target/predkosc.
 * CRC16-CCITT (poly 0x1021, init 0xFFFF) liczone po bajtach [magic..ostatnie pole].
 *
 * Zapis: na przemian A/B; jezeli docelowy slot nie jest wyczyszczony
 * (magic != 0xFFFFFFFF) -> erase calej strony, potem zapis slotu A.
 * Erase co najwyzej co drugi zapis; zapis tylko przy zmianie pozycji.
 */

#include "stepper/stepper_truth.h"
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
    /* Flagi bitowe (wczesniej home_accel — stale TRUTH_HOME_ACCEL, nigdy
     * nieczytane; ten sam layout u16, stare sloty maja tu 1000 = bit0 0).
     * bit0 = radar_en (automat LD2410C wlaczony). */
    uint16_t flags;
    uint16_t home_deg;   /* cel powrotu po resecie 0..360 */
    uint16_t target_deg; /* WYBRANY kat (UI/automat), 0xFFFF = brak -> default */
    /* Czas auto-zjazdu radaru (sekundy). Tylko presety 30/180/600/2700
     * (walidacja przy odczycie i zapisie; inna wartosc = fabryczna 30). */
    uint16_t auto_close_s;
    uint16_t crc;
} truth_slot_t;

/* Poprzedni layout v2 (26 B, przed auto_close_s) — tylko migracja. */
typedef struct {
    uint32_t magic;
    uint32_t seq;
    int32_t  pos;
    int32_t  span;
    uint16_t home_speed;
    uint16_t flags;
    uint16_t home_deg;
    uint16_t target_deg;
    uint16_t crc;
} truth_slot_v2_t;

/* Stary layout (24 B, przed target_deg) — czytany tylko przy migracji
 * po aktualizacji firmware: zachowuje kalibracje/home/predkosc. */
typedef struct {
    uint32_t magic;
    uint32_t seq;
    int32_t  pos;
    int32_t  span;
    uint16_t home_speed;
    uint16_t home_accel; /* = TRUTH_HOME_ACCEL (1000) */
    uint16_t home_deg;
    uint16_t crc;
} truth_slot_old_t;

#define TRUTH_TARGET_DEF 45U
/* Fabryczny czas auto-zjazdu radaru (s). Presety UI: 30/180/600/2700. */
#define TRUTH_AUTO_CLOSE_DEF_S 30U

static uint32_t s_seq = 0U;
static int32_t  s_span = 0L;
static uint8_t  s_cal_ok = 0U;
static uint16_t s_home_speed = TRUTH_SPEED_DEF; /* nadpisuje flash w init */
static uint16_t s_home_deg = 0U; /* cel powrotu po resecie (nadpisuje flash) */
static uint8_t  s_radar_en = 0U; /* automat LD2410C (nadpisuje flash) */
static uint8_t  s_radar_sens = 0U; /* preset czulosci OUT 0/1/2 (nadpisuje flash) */
static uint8_t  s_onoff = 0U; /* "plynne wylaczenie" — dim 7-seg (nadpisuje flash) */
static uint16_t s_target_deg = TRUTH_TARGET_DEF; /* wybrany kat (nadpisuje flash) */
static uint16_t s_auto_close_s = TRUTH_AUTO_CLOSE_DEF_S; /* auto-zjazd radaru (flash) */
static int32_t  s_last_saved_pos = 0L;
static uint8_t  s_have_saved_base = 0U;

/* Uzbrojenie zapisu po krawedzi koniec-ruchu. */
static uint8_t  s_save_armed = 0U;
static int32_t  s_save_pos = 0L;
static uint32_t s_save_deadline = 0;
/* Zmiana PREFERENCJI (nie pozycji) — wymusza zapis takze wtedy, gdy ruch
 * skonczy sie w ostatnio zapisanej pozycji (bez tego SET_* zlany w trakcie
 * ruchu przepadal i po restarcie wracala stara wartosc). */
static uint8_t  s_cfg_dirty = 0U;

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

/* --- Migracja starego layoutu (24 B, bez target_deg) --- */

static uint16_t slot_crc_old(const truth_slot_old_t *s)
{
    return crc16_ccitt((const uint8_t *)s,
                       (uint32_t)((const uint8_t *)&s->crc - (const uint8_t *)s));
}

static void slot_read_old(uint32_t addr, truth_slot_old_t *out)
{
    const uint8_t *p = (const uint8_t *)addr;
    uint8_t *d = (uint8_t *)out;
    for (uint32_t i = 0U; i < sizeof(truth_slot_old_t); ++i) d[i] = p[i];
}

static uint8_t slot_valid_old(const truth_slot_old_t *s)
{
    if (s->magic != TRUTH_MAGIC) return 0U;
    return slot_crc_old(s) == s->crc ? 1U : 0U;
}

/* --- Migracja v2 (26 B, bez auto_close_s) --- */

static uint16_t slot_crc_v2(const truth_slot_v2_t *s)
{
    return crc16_ccitt((const uint8_t *)s,
                       (uint32_t)((const uint8_t *)&s->crc - (const uint8_t *)s));
}

static void slot_read_v2(uint32_t addr, truth_slot_v2_t *out)
{
    const uint8_t *p = (const uint8_t *)addr;
    uint8_t *d = (uint8_t *)out;
    for (uint32_t i = 0U; i < sizeof(truth_slot_v2_t); ++i) d[i] = p[i];
}

static uint8_t slot_valid_v2(const truth_slot_v2_t *s)
{
    if (s->magic != TRUTH_MAGIC) return 0U;
    return slot_crc_v2(s) == s->crc ? 1U : 0U;
}

/* Auto-zjazd radaru: tylko presety UI; cokolwiek innego (w tym 0xFFFF
 * z erase-widma i smieci po czesciowym zapisie) = fabryczna wartosc. */
static uint16_t ac_sanitize(uint16_t s)
{
    switch (s) {
    case 30U:
    case 180U:
    case 600U:
    case 2700U:
        return s;
    default:
        return TRUTH_AUTO_CLOSE_DEF_S;
    }
}

/* Fizyczny zapis: ZAWSZE oba sloty (A i B) ta sama zawartoscia — po zapisie
 * istnieja dwie wazne kopie i uszkodzenie jednej nie gubi kalibracji ani
 * ustawien (wczesniej kazdy zapis uniewaznial kopia zapasowa).
 * Kontekst: stepper_poll, TYLKO na postoju (erase stalluje CPU ~20 ms). */
static uint8_t truth_flash_store(const truth_slot_t *s)
{
    const uint16_t *hw = (const uint16_t *)s;
    HAL_StatusTypeDef st;

    HAL_FLASH_Unlock();

    if (!slot_erased(TRUTH_SLOT_A) || !slot_erased(TRUTH_SLOT_B)) {
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
    }

    const uint32_t addr[2] = { TRUTH_SLOT_A, TRUTH_SLOT_B };
    for (uint32_t a = 0U; a < 2U; ++a) {
        for (uint32_t i = 0U; i < sizeof(truth_slot_t) / 2U; ++i) {
            st = HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,
                                   addr[a] + i * 2U, (uint64_t)hw[i]);
            if (st != HAL_OK) {
                HAL_FLASH_Lock();
                return 0U;
            }
        }
    }
    HAL_FLASH_Lock();

    /* Weryfikacja OBU kopii: re-read + CRC + seq/pos/span. */
    for (uint32_t a = 0U; a < 2U; ++a) {
        truth_slot_t v;
        slot_read(addr[a], &v);
        if (!slot_valid(&v)) return 0U;
        if (v.seq != s->seq || v.pos != s->pos || v.span != s->span) return 0U;
    }
    return 1U;
}

static uint8_t truth_save_now(int32_t pos)
{
    ++s_seq;
    truth_slot_t s;
    s.magic       = TRUTH_MAGIC;
    s.seq         = s_seq;
    s.pos         = pos;
    s.span        = s_span;
    s.home_speed  = s_home_speed;
    s.flags       = (uint16_t)((s_radar_en ? TRUTH_FLAG_RADAR_EN : 0U) |
                               ((uint16_t)(s_radar_sens & 0x03U)
                                << TRUTH_FLAG_RADAR_SENS_SHIFT) |
                               (s_onoff ? TRUTH_FLAG_ONOFF : 0U));
    s.home_deg    = s_home_deg;
    s.target_deg  = s_target_deg;
    s.auto_close_s = s_auto_close_s;
    s.crc         = slot_crc(&s);

    if (truth_flash_store(&s)) {
        ++g_truth_saves;
        s_last_saved_pos = pos;
        s_have_saved_base = 1U;
        s_cfg_dirty = 0U;
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
        /* Automat radaru (stare sloty maja w flags 1000 = bit0 0 = OFF). */
        s_radar_en = (uint8_t)((best->flags & TRUTH_FLAG_RADAR_EN) != 0U);
        /* Preset czulosci OUT (bity 1-2; stare sloty = 0 = SREDNIA). */
        s_radar_sens = (uint8_t)((best->flags >> TRUTH_FLAG_RADAR_SENS_SHIFT) & 0x03U);
        if (s_radar_sens > 2U) s_radar_sens = 0U;
        /* Plynne wylaczenie (bit 3; stare sloty = 0 = OFF). */
        s_onoff = (uint8_t)((best->flags & TRUTH_FLAG_ONOFF) != 0U);
        /* Wybrany kat (0xFFFF/poza zakresem = default). */
        uint16_t td = best->target_deg;
        if (td == 0xFFFFU || td > 360U) td = TRUTH_TARGET_DEF;
        s_target_deg = td;
        /* Auto-zjazd radaru (tylko presety; inne = fabryczny). */
        s_auto_close_s = ac_sanitize(best->auto_close_s);
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
        /* Migracja v2 (26 B, przed auto_close_s): zachowaj kalibracje/home/
         * wybrany kat/automat; auto-zjazd = fabryczny. */
        truth_slot_v2_t a2, b2;
        slot_read_v2(TRUTH_SLOT_A, &a2);
        slot_read_v2(TRUTH_SLOT_B, &b2);
        const uint8_t va2 = slot_valid_v2(&a2);
        const uint8_t vb2 = slot_valid_v2(&b2);
        const truth_slot_v2_t *v2 = NULL;
        if (va2 && vb2)      v2 = (a2.seq >= b2.seq) ? &a2 : &b2;
        else if (va2)        v2 = &a2;
        else if (vb2)        v2 = &b2;

        if (v2 != NULL) {
            s_seq = v2->seq;
            s_span = v2->span;
            s_cal_ok = (v2->span >= TRUTH_MIN_SPAN) ? 1U : 0U;
            uint16_t hs = v2->home_speed;
            if (hs < TRUTH_SPEED_MIN || hs > TRUTH_SPEED_MAX) hs = TRUTH_SPEED_DEF;
            if (hs == 0U || hs == 0xFFFFU) hs = TRUTH_SPEED_DEF;
            s_home_speed = hs;
            uint16_t hd = v2->home_deg;
            if (hd == 0xFFFFU || hd > 360U) hd = 0U;
            s_home_deg = hd;
            s_radar_en = (uint8_t)((v2->flags & TRUTH_FLAG_RADAR_EN) != 0U);
            s_radar_sens = (uint8_t)((v2->flags >> TRUTH_FLAG_RADAR_SENS_SHIFT) & 0x03U);
            if (s_radar_sens > 2U) s_radar_sens = 0U;
            s_onoff = (uint8_t)((v2->flags & TRUTH_FLAG_ONOFF) != 0U);
            uint16_t td = v2->target_deg;
            if (td == 0xFFFFU || td > 360U) td = TRUTH_TARGET_DEF;
            s_target_deg = td;
            s_auto_close_s = TRUTH_AUTO_CLOSE_DEF_S;
            s_last_saved_pos = v2->pos;
            s_have_saved_base = 1U;
            if (pos_out != NULL)    *pos_out = v2->pos;
            if (span_out != NULL)   *span_out = v2->span;
            if (cal_ok_out != NULL) *cal_ok_out = s_cal_ok;
            {
                int64_t ht = s_cal_ok ? (((int64_t)s_home_deg * (int64_t)s_span) / 90LL) : 0LL;
                s_home_needed = (uint8_t)(s_cal_ok && (v2->pos != (int32_t)ht));
            }
        } else {
        /* Migracja ze starego layoutu 24 B (po aktualizacji firmware):
         * zachowaj kalibracje/home/predkosc; nowe pola = default. */
        truth_slot_old_t oa, ob;
        slot_read_old(TRUTH_SLOT_A, &oa);
        slot_read_old(TRUTH_SLOT_B, &ob);
        const uint8_t voa = slot_valid_old(&oa);
        const uint8_t vob = slot_valid_old(&ob);
        const truth_slot_old_t *old = NULL;
        if (voa && vob)      old = (oa.seq >= ob.seq) ? &oa : &ob;
        else if (voa)        old = &oa;
        else if (vob)        old = &ob;

        if (old != NULL) {
            s_seq = old->seq;
            s_span = old->span;
            s_cal_ok = (old->span >= TRUTH_MIN_SPAN) ? 1U : 0U;
            uint16_t hs = old->home_speed;
            if (hs < TRUTH_SPEED_MIN || hs > TRUTH_SPEED_MAX) hs = TRUTH_SPEED_DEF;
            if (hs == 0U || hs == 0xFFFFU) hs = TRUTH_SPEED_DEF;
            s_home_speed = hs;
            uint16_t hd = old->home_deg;
            if (hd == 0xFFFFU || hd > 360U) hd = 0U;
            s_home_deg = hd;
            s_radar_en = 0U;                /* stary layout nie mial flag */
            s_radar_sens = 0U;              /* brak presetu -> SREDNIA */
            s_onoff = 0U;                   /* brak flag -> OFF */
            s_target_deg = TRUTH_TARGET_DEF;
            s_auto_close_s = TRUTH_AUTO_CLOSE_DEF_S;
            s_last_saved_pos = old->pos;
            s_have_saved_base = 1U;
            if (pos_out != NULL)    *pos_out = old->pos;
            if (span_out != NULL)   *span_out = old->span;
            if (cal_ok_out != NULL) *cal_ok_out = s_cal_ok;
            {
                int64_t ht = s_cal_ok ? (((int64_t)s_home_deg * (int64_t)s_span) / 90LL) : 0LL;
                s_home_needed = (uint8_t)(s_cal_ok && (old->pos != (int32_t)ht));
            }
        } else {
            s_seq = 0U;
            s_span = 0L;
            s_cal_ok = 0U;
            s_radar_en = 0U;
            s_radar_sens = 0U;
            s_onoff = 0U;
            s_target_deg = TRUTH_TARGET_DEF;
            s_auto_close_s = TRUTH_AUTO_CLOSE_DEF_S;
            s_have_saved_base = 0U;
            s_home_needed = 0U;
            if (pos_out != NULL)    *pos_out = 0L;
            if (span_out != NULL)   *span_out = 0L;
            if (cal_ok_out != NULL) *cal_ok_out = 0U;
        }
        } /* else: brak v2 — stary layout 24 B wyzej */
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
    const int32_t prev_span   = s_span;
    const uint8_t prev_cal_ok = s_cal_ok;

    s_span = 0L;
    s_cal_ok = 0U;
    s_save_armed = 0U;  /* pozycja 0 zapisze sie normalnym cyklem po ruchu */

    if (truth_save_now(0L)) {
        return 1U;
    }

    /* Zapis nieudany (erase/program/CRC we flash): przywracamy poprzednia
     * rame, zeby RAM byl SPOJNY z flashem. Bez tego po power cycle wrocilaby
     * stara kalibracja przy mechanizmie ustawionym na nowe zero — automat
     * liczylby pozycje/limity w starej skali (cichy dryf). Caller dostaje 0
     * i tylko liczy odrzucenie (bez zmiany protokolu). */
    s_span   = prev_span;
    s_cal_ok = prev_cal_ok;
    return 0U;
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
    if (s_have_saved_base && (pos == s_last_saved_pos) && !s_cfg_dirty) {
        s_save_armed = 0U;  /* stoi gdzie stalo i brak nowych preferencji */
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
    if (!truth_save_now(s_save_pos) && s_cfg_dirty) {
        /* Flash odmowil (np. busy/err) — ponow za TRUTH_SAVE_DELAY_MS. */
        s_save_armed = 1U;
        s_save_deadline = HAL_GetTick() + (TRUTH_SAVE_DELAY_MS);
    }
}

/* Wspolna sciezka zapisu PREFERENCJI (settety truth_set_*): w ruchu tylko
 * uzbraja (erase stalluje CPU — nie wolno w trakcie krokow), na postoju
 * zapisuje od razu. Flaga cfg_dirty przetrwa do skutku, wiec zmiana zlana
 * w trakcie ruchu dojdzie do flasha takze przy powrocie na te sama pozycje. */
static void truth_pref_commit(int32_t cur_pos, uint8_t busy)
{
    s_cfg_dirty = 1U;
    if (busy) {
        s_save_armed = 1U;
        s_save_pos = cur_pos;
        s_save_deadline = HAL_GetTick() + (TRUTH_SAVE_DELAY_MS);
        return;
    }
    if (!truth_save_now(s_have_saved_base ? s_last_saved_pos : cur_pos) &&
        s_cfg_dirty) {
        /* Flash odmowil — uzbroj ponowienie (inaczej zmiana czekalaby na
         * nastepny koniec ruchu/zmiane preferencji). */
        s_save_armed = 1U;
        s_save_pos = cur_pos;
        s_save_deadline = HAL_GetTick() + (TRUTH_SAVE_DELAY_MS);
    }
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
    truth_pref_commit(cur_pos, busy);
}

void truth_set_speed(uint16_t speed, int32_t cur_pos, uint8_t busy)
{
    if (speed < TRUTH_SPEED_MIN) speed = TRUTH_SPEED_MIN;
    if (speed > TRUTH_SPEED_MAX) speed = TRUTH_SPEED_MAX;
    if (speed == s_home_speed) return; /* bez zmian — nie zuzywamy flash */
    s_home_speed = speed;
    /* W ruchu: nie stallujemy erase'em — commit uzbroi zapis na postoj. */
    truth_pref_commit(cur_pos, busy);
}

uint16_t truth_target_deg(void) { return s_target_deg; }

void truth_set_target(uint16_t deg, int32_t cur_pos, uint8_t busy)
{
    if (deg > 360U) deg = 360U;
    if (deg == s_target_deg) return; /* bez zmian — nie zuzywamy flash */
    s_target_deg = deg;
    truth_pref_commit(cur_pos, busy);
}

uint8_t truth_radar_en(void) { return s_radar_en; }

void truth_set_radar_en(uint8_t en, int32_t cur_pos, uint8_t busy)
{
    en = (uint8_t)(en != 0U);
    if (en == s_radar_en) return; /* bez zmian — nie zuzywamy flash */
    s_radar_en = en;
    truth_pref_commit(cur_pos, busy);
}

uint8_t truth_onoff(void) { return s_onoff; }

void truth_set_onoff(uint8_t en, int32_t cur_pos, uint8_t busy)
{
    en = (uint8_t)(en != 0U);
    if (en == s_onoff) return; /* bez zmian — nie zuzywamy flash */
    s_onoff = en;
    truth_pref_commit(cur_pos, busy);
}

uint16_t truth_auto_close_s(void) { return s_auto_close_s; }

void truth_set_auto_close(uint16_t s, int32_t cur_pos, uint8_t busy)
{
    s = ac_sanitize(s); /* tylko presety 30/180/600/2700, inne = default */
    if (s == s_auto_close_s) return; /* bez zmian — nie zuzywamy flash */
    s_auto_close_s = s;
    truth_pref_commit(cur_pos, busy);
}

uint8_t truth_radar_sens(void) { return s_radar_sens; }

void truth_set_radar_sens(uint8_t code, int32_t cur_pos, uint8_t busy)
{
    code = (code <= 2U) ? code : 0U; /* tylko presety 0/1/2 */
    if (code == s_radar_sens) return; /* bez zmian — nie zuzywamy flash */
    s_radar_sens = code;
    truth_pref_commit(cur_pos, busy);
}
