#ifndef PWM_REG_H
#define PWM_REG_H

#include <stdint.h>

/* Wymusza bezpieczny stan PA6 (LOW = pelna jasnosc) zanim ruszy TIM3.
 * Wywolac w main() zaraz po HAL_Init(), przed MX_GPIO_Init/MX_TIM3_Init. */
void PWM_Reg_FailSafe(void);

/* Jasnosc startowa po resecie, zanim przyjdzie pierwsza ramka 0xB0. */
#define PWM_REG_START_PERCENT 3U

/* Startuje TIM3_CH1 i ustawia jasnosc startowa (PWM_REG_START_PERCENT). */
void PWM_Reg_Init(void);

/* percent 0..100 (100 = pelna jasnosc, 0 = zgaszone). >100 przycinane.
 * Ustawia JASNOSC BAZOWA (user/dim) i od razu ja aplikuje. */
void PWM_Reg_SetDuty(uint8_t percent);

/* Surowe wpisanie wartosci do CCR BEZ zmiany bazy — warstwa blinku alarmu
 * (seg_blink) nadpisuje tak faze OFF. Nie uzywac do jasnosci uzytkownika. */
void PWM_Reg_Commit(uint8_t percent);

/* Ustawia tylko JASNOSC BAZOWA (bez zapisu CCR). Dla ciaglych warstw
 * (dim radaru) pisanych takze podczas blinku — blink pozostaje wlascicielem
 * CCR i podlapie nowa baze przy nastepnej fazie. */
void PWM_Reg_SetBase(uint8_t percent);

/* Jasnosc bazowa = ostatni PWM_Reg_SetDuty (sledzenie dla warstwy dimu —
 * stepper_link porownuje wartosc docelowa z bazowa, zeby nie pisac PWM
 * bez zmiany; blink nie zaburza tego porownania). */
uint8_t PWM_Reg_LastDuty(void);

/* Jasnosc realnie wpisana w CCR (rozni sie od bazy tylko w blinie). */
uint8_t PWM_Reg_LastApplied(void);

#endif
