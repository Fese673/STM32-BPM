#include "pwm/pwm_reg.h"
#include "stm32f1xx_hal.h"

extern TIM_HandleTypeDef htim3;

void PWM_Reg_Init(void)
{
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    PWM_Reg_SetDuty(0);
}

void PWM_Reg_SetDuty(uint8_t percent)
{
    if (percent > 100) percent = 100;
    /* 7447 ma BLANKING active-low: LOW=ON, HIGH=OFF.
     * PWM 0% = PA6 LOW = segmenty 100% ON (jasno).
     * PWM 100% = PA6 HIGH = segmenty 0% (zgaszone).
     * Invert: user 100% -> inverted 0% -> PA6 LOW -> ON. */
    uint8_t inv = 100 - percent;
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1,
        (uint16_t)((uint32_t)htim3.Init.Period * inv / 100));
}
