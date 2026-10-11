# Mapa pinów STM32-BPM (Bluepill STM32F103C8)

Stan na branch TEST. Piny wg kodu (MX_*, stepper_hw, msp).

| Pin | Funkcja | Tryb | Dokąd |
|-----|---------|------|-------|
| PA4 | Silnik IN1 | OUT PP | ULN2003 IN1 |
| PA5 | Silnik IN2 | OUT PP | ULN2003 IN2 |
| PA7 | Silnik IN3 | OUT PP | ULN2003 IN3 |
| PA8 | Silnik IN4 | OUT PP | ULN2003 IN4 |
| PA6 | Jasność 7-seg (TIM3_CH1, 15 kHz) | AF PP | 7447 BLANK |
| PB0 | Buzzer (TIM3_CH3) | AF PP | buzzer |
| PA1 | Marker / oscyloskop | OUT PP | — |
| PC13 | LED diagnostyczny (active-low) | OUT PP | dioda na płytce |
| PB6 | OLED SCL (I2C1) | AF OD + PU | SSD1306 SCL |
| PB7 | OLED SDA (I2C1) | AF OD + PU | SSD1306 SDA |
| PB10 | MAX SCL (I2C2) | AF OD + PU | MAX30102 SCL |
| PB11 | MAX SDA (I2C2) | AF OD + PU | MAX30102 SDA |
| PB12 | MAX INT (nieużywany, polling) | IN PU | MAX30102 INT |
| PA9 | USART1 TX → ZEGAR (9600) | AF PP | ESP32 GPIO16 |
| PA10 | USART1 RX ← ZEGAR (9600) | IN PU | ESP32 GPIO17 |
| PA2 | USART2 TX → radar (256000) | AF PP | LD2410C RX (pin 2) |
| PA3 | USART2 RX ← radar (256000) | IN PU | LD2410C TX (pin 1) |
| PA13/PA14 | SWDIO / SWCLK | AF | ST-Link |
| 5V / 3V3 / G | zasilanie | — | silnik 5 V, logika 3,3 V, GND wspólna |

## LD2410C → Bluepill (TX/RX na krzyż)

| LD2410C | Bluepill | Uwaga |
|---------|----------|-------|
| 5 VCC | 5V | 5–12 V, wydajność > 200 mA |
| 4 GND | GND (dowolny) | masa wspólna, obowiązkowo |
| 1 TX | PA3 (RX) | poziom 3,3 V, wprost |
| 2 RX | PA2 (TX) | poziom 3,3 V, wprost |
| 3 OUT | — (wolny) | stan obecności HIGH/LOW, my bierzemy dane z UART |

Silnik 28BYJ-48 (typowe kolory): czerwony = VCC (+5 V, nie do STM32),
fazowe: niebieski / różowy / żółty / pomarańczowy → IN1–IN4
(kolejność faz u producenta bywa inna — w razie drgań zamienić parę).
