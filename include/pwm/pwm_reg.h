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

/* percent 0..100 (100 = pelna jasnosc, 0 = zgaszone). >100 przycinane. */
void PWM_Reg_SetDuty(uint8_t percent);

#endif
