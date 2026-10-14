#ifndef BRIGHTNESS_HANDLER_H
#define BRIGHTNESS_HANDLER_H

#include "frame_protocol.h"

/*
 * Handler ramki 0xB0 (Brightness) z kanału ZEGAR↔STM32.
 *
 * Po otrzymaniu poprawnej ramki:
 *   1. Ustawia PWM (TIM3_CH1 / PA6) przez PWM_Reg_SetDuty()
 *   2. Wysyła ACK do ZEGAR (ramka 0xA2, ok=1, related=0xB0)
 *
 * PWM inversion (100% user → 0% duty) obsługuje PWM_Reg_SetDuty
 * wewnętrznie (aktywne BLANKING dla UCY7447).
 */

/* Handler typu 0xB0 — rejestrowany w frame_protocol. */
void brightness_handler_init(void);
void brightness_handler_dispatch(uint8_t type, uint8_t seq,
                                 const uint8_t *payload, uint16_t len);

/* Maksymalna jasnosc ustawiona przez uzytkownika (ostatnia poprawna ramka
 * 0xB0; fail-safe/bledna ramka = 100). Warstwa "plynnego wylaczania"
 * (stepper_link) schodzi z tej wartosci w dol — nigdy jej nie przekracza. */
uint8_t brightness_user_percent(void);

#endif /* BRIGHTNESS_HANDLER_H */
