/*
 * stepper / stepper_hw — implementacja surowego sterowania.
 * Half-step 8-stanowy: A AB B BC C CD D DA (kolejnosc = kierunek +).
 */

#include "stepper/stepper_hw.h"

#define STEPPER_GPIO        GPIOA
#define STEPPER_PIN_IN1     GPIO_PIN_4
#define STEPPER_PIN_IN2     GPIO_PIN_5
#define STEPPER_PIN_IN3     GPIO_PIN_7
#define STEPPER_PIN_IN4     GPIO_PIN_8
#define STEPPER_ALL_PINS    (STEPPER_PIN_IN1 | STEPPER_PIN_IN2 | \
                             STEPPER_PIN_IN3 | STEPPER_PIN_IN4)

/* Bity ODR dla fazy: [IN1 IN2 IN3 IN4] */
static const uint8_t s_halfstep[8][4] = {
    {1,0,0,0}, /* A    */
    {1,1,0,0}, /* A+B  */
    {0,1,0,0}, /* B    */
    {0,1,1,0}, /* B+C  */
    {0,0,1,0}, /* C    */
    {0,0,1,1}, /* C+D  */
    {0,0,0,1}, /* D    */
    {1,0,0,1}, /* D+A  */
};

static uint8_t s_phase = 0;

void stepper_hw_init(void)
{
    GPIO_InitTypeDef gi = {0};

    __HAL_RCC_GPIOA_CLK_ENABLE();

    HAL_GPIO_WritePin(STEPPER_GPIO, STEPPER_ALL_PINS, GPIO_PIN_RESET);
    gi.Pin   = STEPPER_ALL_PINS;
    gi.Mode  = GPIO_MODE_OUTPUT_PP;
    gi.Pull  = GPIO_NOPULL;
    gi.Speed = GPIO_SPEED_FREQ_LOW;   /* ULN nie potrzebuje szybkich zboczy */
    HAL_GPIO_Init(STEPPER_GPIO, &gi);

    s_phase = 0;
}

void stepper_hw_step(int dir)
{
    if (dir > 0) {
        s_phase = (uint8_t)((s_phase + 1U) & 0x07U);
    } else if (dir < 0) {
        s_phase = (uint8_t)((s_phase + 7U) & 0x07U);
    } else {
        return;
    }

    /* Zapis przez BSRR (nie ODR!): atomowe set/reset bez read-modify-write.
     * Ten sam port GPIOA rusza tez inny task (PA1 buzzer) — RMW na ODR
     * gubiloby kroki/buzzer przy wspolbieznym dostepie. BSRR jest wolny
     * od tego wyscigu z natury sprzetu. */
    uint32_t setMask = 0U;
    if (s_halfstep[s_phase][0]) setMask |= STEPPER_PIN_IN1;
    if (s_halfstep[s_phase][1]) setMask |= STEPPER_PIN_IN2;
    if (s_halfstep[s_phase][2]) setMask |= STEPPER_PIN_IN3;
    if (s_halfstep[s_phase][3]) setMask |= STEPPER_PIN_IN4;
    STEPPER_GPIO->BSRR = ((STEPPER_ALL_PINS & ~setMask) << 16U) | setMask;
}

void stepper_hw_release(void)
{
    HAL_GPIO_WritePin(STEPPER_GPIO, STEPPER_ALL_PINS, GPIO_PIN_RESET);
}

uint64_t stepper_hw_now_us(void)
{
    /* SysTick 1 kHz: LOAD=72000 przy 72 MHz. Uwaga na wyscig przy przejsciu
     * milisekundy — blad max 1 ms, nieszkodliwy dla interwalow krokow. */
    uint32_t ms  = HAL_GetTick();
    uint32_t val = SysTick->VAL;   /* liczy w dol */
    uint32_t load = SysTick->LOAD;
    uint32_t us_in_ms = (load > val)
        ? (uint32_t)(((uint64_t)(load - val) * 1000U) / (load + 1U))
        : 0U;
    return (uint64_t)ms * 1000U + us_in_ms;
}
