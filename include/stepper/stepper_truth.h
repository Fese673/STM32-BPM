#ifndef STEPPER_TRUTH_H
#define STEPPER_TRUTH_H

/*
 * silnik_krokowy / stepper_truth — JEDYNE ZRODLO PRAWDY o pozycji ekranu.
 *
 * Problem, ktory rozwiazuje: licznik krokow w stepper_motion zyl tylko w RAM
 * i po odcieciu zasilania STM32 budzil sie z pos=0, nie wiedzac gdzie ekran
 * fizycznie stoi. ZEGAR trzymal lustro w NVS, ale lustro potrafilo sie
 * rozjechac z rzeczywistoscia (clobber slepym cache'em, urwany zapis).
 * Ten modul przenosi prawde do sterownika silnika:
 *   - pozycja, span (rozpietosc 0..90 st) i flaga kalibracji zyja we flash STM32,
 *   - zapis 2 s po koncu DOWOLNEGO ruchu (DONE albo STOP),
 *   - po starcie licznik jest odtwarzany z flash — jest prawda z konstrukcji,
 *   - kalibracja: SET_ZERO adoptuje biezaca pozycje jako licznikowe 0,
 *     SET_SPAN adoptuje biezaca pozycje jako 90 stopni,
 *   - MOVE_DEG liczy cel z WLASNEGO spanu (zero + deg*span/90).
 *
 * Storage: ostatnia strona flash 64 KB BluePilla (0x0800FC00, 1 KB),
 * dwa sloty A/B po 32 B z {magic, seq, crc16-CCITT}. Urwany zapis przy
 * gasnacym zasilaniu wykrywa CRC — bierzemy starszy wazny slot.
 * Zapis tylko gdy pozycja sie zmienila (oszczednosc cykli erase ~10k).
 *
 * Watek: WSZYSTKO w kontekscie stepper_poll (ten sam kontekst co ruch).
 * truth_task_tick() wolac co obrót petli idle. Zadnych operacji flash
 * z ISR. Sam zapis flash stalluje CPU ~20 ms (erase) — dzieje
 * sie wylacznie na postoju, DONE/STATUS maja retry, PPG najwyzej zgubi probki.
 *
 * Licznik krokow (s_motion.pos) pozostaje wlascicielem "zywej" pozycji;
 * ten modul jest wlascicielem PAMIECI (restore/save) i RAMY (zero/span).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Flaga w ramce 0xB2/0xB4: rama skalibrowana. */
#define STEPPER_REF_FLAG_CAL_OK 0x01U
/* Flaga w ramce 0xB2/0xB4: automat LD2410C wlaczony. */
#define STEPPER_REF_FLAG_RADAR_EN 0x02U
/* Bit wewnetrznego pola flags slotu flash. */
#define TRUTH_FLAG_RADAR_EN 0x01U
/* Bity 1-2 pola flags: preset czulosci OUT radaru (0/1/2 = SREDNIA/WYSOKA/
 * MALA). Stare sloty maja tam 0 (home_accel=1000 / flags bez sens) -> SREDNIA. */
#define TRUTH_FLAG_RADAR_SENS_SHIFT 1U

/* Odtworzenie z flash. Wywolac raz w stepper_init() PRZED startem
 * petli. Zwraca przez wskazniki: pos (do wstawienia w licznik),
 * span, calOk (0/1). Brak waznego slotu = fabrycznie: 0, 0, 0. */
void truth_init(int32_t *pos_out, int32_t *span_out, uint8_t *cal_ok_out);

/* Gettery ramy (do REF 0xB4 i MOVE_DEG). */
int32_t truth_zero(void);   /* zawsze 0 po adopcji (licznik sie zeruje) */
int32_t truth_span(void);
uint8_t truth_calibrated(void);

/* SET_ZERO: adopcja biezacej pozycji jako zera ramy. Wywolywac z taska
 * gdy idle; zwraca 1 gdy przyjeto (pozycja fizyczna ma stac sie licznikowym
 * 0 — WYWOLUJACY zeruje licznik przez stepper_motion_set_pos(m, 0)). */
uint8_t truth_set_zero(void);

/* SET_SPAN: adopcja biezacej pozycji (pos90 w licznikowym zerze) jako 90 st.
 * span = |pos90|, calOk = (span >= 64). Trwaly zapis OD RAZU (kalibracja
 * jest rzadka). Zwraca 1 gdy przyjeto i zapisano. */
uint8_t truth_set_span(int32_t pos90);

