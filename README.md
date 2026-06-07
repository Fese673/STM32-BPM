# STM32-BPM

Monitor tętna i SpO2 z zegarem — projekt oparty na STM32F103C8 (BluePill).

## Sprzęt

| Element | Interfejs | Piny |
|---------|-----------|------|
| MAX30102 (tętno/SpO2) | I2C2 | PB10 (SCL), PB11 (SDA), PB12 (INT) |
| OLED SSD1306 | I2C1 | PB6 (SCL), PB7 (SDA) |
| Enkoder KY-040 | GPIO | PA6 (CLK), PA4 (DT) |
| ESP32 "ZEGAR" | UART1 | PA9 (TX→GPIO16), PA10 (RX←GPIO17) |
| Buzzer | TIM3_CH3 | PB0 (PWM 2kHz) |
| LED | GPIO | PC13 (diagnostyczny), PA7 (testowy) |

## Funkcje

- Wykres PPG, tętno i SpO2 z czujnika MAX30102
- Dane (BPM, SpO2, przebieg PPG) wysyłane do zegara ESP32 po UART1
- Przełączanie stron enkoderem KY-040
- Buzzer przy wykryciu pulsu

## Budowanie

```bash
# PlatformIO
pio run

# IAR Embedded Workbench
# Otwórz EWARM/Project.eww
```

## Struktura

```
src/main.c          — pętla główna, inicjalizacja
src/max30102.c      — sterownik czujnika tętna
src/oled.c          — sterownik OLED SSD1306
src/KY040.c         — enkoder obrotowy
src/Uart1.c         — UART1 (PPG stream)
src/ESP32_FLASHER.c — flasher ESP32 (prototyp)
include/            — nagłówki modułów
Drivers/            — STM32F1xx HAL + CMSIS
```

## Zależności

- STM32 HAL (CubeMX)
- PlatformIO: `ststm32` + `stm32cube`
