#ifndef __OLED_H
#define __OLED_H

// SSD1306 na magistrali I2C1. HAL przyjmuje adres w formacie 8-bitowym
// (7-bitowe adresy, bit R/W w bit0): 0x3C(SA0=LOW) = 0x78, 0x3D(SA0=HIGH) = 0x7A.
//
// oled_addr to JEDYNE źródło prawdy o adresie — ustawia go OLED_Detect()
// na pierwszy adres z listy kandydatów, który odpowiedział ACK. Caly
// sterownik (init, zapisy komend, łańcuch odświeżania) używa tej zmiennej,
// więc adres fizyczny modułu nie jest wpisany nigdzie sztywno.
extern volatile uint8_t oled_addr;

// Liczba i lista kandydatów do detekcji: 0x3C (SA0=LOW) = 0x78, 0x3D (SA0=HIGH) = 0x7A.
#define OLED_ADDR_CANDIDATES_COUNT  2U
extern const uint8_t oled_addr_candidates[OLED_ADDR_CANDIDATES_COUNT];

// Tryby zapisu do kontrolera SSD1306
#define OLED_CMD 0  // Zapis komendy konfiguracyjnej (Co=0, DC=0)
#define OLED_DATA 1 // Zapis danych pikseli do GDDRAM (Co=0, DC=1)

//-----------------OLED---------------- (Definicje portów OLED)

//OLED (Funkcje sterowania OLED)

// Niskopoziomowa funkcja zapisu bajtu do OLED
// dat: bajt danych do wysłania
// cmd: tryb (OLED_CMD=komenda, OLED_DATA=dane pikseli)
void OLED_WR_Byte(uint8_t dat, uint8_t cmd);

// Inicjalizacja wyświetlacza OLED (reset, konfiguracja SSD1306)
void OLED_Init(void);

// Wyczyszczenie całego ekranu (wypełnienie zerami - wszystkie piksele OFF)
void OLED_Clear(void);

// Wyświetlenie ciągu znaków ASCII na ekranie
// x: pozycja X (0-127 pikseli)
// y: pozycja Y (0-7 stron po 8 pikseli)
// str: wskaźnik na string (tablica znaków zakończona \0)
void OLED_ShowString(uint8_t x, uint8_t y, const char *str);

// Ustawienie pozycji kursora zapisu (GDDRAM)
// x: kolumna (0-127)
// y: strona (0-7, każda strona = 8 pikseli w pionie)
void OLED_Set_Pos(uint8_t x, uint8_t y);


// --- Funkcje dedykowane dla projektu pomiaru tętna ---

// Wyświetlenie ikony serca (duże/małe)
// showOrNot: 0 = duże serce (uderzenie), 1 = małe serce (spoczynek)
void OLED_showHeart(uint8_t showOrNot);

// Wyświetlenie pojedynczej cyfry w dużym rozmiarze
// which: pozycja cyfry (0-5: tętno i SpO2)
// num: wartość cyfry (0-9) lub 10 (spacja)
void OLED_showNum(uint8_t which, uint8_t num);

// Rysowanie wykresu fali PPG (oscylogram tętna)
// value: amplituda sygnału (różnica max30102_getDiff())
void OLED_drawChart(float value);

uint8_t OLED_GetError(void);
// Watchdog łańcucha odświeżania (nieblokujący, do wołania co cykl taska).
// Wykrywa skleszczony łańcuch bez flagi błędu i wymusza recovery.
void OLED_WatchdogPoll(void);
void OLED_Kick(void);
// Nieblokujący recovery (maszyna stanów na kilka cykli taska, bez HAL_Delay).
// Wołany w każdym cyklu taska dopóki OLED_GetError() != 0.
void OLED_Recover(void);

// Zombie mode: 0 = urządzenie wykryte, 1 = nie wykryte (inicjalizacja i zapis pomijane)
extern volatile uint8_t oled_zombie;

// Szybka detekcja urządzenia na I2C1 (próbuje 0x78 i 0x7A).
// Ustawia oled_addr na działający adres. Zwraca 1 = OK, 0 = brak.
uint8_t OLED_Detect(void);


#endif
