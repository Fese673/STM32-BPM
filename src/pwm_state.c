/**
 * ******************************************************************************
 * File Name          : pwm_state.c
 * Description        : TIM2 1ms tick — tylko heartbeat LED (PC13 co 500ms).
 *                      Sterowanie jasnoscia 7-seg (TIM3_CH1 / PA6) jest wylacznie
 *                      manual — z komendy BRT:XX przychodzacej z UART RX
 *                      (patrz cmd_parser.c + pwm_reg.c).
 *
 *                      Auto state machine (ramp-up + pulsing triangle wave) zostal
 *                      usuniety w Etapie 1 — jasnosc ustawiana z HMI GUTION.
 * ******************************************************************************
 */

#include "pwm_state.h"
#include "stm32f1xx_hal.h"
#include "pwm_reg.h"
#include "main.h"

static TIM_HandleTypeDef htim2;

static volatile uint32_t hb_tick = 0;

static void MX_TIM2_Init(void)
{
    __HAL_RCC_TIM2_CLK_ENABLE();

    htim2.Instance               = TIM2;
    htim2.Init.Prescaler         = 71;
    htim2.Init.CounterMode       = TIM_COUNTERMODE_UP;
    htim2.Init.Period            = 999;
    htim2.Init.ClockDivision     = TIM_CLOCKDIVISION_DIV1;
    htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;

    if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
    {
        Error_Handler();
    }

    HAL_NVIC_SetPriority(TIM2_IRQn, 3, 0);
    HAL_NVIC_EnableIRQ(TIM2_IRQn);

    HAL_TIM_Base_Start_IT(&htim2);
}

void PWM_State_Init(void)
{
    MX_TIM2_Init();
    /* Bezpieczny start: 0% jasnosci (7-seg zgaszone) do czasu pierwszej
     * BRT:XX komendy z ZEGAR-ESP32. Dlaczego 0%, nie 100%:
     *  - eliminuje krotki "flash" 100% przy starcie, gdy ZEGAR jeszcze
     *    nie zdazyl wyslac swojej wartosci (timing race między ZEGAR
     *    boot a STM32 RX armed — STM32 init + 200ms delay + RX enable
     *    vs. ZEGAR initSensors tuz po STM32data_begin).
     *  - 0% = widoczne "czekam na komende" zamiast "100% potem 0%"
     *  - gdy ZEGAR wysle BRT:XX z NVS, STM32 ustawi prawidlowa wartosc.
     * Jesli brak komendy (ZEGAR odlaczony/uszkodzony) — 7-seg zostaja
     * zgaszone, co jest bezpieczniejsze niz pelna jasnosc. */
    PWM_Reg_SetDuty(0);
}

void TIM2_IRQHandler(void)
{
    HAL_TIM_IRQHandler(&htim2);
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim->Instance != TIM2) return;

    uint32_t now = HAL_GetTick();

    /* Heartbeat 500ms (LED diagnostyczny). */
    if (now - hb_tick >= 500)
    {
        hb_tick = now;
        HAL_GPIO_TogglePin(GPIOC, GPIO_PIN_13);
    }

    /* PWM 7-seg (TIM3_CH1/PA6) jest sterowany wylacznie z cmd_parser
     * (BRT:XX). Brak auto state machine. */
}
