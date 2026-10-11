/*
 * silnik_krokowy / stepper_motion — trapez: przyspieszanie az do vmax,
 * hamowanie tak zeby wyhamowac dokladnie w target (decel = cur^2 / 2a).
 */

#include "silnik_krokowy/stepper_motion.h"
#include "silnik_krokowy/stepper_hw.h"  /* tylko stepper_hw_step() */

static uint32_t clamp_u32(uint32_t v, uint32_t lo, uint32_t hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
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
    m->cur    = 0;
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

    int dir = (remaining > 0) ? 1 : -1;
    uint32_t dist = (remaining > 0) ? (uint32_t)remaining : (uint32_t)-remaining;

    /* Ile krokow potrzeba na wyhamowanie z biezacej predkosci: v^2 / 2a. */
    uint32_t decel_steps = (m->cur * m->cur) / (2U * m->accel);

    if (dist <= decel_steps + 1U) {
        /* Faza hamowania. */
        if (m->cur > 1U) m->cur--;
        else m->cur = 1U;   /* min. 1 krok/s — zawsze dojedziemy */
    } else if (m->cur < m->vmax) {
        /* Faza przyspieszania: v += a*dt, dt = 1/cur (okolo). */
        uint32_t dv = (m->cur > 0U) ? (m->accel / m->cur) : m->accel;
        if (dv < 1U) dv = 1U;
        m->cur += dv;
        if (m->cur > m->vmax) m->cur = m->vmax;
    }
    if (m->cur < 1U) m->cur = 1U;

    uint64_t interval_us = 1000000U / m->cur;
    if (now_us < m->next_us) return 0;

    stepper_hw_step(dir);
    if (dir > 0) m->pos++;
    else         m->pos--;

    /* Nadganianie backlogu po preemption, z cap zeby nie "strzelal". */
    m->next_us += interval_us;
    if (m->next_us + 4U * interval_us < now_us) {
        m->next_us = now_us;
    } else if (m->next_us < now_us) {
        m->next_us = now_us + interval_us;
    }

    if (m->pos == m->target) {
        m->busy = 0;
        m->cur  = 0;
        return 1;   /* DONE */
    }
    return 0;
}
