#ifndef STEPPER_MOTION_H
#define STEPPER_MOTION_H

/*
 * silnik_krokowy / stepper_motion — PROFIL RUCHU (trapez predkosci).
 *
 * Czysta matematyka na krokach, BEZ zaleznosci od HAL — da sie testowac
 * na hoście. Sterowanie pinami robi stepper_hw (wstrzykniete przez poll()).
 *
 * Sposob uzycia (z taska, non-blocking):
 *   stepper_motion_move_rel(&m, steps, speed, accel);  // start zadania
 *   ...
 *   if (stepper_motion_poll(&m, stepper_hw_now_us())) {
 *       // wlasnie dojechal do celu -> wyslij DONE
 *   }
 *
 * Limity jak w Uno v2: kroki |n|<=32768, speed 1..1000 kroki/s,
 * accel 50..2000 kroki/s^2.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STEPPER_MAX_MOVE    32768L
#define STEPPER_SPEED_MIN   1U
#define STEPPER_SPEED_MAX   1000U
#define STEPPER_ACCEL_MIN   50U
#define STEPPER_ACCEL_MAX   2000U
#define STEPPER_SPEED_DEF   400U
#define STEPPER_ACCEL_DEF   400U

typedef struct {
    int32_t  pos;        /* aktualna pozycja w krokach */
    int32_t  target;     /* cel */
    uint32_t vmax;       /* kroki/s */
    uint32_t accel;      /* kroki/s^2 */
    uint32_t cur;        /* biezaca predkosc, kroki/s */
    uint64_t next_us;    /* kiedy wolno zrobic nastepny krok */
    uint8_t  busy;
} stepper_motion_t;

void stepper_motion_init(stepper_motion_t *m);

/* Start zadania. Wartosci poza zakresem sa clampowane. Zawsze ustawia busy=1
 * (nawet dla steps==0 -> DONE przyjdzie w najblizszym poll). */
void stepper_motion_move_rel(stepper_motion_t *m, int32_t steps,
                             uint32_t speed, uint32_t accel);
void stepper_motion_move_abs(stepper_motion_t *m, int32_t pos,
                             uint32_t speed, uint32_t accel);

/* Awaryjny stop: busy=0, pozycja zostaje tam gdzie licznik krokow. */
void stepper_motion_stop(stepper_motion_t *m);

/* Adopcja ramy (SET_ZERO z truth): ustawia licznik na v (target=v).
 * WOLAC WYLACZNIE na postoju (wywolujacy pilnuje idle) — inaczej rozjazd
 * celu z fizyka. Uzywa tylko truth do kalibracji. */
void stepper_motion_set_pos(stepper_motion_t *m, int32_t v);

static inline uint8_t stepper_motion_busy(const stepper_motion_t *m)
{ return m->busy; }
static inline int32_t stepper_motion_pos(const stepper_motion_t *m)
{ return m->pos; }

/* Non-blocking pump. Zwraca 1 DOKLADNIE RAZ — w momencie dojazdu do celu
 * (krawedz do wyslania DONE). Wywolywac czesto (z taska w petli). */
uint8_t stepper_motion_poll(stepper_motion_t *m, uint64_t now_us);

#ifdef __cplusplus
}
#endif

#endif /* STEPPER_MOTION_H */
