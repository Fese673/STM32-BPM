/**
 * @file ky040.c
 * @brief Implementacja biblioteki KY-040 dla STM32 HAL
 */

#include "ky040.h"

// Sekwencje sygnałów (stałe prywatne)
static const uint8_t c_signalSequenceCW[MAXSEQUENCESTEPS] = {0b01, 0b00, 0b10, INITSTEP};
static const uint8_t c_signalSequenceCCW[MAXSEQUENCESTEPS] = {0b10, 0b00, 0b01, INITSTEP};

/**
 * @brief Inicjalizacja enkodera KY-040
 */
void KY040_Init(KY040_Handle_t* handle, 
                GPIO_TypeDef* clk_port, uint16_t clk_pin,
                GPIO_TypeDef* dt_port, uint16_t dt_pin)
{
    handle->clk_port = clk_port;
    handle->clk_pin = clk_pin;
    handle->dt_port = dt_port;
    handle->dt_pin = dt_pin;
    
    handle->state = 255;  // Stan nieokreślony (0b11111111) - wymusza inicjalizację przy pierwszym odczycie
    handle->lastResult = KY040_IDLE;
    handle->lastSequenceStartMillis = HAL_GetTick();
    handle->sequenceStep = 0;
    handle->direction = KY040_IDLE;
    handle->oldState = INITSTEP;
}

/**
 * @brief Sprawdź aktualny stan rotacji
 */
KY040_Direction_t KY040_CheckRotation(KY040_Handle_t* handle)
{
    uint8_t result = KY040_IDLE;
    
    if (handle->state != handle->oldState) { // Zmiana stanu?
        if (handle->sequenceStep == 0) { // Początek rotacji?
            if (handle->state == c_signalSequenceCW[0]) { // Początek CW
                handle->direction = KY040_CLOCKWISE;
                handle->sequenceStep = 1;
                handle->lastSequenceStartMillis = HAL_GetTick();
            }
            if (handle->state == c_signalSequenceCCW[0]) { // Początek CCW
                handle->direction = KY040_COUNTERCLOCKWISE;
                handle->sequenceStep = 1;
                handle->lastSequenceStartMillis = HAL_GetTick();
            }
        } else {
            switch (handle->direction) {
                case KY040_CLOCKWISE:
                    if (handle->state == c_signalSequenceCW[handle->sequenceStep]) {
                        handle->sequenceStep++;
                        if (handle->sequenceStep >= MAXSEQUENCESTEPS) { // Sekwencja ukończona
                            result = handle->direction;
                            handle->lastResult = result;
                            handle->direction = KY040_IDLE;
                            handle->sequenceStep = 0;
                        } else {
                            result = KY040_ACTIVE;
                        }
                    } else {
                        // Nieprawidłowa sekwencja
                        if (handle->state == INITSTEP) { // Reset w stanie spoczynku
                            handle->direction = KY040_IDLE;
                            handle->sequenceStep = 0;
                        }
                    }
                    break;
                    
                case KY040_COUNTERCLOCKWISE:
                    if (handle->state == c_signalSequenceCCW[handle->sequenceStep]) {
                        handle->sequenceStep++;
                        if (handle->sequenceStep >= MAXSEQUENCESTEPS) { // Sekwencja ukończona
                            result = handle->direction;
                            handle->lastResult = result;
                            handle->direction = KY040_IDLE;
                            handle->sequenceStep = 0;
                        } else {
                            result = KY040_ACTIVE;
                        }
                    } else {
                        // Nieprawidłowa sekwencja
                        if (handle->state == INITSTEP) { // Reset w stanie spoczynku
                            handle->direction = KY040_IDLE;
                            handle->sequenceStep = 0;
                        }
                    }
                    break;
                    
                default:
                    break;
            }
        }
        handle->oldState = handle->state;
    }
    
    // Zapobiegaj przepełnieniu (jak w oryginale)
    if (HAL_GetTick() - handle->lastSequenceStartMillis > PREVENTSLEEPMS) {
        handle->lastSequenceStartMillis = HAL_GetTick() - PREVENTSLEEPMS - 1;
    }
    
    return (KY040_Direction_t)result;
}

/**
 * @brief Odczytaj i zapisz aktualny stan pinów CLK/DT
 */
KY040_Direction_t KY040_GetRotation(KY040_Handle_t* handle)
{
    // Odczyt pinów: CLK (bit 1) | DT (bit 0)
    uint8_t clk_state = HAL_GPIO_ReadPin(handle->clk_port, handle->clk_pin) ? 1 : 0;
    uint8_t dt_state = HAL_GPIO_ReadPin(handle->dt_port, handle->dt_pin) ? 1 : 0;
    
    KY040_SetState(handle, (clk_state << 1) | dt_state);
    return KY040_CheckRotation(handle);
}

/**
 * @brief Pobierz i zresetuj ostatni ukończony kierunek rotacji
 */
KY040_Direction_t KY040_GetAndResetLastRotation(KY040_Handle_t* handle)
{
    __disable_irq();  // Odpowiednik cli()
    uint8_t result = handle->lastResult;
    handle->lastResult = KY040_IDLE;
    __enable_irq();   // Odpowiednik sei()
    
    return (KY040_Direction_t)result;
}

/**
 * @brief Pobierz zapisany stan pinów CLK/DT
 */
uint8_t KY040_GetState(KY040_Handle_t* handle)
{
    return handle->state;
}

/**
 * @brief Ustaw stan pinów CLK/DT - wywołuj z ISR
 */
void KY040_SetState(KY040_Handle_t* handle, uint8_t state)
{
      state &= 0x03;                    // tylko 2 bity: CLK|DT

    uint8_t prev = handle->state & 0x03;

    // 1. Ignoruj brak zmiany
    if (state == prev) {
        return;
    }

    // 2. Ignoruj skok z dwoma naraz zmienionymi bitami (np. 11 -> 00),
    //    to najczęściej odbicie styków, nie prawidłowy krok Gray-code
    uint8_t diff = prev ^ state;      // które bity się zmieniły?
    if (diff == 0x03) {               // 0b11 = dwa bity na raz
        return;
    }

    // 3. Jeśli przeszedł powyższe filtry, przyjmij nowy stan
    handle->state = state;
}

/**
 * @brief Sprawdź czy bezpieczne jest wejście w tryb uśpienia
 */
uint8_t KY040_ReadyForSleep(KY040_Handle_t* handle)
{
    __disable_irq();
    uint32_t lastStepMillis = handle->lastSequenceStartMillis;
    __enable_irq();
    
    return (HAL_GetTick() - lastStepMillis > PREVENTSLEEPMS) ? 1 : 0;
}
