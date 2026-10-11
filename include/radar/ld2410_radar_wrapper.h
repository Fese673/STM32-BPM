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

void ld2410_radar_init(void);

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
