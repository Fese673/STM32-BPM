# Skill: Bug Verification & Fix

Agent specjalizujący się w weryfikacji zgłoszonych przez użytkownika błędów, ocenie ich realności, naprawie i archiwizacji.

## Zasady ogólne

- Użytkownik zgłasza podejrzenie błędu — agent NIE szuka bugów na własną rękę
- Agent weryfikuje czy bug jest REALNY (analiza kodu, trace przepływu)
- Jeśli bug istnieje: naprawia + ERROR_ARCHIVE
- Jeśli bug to nadinterpretacja: wyjaśnia DLACZEGO nie naprawia
- Zawsze podaje krótkie podsumowanie użytkownikowi

## Workflow

### Faza 1: Odbierz zgłoszenie

Użytkownik podaje opis błędu. Agent:

1. Czyta ze zrozumieniem — user często opisuje objaw, nie przyczynę Chociaż to zależy od tego co napisze 
2. Zadaje pytania jeśli opis jest niejasny ale i tak próbuje zrozumieć problem 
3. Lokalizuje dotknięte pliki (grep, read)

### Faza 2: Weryfikacja

Dla każdego podejrzenia:

1. **Prześledź przepływ danych** — od źródła do miejsca wyświetlenia/zapisania
2. **Sprawdź wszystkie ścieżki** — `if/else`, guard clauses, early returns
3. **Zidentyfikuj race conditions** — FreeRTOS taski, współdzielone zmienne
4. **Określ realny impact**:
   - CRITICAL: dane gubione, system nie działa, reset
   - HIGH: funkcja nie działa jak oczekiwano, ale nie crash
   - LOW: kosmetyka, performance, rzadki edge case

#### Jeśli bug jest REALNY:

- Przedstaw użytkownikowi analizę: `PROBLEM: ..., CAUSE: ..., EFEKT: ...`
- Zaproponuj fix z uzasadnieniem (dlaczego TAK, a nie inaczej)
- Zaczekaj na akceptację lub implementuj od razu

#### Jeśli bug to NADINTERPRETACJA:

- Wytłumacz DOCKŁADNIE dlaczego problem nie występuje
- Pokaż w kodzie dowód (linijka, wartość, guard który to łapie)
- Nie naprawiaj

### Faza 3: Implementacja

1. **Plan** — wypisz kroki przed pierwszym edit
2. **Edycja** — jedna logiczna zmiana na raz
3. **Build** — `pio run -e esp32dev` po każdej grupie zmian
4. **Cofnij jeśli build fail** — `git checkout -- <file>` i popraw Ostrożnie
5. **Unikaj replaceAll** — niebezpieczne dla słów występujących w wielu kontekstach

Zasady kodu:

- Nie używaj sub agentów
- Po każdej edycji sprawdź czy build przechodzi
- Jeśli build fail → czytaj error, napraw, nie ignoruj

### Faza 4: Archiwizacja

Po udanej naprawie:

1. Przeczytaj `ERROR_ARCHIVE/ERROR_LOG.md` i `ERROR_ARCHIVE/ERROR_ARCHIVE_GUIDE.md`
2. Dodaj nowy wpis na GÓRĘ pliku (za nagłówkiem)
3. Użyj formatu z GUIDE:
   ```markdown
   ---
   ### [ID: ERR_XXX] | [MODUŁ] | IMPACT: [LOW/HIGH/CRITICAL]
   **Files:** `[Plik1.cpp, Plik2.h]`

   **PROBLEM:** [Opis — co się działo?]
   **CAUSE:** [Analiza techniczna — dlaczego?]
   **LOGIC_CHANGE:**
   - [Konkretna zmiana — punkt 1]
   - [Konkretna zmiana — punkt 2]
   **VERIFICATION:** [Jak sprawdzić że działa?]
   ---
   ```
4. Sprawdź kolejność ID (najwyższy numer to najnowszy)
5. Upewnij się że nie ma double separatorów (`---` dwa razy pod rząd)

### Faza 5: Odpowiedź

Po wszystkim napisz krótkie podsumowanie:

```
## Podsumowanie

| ID | Problem | Impact | Status |
|----|---------|--------|--------|
| ERR_003 | ... | CRITICAL | Naprawiono |
| — | Bug XYZ | — | Nadinterpretacja — wyjaśnienie: ... |
```

## Wskazówki z naszej sesji

### Przykład: NTP nie aktualizuje ClockService

User zgłosił: `setTimeRefs(Clock::hours(), ...)` nie działa.

Weryfikacja:
- `Clock::hours()` zwraca `int` (rvalue) → nie może bindować do `int&`
- Kompilator wybiera value-overload
- `pHours = &s_hours` → wskaźnik na LOKALNĄ kopię
- Po NTP: `*pHours = timeinfo.tm_hour` zapisuje do lokalnej kopii
- **BUG REALNY — CRITICAL**

### Przykład: loops<60 w catch-up

User zgłosił: limit 60s gubi sekundy.

Weryfikacja: REALNY — po 61s zawieszenia 1s jest stracona na zawsze. Naprawa: usunąć limit.

### Przykład: alarm podczas catch-up

User zgłosił: alarm nie zadzwoni bo `seconds==0` przechodzi w catch-up loop.

Weryfikacja: **NADINTERPRETACJA** — catch-up działa tylko w gałęzi `timeSeeded && !timeValid`. Warunek alarmu wymaga `timeValid == true`. Gdy `timeValid == true`, catch-up nie jest używany — czas czytany z system time. Alarm zadzwoni przy najbliższym tickClock po NTP.

### Typowe wzorce bugów w tym projekcie

| Wzorzec | Gdzie szukać |
|---------|-------------|
| Global externs rozrzucone po plikach | `UI_Draw.h`, `AppRuntime.h`, `ClockAlarmService.cpp` |
| Get-modify-set bez atomowości | `Clock::hours()` → zmień → `Clock::set()` |
| Display prioritizes system time over app time | `if (systemTimeValid) localtime_r(...)` |
| Guard clause zatrzymuje czas | `if (appState == STATE_SET_TIME) return` w tickClock |
| Pointer-based data flow zamiast direct call | `pHours = &s_hours` w WiFiSync |

## Kontekst projektu

- ESP32 + FreeRTOS, Arduino framework
- Build: `pio run -e esp32dev`
- Archiwum błędów: `ERROR_ARCHIVE/ERROR_LOG.md`
- Template: `ERROR_ARCHIVE/ERROR_ARCHIVE_GUIDE.md`
- AGENTS.md — główny instruction file
- ClockService — singleton dla czasu (Clock:: namespace)
- WiFiSync — NTP + WiFi, woła Clock::set() po NTP
