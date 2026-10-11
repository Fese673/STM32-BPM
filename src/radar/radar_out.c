#include "radar/radar_out.h"

#define RADAR_OUT_DEBOUNCE_MS  50U

static bool     s_stable = false;
static bool     s_last_raw = false;
static uint32_t s_change_ms = 0U;
static uint32_t s_rises = 0U;

void radar_out_init(void)
{
    GPIO_InitTypeDef gi = {0};
    gi.Pin  = RADAR_OUT_PIN;
    gi.Mode = GPIO_MODE_INPUT;
    gi.Pull = GPIO_PULLDOWN;   /* wiszacy/odpiety = LOW = "pusto" */
    gi.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RADAR_OUT_GPIO, &gi);

    s_stable = (HAL_GPIO_ReadPin(RADAR_OUT_GPIO, RADAR_OUT_PIN) == GPIO_PIN_SET);
    s_last_raw = s_stable;
    s_change_ms = HAL_GetTick();
}

void radar_out_poll(void)
{
    const bool raw = (HAL_GPIO_ReadPin(RADAR_OUT_GPIO, RADAR_OUT_PIN) == GPIO_PIN_SET);
    const uint32_t now = HAL_GetTick();

    if (raw != s_last_raw) {
        s_last_raw = raw;
        s_change_ms = now;
        return;
    }
    if (raw != s_stable && (now - s_change_ms) >= RADAR_OUT_DEBOUNCE_MS) {
        s_stable = raw;
        if (raw) ++s_rises;
    }
}

bool radar_out_present(void) { return s_stable; }

uint32_t radar_out_rises(void) { return s_rises; }
