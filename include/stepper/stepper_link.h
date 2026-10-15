#ifndef STEPPER_LINK_H
#define STEPPER_LINK_H

/*
 * stepper / stepper_link — KOMUNIKACJA ZEGAR(ESP32) <-> STM32.
 *
 * !!! NIE RUSZA istniejacego protokolu: uzywa tylko publicznego
 *     frame_protocol_register() + frame_tx_send()/frame_tx_ack().
 *     Istniejace ramki 0xA0..A3/B0 i ich timing (PPG 50 Hz itd.) nietkniete.
 *
 * Nowe typy ramek (wolne, z zakresu 0xA0..0xBF dla kanalu STM32):
 *   0xB1  ZEGAR -> STM32  KOMENDA   payload = [cmd][args...]
 *     cmd 0x01 MOVE_REL : [steps int32 LE][speed u16 LE][accel u16 LE]  (9 B)
 *     cmd 0x02 MOVE_ABS : [pos   int32 LE][speed u16 LE][accel u16 LE]  (9 B)
 *     cmd 0x03 STOP     : brak argumentow                              (1 B)
 *     cmd 0x04 STATUS   : brak argumentow -> odpowiada 0xB2             (1 B)
 *     cmd 0x05 SET_ZERO : brak argumentow — adopcja biezacej pozycji
 *                         jako licznikowe 0 ramy (tylko na postoju,
 *                         w ruchu komenda odrzucana)                    (1 B)
 *     cmd 0x06 SET_SPAN : [speed u16 LE][accel u16 LE] — adopcja biezacej
 *                         pozycji jako 90 st (span=|pos|), trwaly zapis
 *                         od razu + autonomiczny powrot do 0 podana
 *                         predkoscia (tylko na postoju)                 (5 B)
 *     cmd 0x07 GET_REF  : brak argumentow -> odpowiada 0xB4             (1 B)
 *     cmd 0x08 MOVE_DEG : [deg u16 LE][speed u16 LE][accel u16 LE] —
 *                         cel = zero + deg*span/90 z WLASNEGO spanu;
 *                         nieskalibrowany -> odrzucenie (licznik)        (7 B)
 *     cmd 0x09 SET_SPEED: [speed u16 LE] — predkosc autonomicznego
 *                         powrotu (persist we flash truth);
 *                         dotyczy przyszlych jazd, bezwarunkowo         (3 B)
 *     cmd 0x0A SET_HOME : [deg u16 LE 0..360] — cel powrotu po resecie
 *                         (persist we flash truth + push 0xB4);
 *                         dotyczy przyszlych restartow, bezwarunkowo    (3 B)
 *     Po kazdej komendzie STM32 odsylia ACK 0xA2 [ok][0xB1] (jak brightness).
 *   0xB2  STM32 -> ZEGAR STATUS    payload = [busy 0/1][pos int32 LE]
 *                                          [flags: bit0=calOk]         (6 B)
 *   0xB3  STM32 -> ZEGAR DONE      payload = [pos int32 LE]             (4 B)
 *     DONE przychodzi DOKLADNIE RAZ po zakonczeniu ruchu (krawedz).
 *   0xB4  STM32 -> ZEGAR REF       payload = [zero int32 LE][span int32 LE]
 *                                          [flags: bit0=calOk, bit1=radarEn,
 *                                           bity2-3=czulosc OUT radaru]
 *                                          [homeDeg u16 LE][targetDeg u16 LE]
 *                                          [speed u16 LE][autoCloseS u16 LE] (17 B)
 *     Stary ZEGAR czyta krotszy prefiks (len-check) — zgodne wstecz.
 *     Wysylane po GET_REF, po SET_ZERO/SET_SPAN/SET_HOME/SET_AUTOCLOSE/
 *     SET_RADAR_SENS i raz po starcie.
 *     Prawda o ramie zyje w stepper_truth (flash STM32) — ZEGAR nie liczy mapy.
 *
 * KONTRAKT DLA ESP32 (do dopisania PO STRONIE ZEGAR, nie tu):
 *   ZEGAR pakuje MOVE:kroki,speed,accel z apki PC w ramke 0xB1 i forwarduje
 *   ja soft-UARTem 19200; DONE 0xB3 / STATUS 0xB2 wracaja ta sama droga.
 *   Ten plik po stronie STM32 jest gotowy — ESP32 dopiszesz pozniej.
 *
 * Watek: handler ramki (kontekst main loop) TYLKO kolejkuje komende
 * (statyczny ring, nigdy nie blokuje dekodera COBS).
 * Ruch wykonuje stepper_poll (co iteracje superloop, bez bramki —
 * harmonogramu pilnuje motion_poll przez next_us; latencja STOP
 * to jedna iteracja petli). STOP idzie na poczatek kolejki
 * i CZYSCI zalegle MOVE (e-stop: po STOP nic starego nie ruszy).
 * DONE wysylane raz na dojazd, z retry do 1 s gdyby ring TX byl pelny.
 *
 * USUWANIE MODULU: skasuj foldery include/stepper + src/stepper
 * i oznaczone linijki w main.c (SZUKAJ: SILNIK_KROKOWY). Nic wiecej.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Typy ramek steppera (przy integracji dopisz je tez w ZEGAR/STM32_Data). */
