#ifndef STEPPER_HW_H
#define STEPPER_HW_H

/*
 * silnik_krokowy / stepper_hw — SUROWE sterowanie pinami ULN2003/2803.
 *
 * Piny (wg silnik.md, wolne w max30102.ioc — NIE koliduja z I2C/OLED/UART/PWM):
 *   PA4 -> IN1,  PA5 -> IN2,  PA7 -> IN3,  PA8 -> IN4
 *   (UWAGA: to PA7 na porcie A. OLED_SDA siedzi na PB7 — inny port, brak kolizji.)
 * Zasilanie silnika 5 V OSOBNE, GND wspolny z BluePill.
 *
 * Init GPIO robi SAM modul (nie ruszamy MX_GPIO_Init w main.c).
 * Tryb: half-step, 8 stanow (jak 28BYJ-48, 2048 kroki/obrot).
 * Wszystko non-blocking: step() wykonuje DOKLADNIE 1 krok i wraca.
 */

#include "stm32f1xx_hal.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Konfiguruje PA4/PA5/PA7/PA8 jako wyjscia PP, startowo LOW (zimne cewki). */
void stepper_hw_init(void);

/* Jeden krok: dir > 0 w przod, dir < 0 w tyl. dir == 0: nic nie robi. */
void stepper_hw_step(int dir);

/* Wylacza wszystkie cewki (zimny silnik). Wywolac po DONE i po STOP. */
void stepper_hw_release(void);

/* Czas monotoniczny w mikrosekundach (SysTick trick, 72 MHz).
 * Sluzy do odmierzania interwalow krokow — nie blokuje. */
uint64_t stepper_hw_now_us(void);

#ifdef __cplusplus
}
#endif

#endif /* STEPPER_HW_H */
