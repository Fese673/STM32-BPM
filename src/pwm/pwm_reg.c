#include "pwm/pwm_reg.h"
#include "stm32f1xx_hal.h"

extern TIM_HandleTypeDef htim3;

void PWM_Reg_FailSafe(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    /* PA6 jako zwykle wyjscie LOW (7447 BLANK = pelna jasnosc) zanim
     * ruszy TIM3. Reset MCU daje pin floating (wejscie) -> 7447 mialby
     * na BLANK stan nieustalony. LOW wygrywa z kazdym pozniejszym bledem. */
    __HAL_RCC_GPIOA_CLK_ENABLE();
    HAL_GPIO_WritePin(GPIOA, GPIO_PIN_6, GPIO_PIN_RESET);
    GPIO_InitStruct.Pin   = GPIO_PIN_6;
    GPIO_InitStruct.Mode  = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);
}

void PWM_Reg_Init(void)
{
    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_1);
    /* Start na 3% (przygaszone). ZEGAR przy starcie wysyla 5x swoja
     * wartosc z NVS i to ona zatryumfuje; dopoki ramka nie przyjdzie,
     * swiecimy slabo, nie na 100%. PWM_Reg_FailSafe() trzymal PA6 = LOW
     * (max) tylko przez kilka ms do startu TIM3 - stan nigdy nie byl
     * nieustalony. Bledna ramka 0xB0 nadal wymusza 100% (fail-safe). */
    PWM_Reg_SetDuty(PWM_REG_START_PERCENT);
}

static uint8_t s_last_pct = 100U; /* FailSafe trzyma pelna jasnosc */

uint8_t PWM_Reg_LastDuty(void)
{
    return s_last_pct;
}

void PWM_Reg_SetDuty(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_last_pct = percent;
    /* 7447 ma BLANKING active-low: LOW=ON, HIGH=OFF.
     * PWM 0% = PA6 LOW = segmenty 100% ON (jasno).
     * PWM 100% = PA6 HIGH = segmenty 0% (zgaszone).
     * Invert: user 100% -> inverted 0% -> PA6 LOW -> ON.
     *
     * Dzielnik = Period+1 (licznik liczy 0..Period), dzieki czemu skraje
     * sa dokladne: user 0% -> CCR=Period+1 = stale HIGH (pelne zgaszenie),
     * user 100% -> CCR=0 = stale LOW (pelna jasnosc bez szczelin). */
    const uint32_t period = (uint32_t)htim3.Init.Period + 1U;
    const uint8_t  inv    = (uint8_t)(100U - percent);
    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1,
        (uint16_t)(period * inv / 100U));
}
