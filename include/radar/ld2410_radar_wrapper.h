#ifndef LD2410_RADAR_WRAPPER_H
#define LD2410_RADAR_WRAPPER_H

/* Twardy wylacznik calego modulu (test A/B). 0 = brak init USART2,
 * brak poll — reszta firmware jedzie bez zmian. */
#define LD2410_ENABLE 1

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start radaru z presetem czulosci OUT (0 = SREDNIA, 1 = WYSOKA,
 * 2 = MALA; inna wartosc = SREDNIA). Preset zyje we flash truth —
 * main.c podaje truth_radar_sens() (stepper_init juz przed tym). */
void ld2410_radar_init(uint8_t sens_code);

/* Zmiana presetu czulosci w locie (komenda SET_RADAR_SENS 0x0F).
 * Nieblokujace: zapamietuje zadanie; poll aplikuje jedna sesja config
 * gdy radar zywy (brak lacza -> zadanie czeka do 30 s). */
void ld2410_radar_sens_request(uint8_t code);

/* Bare-metal poll — wywolywac co iteracje superloop (nieblokujacy). */
void ld2410_radar_poll(void);

bool ld2410_radar_is_connected(void);
bool ld2410_radar_is_zombie(void);
bool ld2410_radar_presence_detected(void);
bool ld2410_radar_moving_detected(void);
uint16_t ld2410_radar_moving_distance(void);
uint8_t ld2410_radar_moving_energy(void);
bool ld2410_radar_stationary_detected(void);
uint16_t ld2410_radar_stationary_distance(void);
uint8_t ld2410_radar_stationary_energy(void);
uint16_t ld2410_radar_detection_distance(void);
uint32_t ld2410_radar_frames_ok(void);
uint32_t ld2410_radar_frames_bad(void);

#ifdef __cplusplus
}
#endif

#endif
