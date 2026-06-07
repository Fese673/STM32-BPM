# ESP32_FLASHER — Specyfikacja protokołu STM32 -> ESP32

Poniżej opisuję koncept i wymagane kroki protokołu, który ma realizować STM32 po stronie wysyłania firmware'u do ESP32.

## 1. Sekwencja wejścia w tryb Boot (Hardware Handshake)

Aby ESP32 "usłyszało" STM32, wykonaj następujący taniec na pinach:

1. GPIO0 -> LOW (trzymaj nisko)
2. EN (Reset) -> LOW (zresetuj)
3. Czekaj 100 ms
4. EN (Reset) -> HIGH (uruchom)
5. Czekaj 50 ms
6. GPIO0 -> HIGH (można puścić — ESP w ROM bootloaderze)

## 2. Formuła obliczania czasu wgrywania

Czas $T$ zależy od prędkości bitowej $B$ i rozmiaru pliku $S$:

$$T \approx \frac{S \times 10}{B}$$

(Mnożymy przez 10, bo 8 bitów danych + start bit + stop bit)

Przykładowe wartości dla 1 MB (orientacyjne):

- `115200` → ~87 sekund — bardzo stabilne
- `460800` → ~22 sekundy — zalecane dla kabli > 10 cm
- `921600` → ~11 sekund — wymaga krótkich połączeń

## 3. Dlaczego MD5 jest kluczowe?

Plik binarny generowany przez PlatformIO może zawierać stopkę z sumą kontrolną. Jeśli STM32 prześle choć jeden bit błędnie (np. przez zakłócenia na kablach UART), ESP32 może odrzucić cały obraz i nie wystartować.

Rekomendacja: STM32 powinien czytać plik z karty SD blokami po 4096 bajtów (rozmiar sektora Flash w ESP32) i wysyłać te bloki przez protokół esptool/SLIP. Po wysłaniu warto potwierdzać CRC/MD5.

## Ogólny przepływ (high-level)

1. Weryfikacja pliku (MD5/CRC) na SD
2. Wejście ESP32 w tryb bootloader (hardware handshake)
3. Synchronizacja (ramka SYNC opakowana w SLIP)
4. (Opcjonalnie) Negocjacja większej prędkości UART
5. FLASH_BEGIN (przekazanie rozmiaru i liczby bloków)
6. Pętla: czytaj 4096B z SD -> wyślij FLASH_DATA
7. FLASH_END -> sprawdzenie sum kontrolnych/response
8. Wyjście z trybu bootloader (GPIO0 HIGH + reset)

## Przykładowy fragment kodu (użytkownik)

```c
#include <Arduino.h> // Można łatwo zaadaptować do STM32 HAL

// Definicje pinów (dostosuj do swojego STM32)
#define ESP32_EN_PIN    PA0
#define ESP32_GPIO0_PIN PA1
#define UART_BAUD_INIT  115200
#define UART_BAUD_FAST  921600

// Komendy protokołu SLIP (używane przez esptool)
const uint8_t ESP_SYNC_REQ[] = {
    0x00, 0x08, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 
    0x07, 0x07, 0x12, 0x20, 0x55, 0x55, 0x55, 0x55, 
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 
    0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55, 0x55
};

void enterEsp32BootMode() {
    pinMode(ESP32_EN_PIN, OUTPUT);
    pinMode(ESP32_GPIO0_PIN, OUTPUT);

    digitalWrite(ESP32_GPIO0_PIN, LOW);
    digitalWrite(ESP32_EN_PIN, LOW);
    delay(100);
    digitalWrite(ESP32_EN_PIN, HIGH);
    delay(50);
}

bool syncWithEsp32() {
    Serial1.begin(UART_BAUD_INIT);
    while(Serial1.available()) Serial1.read();
    Serial1.write(0xC0);
    Serial1.write(ESP_SYNC_REQ, sizeof(ESP_SYNC_REQ));
    Serial1.write(0xC0);
    delay(100);
    if (Serial1.available() > 0) {
        return true;
    }
    return false;
}

// ... reszta logiki: FLASH_BEGIN, FLASH_DATA, FLASH_END ...
```

## Uwagi końcowe

- Pliki `include/ESP32_FLASHER.h` i `src/ESP32_FLASHER.c` w repo są szkicami (stuby). Nie są wstępnie integrowane z `main.c` — możesz zaimplementować HAL i podłączyć je później.
- Następny krok: zaimplementować konkretne operacje GPIO (STM32 HAL) i IO UART/SD oraz obliczanie MD5/CRC.
