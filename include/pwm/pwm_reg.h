#ifndef PWM_REG_H
#define PWM_REG_H

#include <stdint.h>

void PWM_Reg_Init(void);
void PWM_Reg_SetDuty(uint8_t percent);

#endif
