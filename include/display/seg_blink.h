#ifndef SEG_BLINK_H
#define SEG_BLINK_H

#include <stdint.h>
#include <stdbool.h>

/*
 * seg_blink — blink jasnosci 7-seg (alarm), ramka 0xB5 z ZEGARA.
 *
 * Problem: stara sygnalizacja alarmu blankowala CYFRY (naprzemiennie czyscila
 * zawartosc wyswietlacza) — wygladalo to jak awaria. Teraz cyfry pozostaja
 * stabilne (generuje je ZEGAR po SPI), a alarm pulsuje JASNOSCIA przez PWM:
 * warstwa nad PWM_Reg nadpisuje "zastosowana" wartosc miedzy jasnoscia
 * bazowa (user/dim radaru) a ~2%. Dim od radaru i jasnosc uzytkownika dzialaja
 * bez zmian pod spodem (baza w PWM_Reg_SetDuty).
 *
 * Bezpieczenstwo: TTL 5 s — brak odswiezenia (ZEGAR padl / link umarl /
 * restart ZEGARA) = auto-OFF i powrot do bazy. ZEGAR wysyla heartbeat co 2 s
 * podczas dzwonienia. Po starcie STM32 blink jest ZAWSZE wylaczony.
 */

/* Rejestruje handler ramki 0xB5. Wywolac raz w main() po frame_protocol_init. */
void seg_blink_init(void);

/* Ustawia stan blinku (z handlera 0xB5). KAZDE wywolanie odswieza TTL
 * (takze duplikaty tego samego stanu — one tylko przedluzaja zywotnosc). */
void seg_blink_set(bool en);

/* Superloop: faza blinku + TTL. Tania — sama sie bramkuje do ~50 Hz. */
void seg_blink_poll(void);

/* Diagnostyka. */
bool seg_blink_active(void);

#endif /* SEG_BLINK_H */
