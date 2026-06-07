/*
 * ESP32_FLASHER.h
 *
 * Prototypy i konfiguracja dla prostego "ESP32 flasher" sterowanego przez STM32.
 * Uwaga: to prototyp — pliki zawierają jedynie deklaracje i stuby.
 * NIE dołączaj jeszcze tego pliku do main.c jeśli nie chcesz, żeby kod był używany.
 */

#ifndef ESP32_FLASHER_H
#define ESP32_FLASHER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Default constants */
#define ESP32_FLASHER_DEFAULT_BLOCK_SIZE 4096U
#define ESP32_FLASHER_SLIP_FLAG 0xC0

typedef struct {
    int en_pin;        /* platform-specific pin id (adapt to HAL) */
    int gpio0_pin;     /* platform-specific pin id (adapt to HAL) */
    uint32_t uart_baud_init;
    uint32_t uart_baud_fast;
    size_t flash_block_size;
} esp32_flasher_config_t;

extern const esp32_flasher_config_t ESP32_FLASHER_DEFAULT_CONFIG;
extern const uint8_t ESP32_FLASHER_SYNC_REQ[];
extern const size_t ESP32_FLASHER_SYNC_REQ_LEN;

/* Inicjalizacja: przechowuje konfigurację (nie wykonuje HW init) */
void ESP32F_init(const esp32_flasher_config_t* config);

/* Przygotowanie pinów (stub — zaimplementować w HAL/board) */
void ESP32F_initPins(void);

/* Hardware handshake: wejdź w tryb bootloadera (ROM) */
void ESP32F_enterBootMode(void);

/* Wysyła ramkę SYNC (SLIP) i sprawdza odpowiedź (stub) */
bool ESP32F_sync(void);

/* Negocjacja prędkości UART (opcjonalny, stub) */
bool ESP32F_changeBaudrate(uint32_t baud);

/* Przybliżony czas wgrywania (sekundy): T ≈ (S*10)/B */
size_t ESP32F_calcEstimatedTimeSeconds(uint32_t baud, size_t size_bytes);

/* Weryfikacja pliku (MD5/CRC) — stub do implementacji */
bool ESP32F_verifyFileMD5(const char* filepath, const char* expected_md5);

/* Wysoki poziom procesu aktualizacji (prototyp) */
bool ESP32F_performUpdate(const char* filepath);

#ifdef __cplusplus
}
#endif

#endif /* ESP32_FLASHER_H */
