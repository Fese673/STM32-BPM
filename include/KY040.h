/**
 * @file ky040.h
 * @brief Biblioteka KY-040 rotary encoder dla STM32 HAL
 * @version 1.0.3
 * @author codingABI (konwersja na STM32 HAL)
 * 
 * Opis:
 * Biblioteka dla enkoderów obrotowych KY-040. Działa z przerwaniami EXTI
 * i zapobiega drganiom styków przez ignorowanie nieprawidłowych sekwencji CLK/DT
 * 
 * Prawidłowa sekwencja CW dla CLK/DT: Low/High->Low/Low->High/Low->High/High
 * Prawidłowa sekwencja CCW dla CLK/DT: High/Low->Low/Low->Low/High->High/High
 * 
 * License: 2-Clause BSD License
 * Copyright (c) 2023-2025 codingABI
 */

#ifndef __KY040_H
#define __KY040_H

#include "stm32f1xx_hal.h"

/** Wersja biblioteki */
#define KY040_VERSION "1.0.3"

/** Opóźnienie przed wejściem w tryb uśpienia po starcie sekwencji (ms) */
#define PREVENTSLEEPMS 150

/** Stan spoczynkowy pinów (oba HIGH) */
#define INITSTEP 0b11

/** Maksymalna liczba kroków w sekwencji sygnału */
#define MAXSEQUENCESTEPS 4

/** Stany rotacji enkodera */
typedef enum {
    KY040_IDLE = 0,             /**< Enkoder w spoczynku */
    KY040_ACTIVE,               /**< Enkoder się obraca, sekwencja nieukończona */
    KY040_CLOCKWISE,            /**< Sekwencja CW ukończona (obrót w prawo) */
    KY040_COUNTERCLOCKWISE      /**< Sekwencja CCW ukończona (obrót w lewo) */
} KY040_Direction_t;

/** Struktura enkodera KY-040 */
typedef struct {
    GPIO_TypeDef* clk_port;     /**< Port GPIO dla pinu CLK (A) */
    uint16_t clk_pin;           /**< Numer pinu CLK (A) */
    GPIO_TypeDef* dt_port;      /**< Port GPIO dla pinu DT (B) */
    uint16_t dt_pin;            /**< Numer pinu DT (B) */
    
    volatile uint8_t state;                     /**< Aktualny stan pinów (2 bity: CLK|DT) */
    volatile uint8_t lastResult;                /**< Ostatni ukończony kierunek rotacji */
    volatile uint32_t lastSequenceStartMillis;  /**< Timestamp początku sekwencji */
    volatile uint8_t sequenceStep;              /**< Aktualny krok w sekwencji (0-3) */
    volatile uint8_t direction;                 /**< Aktualny kierunek rotacji */
    volatile uint8_t oldState;                  /**< Poprzedni stan pinów */
} KY040_Handle_t;

// === Prototypy funkcji publicznych ===

/**
 * @brief Inicjalizacja enkodera KY-040
 * @param handle Wskaźnik na strukturę KY040_Handle_t
 * @param clk_port Port GPIO dla CLK (np. GPIOA)
 * @param clk_pin Numer pinu CLK (np. GPIO_PIN_3)
 * @param dt_port Port GPIO dla DT (np. GPIOA)
 * @param dt_pin Numer pinu DT (np. GPIO_PIN_4)
 */
void KY040_Init(KY040_Handle_t* handle, 
                GPIO_TypeDef* clk_port, uint16_t clk_pin,
                GPIO_TypeDef* dt_port, uint16_t dt_pin);

/**
 * @brief Sprawdź aktualny stan rotacji (wywołuj z głównej pętli lub ISR)
 * @param handle Wskaźnik na strukturę KY040_Handle_t
 * @retval KY040_CLOCKWISE Obrót CW ukończony
 * @retval KY040_COUNTERCLOCKWISE Obrót CCW ukończony
 * @retval KY040_IDLE Enkoder w spoczynku
 * @retval KY040_ACTIVE Enkoder się obraca (sekwencja w toku)
 */
KY040_Direction_t KY040_CheckRotation(KY040_Handle_t* handle);

/**
 * @brief Odczytaj i zapisz aktualny stan pinów CLK/DT
 * @param handle Wskaźnik na strukturę KY040_Handle_t
 * @retval Stan rotacji (jak KY040_CheckRotation)
 */
KY040_Direction_t KY040_GetRotation(KY040_Handle_t* handle);

/**
 * @brief Pobierz i zresetuj ostatni ukończony kierunek rotacji
 * @param handle Wskaźnik na strukturę KY040_Handle_t
 * @retval Ostatni kierunek rotacji (IDLE jeśli brak)
 * @note NIE UŻYWAJ w przerwaniach ISR (używa __disable_irq)
 */
KY040_Direction_t KY040_GetAndResetLastRotation(KY040_Handle_t* handle);

/**
 * @brief Pobierz zapisany stan pinów CLK/DT (2 bity)
 * @param handle Wskaźnik na strukturę KY040_Handle_t
 * @retval Stan pinów (bit 1=CLK, bit 0=DT)
 */
uint8_t KY040_GetState(KY040_Handle_t* handle);

/**
 * @brief Ustaw stan pinów CLK/DT (2 bity) - wywołuj z ISR
 * @param handle Wskaźnik na strukturę KY040_Handle_t
 * @param state Stan pinów (bit 1=CLK, bit 0=DT)
 */
void KY040_SetState(KY040_Handle_t* handle, uint8_t state);

/**
 * @brief Sprawdź czy bezpieczne jest wejście w tryb uśpienia
 * @param handle Wskaźnik na strukturę KY040_Handle_t
 * @retval 1 Bezpieczne (minęło >150ms od ostatniej sekwencji)
 * @retval 0 Niebezpieczne (możliwa utrata sygnałów)
 * @note NIE UŻYWAJ w przerwaniach ISR
 */
uint8_t KY040_ReadyForSleep(KY040_Handle_t* handle);

#endif /* __KY040_H */
