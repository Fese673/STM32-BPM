/*
 * stepper / stepper_motion — trapez: przyspieszanie az do vmax,
 * hamowanie tak zeby wyhamowac dokladnie w target (decel = cur^2 / 2a).
 */

#include "stepper/stepper_motion.h"
#include "stepper/stepper_hw.h"  /* tylko stepper_hw_step() */

/* Start przyspieszania: pierwszy krok nie moze leciec od 1 kroku/s
 * (interwal 1 s = widoczne zatrzymanie). 100 krokow/s jest bezpieczne dla
 * tego napedu (poprzednia wersja ruszala wręcz od razu z vmax). */
#define MOTION_START_PPS 100U

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Integer sqrt (metoda bit-po-bicie). Argumenty: 2*a*dist <= 2*2000*32768
 * = 131e6 < 2^31 — mieści się w u32 bez przepełnienia. */
static uint32_t isqrt_u32(uint32_t v)
{
    uint32_t r = 0U;
    uint32_t bit = 1U << 30;
    while (bit > v) bit >>= 2;
    while (bit != 0U) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return r;
}

void stepper_motion_init(stepper_motion_t *m)
{
    m->pos     = 0;
    m->target  = 0;
    m->vmax    = STEPPER_SPEED_DEF;
    m->accel   = STEPPER_ACCEL_DEF;
    m->cur     = 0;
    m->next_us = 0;
    m->busy    = 0;
}

static void motion_start(stepper_motion_t *m, int32_t target,
                         uint32_t speed, uint32_t accel, uint64_t now_us)
{
    m->target = target;
    m->vmax   = clamp_u32(speed, STEPPER_SPEED_MIN, STEPPER_SPEED_MAX);
    m->accel  = clamp_u32(accel, STEPPER_ACCEL_MIN, STEPPER_ACCEL_MAX);
    /* Retarget (preempcja / zmiana kierunku) startuje od bezpiecznej
     * predkosci startowej i rozpedza sie trapezem — bez strzalu z vmax.
     * Krotki dystans startuje jeszcze wolniej (v <= sqrt(2*a*d)), zeby
     * wyhamowac gladko takze na kilku krokach. */
    uint32_t v0 = (m->vmax < MOTION_START_PPS) ? m->vmax : MOTION_START_PPS;
    const int32_t d = (target > m->pos) ? (target - m->pos)
                                        : (m->pos - target);
    if ((uint32_t)d < 1000000U) {
        const uint32_t v_land = isqrt_u32(2U * m->accel * (uint32_t)d);
        if (v0 > v_land) v0 = v_land;
    }
    if (v0 < STEPPER_SPEED_MIN) v0 = STEPPER_SPEED_MIN;
    m->cur    = v0;
    m->next_us = now_us;   /* pierwszy krok od razu */
    m->busy   = 1;
}

void stepper_motion_move_rel(stepper_motion_t *m, int32_t steps,
                             uint32_t speed, uint32_t accel)
{
    if (steps > STEPPER_MAX_MOVE)  steps = STEPPER_MAX_MOVE;
    if (steps < -STEPPER_MAX_MOVE) steps = -STEPPER_MAX_MOVE;
    motion_start(m, m->pos + steps, speed, accel, stepper_hw_now_us());
}

void stepper_motion_move_abs(stepper_motion_t *m, int32_t pos,
                             uint32_t speed, uint32_t accel)
{
    motion_start(m, pos, speed, accel, stepper_hw_now_us());
}

void stepper_motion_stop(stepper_motion_t *m)
{
    m->busy = 0;
    m->cur  = 0;
}

void stepper_motion_set_pos(stepper_motion_t *m, int32_t v)
{
    m->pos    = v;
    m->target = v;
    m->cur    = 0;
    m->busy   = 0;
}

uint8_t stepper_motion_poll(stepper_motion_t *m, uint64_t now_us)
{
    if (!m->busy) return 0;

    int32_t remaining = m->target - m->pos;
    if (remaining == 0) {
        /* Krawedz: cel osiagniety (np. zadanie 0 krokow). */
        m->busy = 0;
        m->cur  = 0;
        return 1;
    }

    /* Profil liczymy TYLKO raz na krok (nie raz na wywolanie z superloopa —
     * inaczej przyspieszanie i hamowanie zaleza od czestotliwosci petli). */
    if (now_us < m->next_us) return 0;

    const int dir = (remaining > 0) ? 1 : -1;
    const uint32_t dist =
        (remaining > 0) ? (uint32_t)remaining : (uint32_t)(-remaining);

    /* Predkosc bezpieczna dla pozostalego dystansu: v <= sqrt(2*a*dist).
     * Zapewnia hamowanie dokladnie w celu (bez przestrzelenia) niezaleznie
     * od tego, czy profil jest w fazie rozpędzania, czy hamowania. */
    uint32_t v_allowed = m->vmax;
    if (dist < 1000000U) {
        const uint32_t v_brake = isqrt_u32(2U * m->accel * dist);
        if (v_brake < v_allowed) v_allowed = v_brake;
    }
    if (v_allowed < STEPPER_SPEED_MIN) v_allowed = STEPPER_SPEED_MIN;

    if (m->cur < v_allowed) {
        /* Rozpedzanie: v += a*dt, dt ~= 1/cur. */
        uint32_t dv = (m->cur > 0U) ? (m->accel / m->cur) : m->accel;
        if (dv < 1U) dv = 1U;
        m->cur += dv;
        if (m->cur > v_allowed) m->cur = v_allowed;
    } else if (m->cur > v_allowed) {
        /* Hamowanie: v -= a*dt, ale nigdy w dol do przestoju (min 1). */
        uint32_t dv = (m->cur > 0U) ? (m->accel / m->cur) : 1U;
        if (dv < 1U) dv = 1U;
        m->cur = (m->cur > dv) ? (m->cur - dv) : 1U;
        if (m->cur < v_allowed) m->cur = v_allowed;
    }
    if (m->cur < STEPPER_SPEED_MIN) m->cur = STEPPER_SPEED_MIN;

    const uint64_t interval_us = 1000000ULL / m->cur;

    stepper_hw_step(dir);
    if (dir > 0) m->pos++;
    else         m->pos--;

    /* Nadganianie backlogu po stallu (erase flash ~20 ms, sesja radaru):
     * nigdy seria krokow — najwyzej jeden krok "od razu", potem rytm. */
    m->next_us += interval_us;
    if (now_us > m->next_us) {
        m->next_us = ((now_us - m->next_us) > 4U * interval_us)
                         ? now_us
                         : now_us + interval_us;
    }

    if (m->pos == m->target) {
        m->busy = 0;
        m->cur  = 0;
        return 1;   /* DONE */
    }
    return 0;
}