/* Przeliczenie stopni na kroki z WLASNEGO spanu. Wywolujacy sprawdza
 * truth_calibrated() przed uzyciem (nieskalibrowany -> NACK). */
int32_t truth_deg_to_steps(uint16_t deg);

/* Krawedz koniec-ruchu: uzbrojenie zapisu (pozycja + tick). Faktyczny zapis
 * robi truth_task_tick() po 2 s postoju. Wywolac na przejsciu busy 1->0. */
void truth_on_motion_end(int32_t pos);

/* Pompa: wolac co obrót stepper_poll z (curPos, busy). Nieblokujaca. */
void truth_task_tick(int32_t cur_pos, uint8_t busy);

/* Czy po starcie nalezy autonomicznie wrocic do zera (skalibrowany
 * i odtworzona pozycja != 0). Wywolac raz po starcie taska; konsumuja flage. */
uint8_t truth_home_needed(void);

/* Predkosc autonomicznego powrotu: OSTATNIA ustawiona przez ZEGAR (SET_SPEED,
 * persist we flash). Domyslnie 400. Dzieki temu powrot po restarcie jedzie
 * tak szybko jak jezdzil operator — nie na sztywnej wartosci. */
uint16_t truth_home_speed(void);
uint16_t truth_home_accel(void);

/* Ustawienie predkosci powrotu (komenda SET_SPEED z ZEGAR-a). Niezalezne
 * od ruchu (dotyczy przyszlych jazd) — wolac o dowolnej porze z taska.
 * RAM od razu; zapis flash OD RAZU na postoju albo 2 s po postoju w ruchu
 * (erase stalluje CPU ~20 ms — nie w trakcie krokow). Zmiany rzadkie. */
void truth_set_speed(uint16_t speed, int32_t cur_pos, uint8_t busy);

/* Cel powrotu po resecie w stopniach (komenda SET_HOME z ZEGAR-a, default 0).
 * Persist we flash (pole home_deg w slocie — wczesniej rsvd, ten sam layout,
 * stare sloty czytaja sie jako home=0). Auto-home jedzie na
 * truth_home_steps(). */
uint16_t truth_home_deg(void);
void truth_set_home(uint16_t deg, int32_t cur_pos, uint8_t busy);
/* Kroki celu powrotu: skalibrowany ? zero+home*span/90 : 0. */
int32_t truth_home_steps(void);

/* Automat LD2410C (flaga z przycisku na Guitionie, persist we flash
 * w polu flags slotu — ten sam layout, stare sloty czytaja sie jako OFF).
 * Zapis jak SET_SPEED: od razu na postoju, odroczony w ruchu. */
uint8_t truth_radar_en(void);
void truth_set_radar_en(uint8_t en, int32_t cur_pos, uint8_t busy);

/* Preset czulosci OUT radaru (0=SREDNIA, 1=WYSOKA, 2=MALA; inna = 0).
 * Persist w bitach 1-2 pola flags slotu (ten sam layout — stare sloty
 * czytaja sie jako SREDNIA). Zapis jak SET_SPEED. Uzywane przez
 * ld2410_radar_init/request (progi bramek w wrapperze radaru). */
uint8_t truth_radar_sens(void);
void truth_set_radar_sens(uint8_t code, int32_t cur_pos, uint8_t busy);

/* WYBRANY kat z UI (single-click presetu; persist we flash). Sluzy
 * automatowi LD2410C (otwarcie) i wraca echem REF do HMI. Default 45
 * (0xFFFF/poza zakresem w slocie = default). Zapis jak SET_SPEED. */
uint16_t truth_target_deg(void);
void truth_set_target(uint16_t deg, int32_t cur_pos, uint8_t busy);

/* Czas auto-zjazdu radaru (cisza OUT -> 0 st), sekundy. Tylko presety
 * 30/120/300/600 (UI); inna wartosc = fabryczna 30. Persist we flash
 * (pole auto_close_s slotu v3; stare sloty migruja z defaultem).
 * Zapis jak SET_HOME: od razu na postoju, odroczony w ruchu. */
uint16_t truth_auto_close_s(void);
void truth_set_auto_close(uint16_t s, int32_t cur_pos, uint8_t busy);

/* Diagnostyka BEZ UART (podglad w debuggerze). */
extern volatile uint32_t g_truth_saves;
extern volatile uint32_t g_truth_save_fail;
extern volatile uint32_t g_truth_rejected;

#ifdef __cplusplus
}
#endif

#endif /* STEPPER_TRUTH_H */