#define STEPPER_FRAME_CMD     0xB1U
#define STEPPER_FRAME_STATUS  0xB2U
#define STEPPER_FRAME_DONE    0xB3U
#define STEPPER_FRAME_REF     0xB4U

/* Komendy w ramce 0xB1. */
#define STEPPER_CMD_MOVE_REL  0x01U
#define STEPPER_CMD_MOVE_ABS  0x02U
#define STEPPER_CMD_STOP      0x03U
#define STEPPER_CMD_STATUS    0x04U
#define STEPPER_CMD_SET_ZERO  0x05U
#define STEPPER_CMD_SET_SPAN  0x06U
#define STEPPER_CMD_GET_REF   0x07U
#define STEPPER_CMD_MOVE_DEG  0x08U
#define STEPPER_CMD_SET_SPEED 0x09U
#define STEPPER_CMD_SET_HOME  0x0AU
#define STEPPER_CMD_SET_RADAR 0x0BU   /* cmd(1) + en(1): automat LD2410C */
#define STEPPER_CMD_SET_TARGET 0x0CU  /* cmd(1) + deg u16 LE: wybrany kat (flash) */
#define STEPPER_CMD_CAL_MODE  0x0DU   /* cmd(1) + en(1): kalibracja w toku —
                                       * wstrzymaj automat LD2410C (bez persistencji;
                                       * auto-timeout 30 s bez odswiezen; en=0 gasi) */
#define STEPPER_CMD_SET_AUTOCLOSE 0x0EU /* cmd(1) + sekundy u16 LE: czas auto-zjazdu
                                       * radaru (presety 30/180/600/2700; inne = fabryczny
                                       * 30 s). Persist we flash truth + push 0xB4 */
#define STEPPER_CMD_SET_RADAR_SENS 0x0FU /* cmd(1) + kod(1): czulosc OUT radaru
                                       * (0=SREDNIA, 1=WYSOKA, 2=MALA; inne = SREDNIA).
                                       * Persist (bity 1-2 flags truth) + aplikacja
                                       * w module + echo w 0xB2/0xB4 (bity 2-3 flags) */
#define STEPPER_CMD_SET_ONOFF 0x10U    /* cmd(1) + en(1): "plynne wylaczenie" —
                                       * bramka premium dimu 7-seg przy zjezdzie
                                       * (bit 3 flags truth; persist + echo 0xB4
                                       * bit4). Automat ruchu bez zmian. */

/* Rejestruje handler 0xB1 + zeruje ring. Wywolac raz w main()
 * PO frame_protocol_init(). Nie tyka UART ani innych handlerow. */
void stepper_init(void);

/* Bare-metal poll steppera. Wolac co iteracje superloop (nieblokujacy). */
void stepper_poll(void);

/* 1 gdy silnik wykonuje ruch. Uzywane przez warstwe radaru (sesje config
 * LD2410 blokuja superloop ~0.3-1.3 s — wolno je robic tylko na postoju,
 * inaczej kroki staja i ring RX gubi komendy). */
uint8_t stepper_is_busy(void);

/* Liczniki do debugowania BEZ UART (DEBUG_LOG nie wolno — pisze na UART1
 * i rozsypalby strumien COBS!). Podglad w debuggerze. */
extern volatile uint32_t g_stepper_rx_cmd;
extern volatile uint32_t g_stepper_rx_bad;
extern volatile uint32_t g_stepper_tx_status;
extern volatile uint32_t g_stepper_tx_done;
extern volatile uint32_t g_stepper_tx_ref;
extern volatile uint32_t g_stepper_steps_total;

#ifdef __cplusplus
}
#endif

#endif /* STEPPER_LINK_H */
