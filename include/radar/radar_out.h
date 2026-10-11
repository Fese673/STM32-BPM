#ifndef RADAR_OUT_H
#define RADAR_OUT_H

/* Sprzetowy pin OUT z LD2410C (HIGH = wykryto obecnosc, LOW = pusto).
 * Niezalezny od UART — dziala nawet gdy parser ramek milczy.
 *
 * Pin: PB5 (wolne GPIO, pull-down; wiszacy przewod = LOW = "pusto").
 */

#include <stdint.h>
#include <stdbool.h>
#include "stm32f1xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RADAR_OUT_GPIO  GPIOB
#define RADAR_OUT_PIN   GPIO_PIN_5

/* Konfiguruje PB5 jako wejscie. Wywolac raz po MX (zegar GPIOB juz wlaczony). */
void radar_out_init(void);

/* Probkuje pin co iteracje petli (debounce 50 ms). Nieblokujace. */
void radar_out_poll(void);

/* Stabilny stan OUT: true = obecnosc. */
bool radar_out_present(void);

/* Licznik zboczy 0->1 od startu (diagnostyka). */
uint32_t radar_out_rises(void);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_OUT_H */
