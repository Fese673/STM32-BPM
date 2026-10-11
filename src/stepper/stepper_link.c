/*
 * stepper / stepper_link — implementacja komunikacji + poll.
 * (Bare-metal: brak FreeRTOS. Kolejka = statyczny ring 4-slotowy,
 *  stepper_poll() wolany co iteracje superloop.)
 *
 * Kolejka komand: frame RX (producent, nigdy nie blokuje) ->
 * stepper_poll (konsument). Ruch: stepper_motion (trapez)
 * pompowany w pollu; DONE wysylane na krawedzi dojazdu (z retry 1 s).
 */

#include "stepper/stepper_link.h"
#include "stepper/stepper_hw.h"
#include "stepper/stepper_motion.h"
#include "stepper/stepper_truth.h"
#include "comm/frame_protocol.h"

#include "stm32f1xx_hal.h"

volatile uint32_t g_stepper_rx_cmd      = 0U;
volatile uint32_t g_stepper_rx_bad      = 0U;
volatile uint32_t g_stepper_tx_status   = 0U;
volatile uint32_t g_stepper_tx_done     = 0U;
volatile uint32_t g_stepper_tx_ref      = 0U;
volatile uint32_t g_stepper_steps_total = 0U;

#define STEPPER_QUEUE_LEN 4U
#define STEPPER_QUEUE_MASK (STEPPER_QUEUE_LEN - 1U)

typedef struct {
    uint8_t  cmd;
    int32_t  arg1;    /* steps (REL) albo pos (ABS); dla MOVE_DEG: stopnie */
    uint16_t speed;
    uint16_t accel;
} stepper_cmd_t;

/* Statyczny ring zamiast kolejki FreeRTOS. head = odczyt, tail = zapis.
 * Producent = frame RX (main-loop, nie ISR), konsument = stepper_poll —
 * ten sam kontekst, brak potrzeby sekcji krytycznej. */
static stepper_cmd_t s_cmd_ring[STEPPER_QUEUE_LEN];
static uint8_t s_cmd_head = 0U;
static uint8_t s_cmd_tail = 0U;
static uint8_t s_cmd_count = 0U;
static uint8_t s_cmd_ready = 0U;
static uint8_t s_last_cmd = 0U;     /* TEMP-DIAG: ostatnia zakolejkowana komenda */
static uint16_t s_last_speed = 0U;  /* TEMP-DIAG: jej predkosc */
static stepper_motion_t s_motion;

/* DONE jest krytyczne dla apki (czeka na nie sekwencer): jesli ring TX byl
 * pelny w momencie dojazdu, probujemy ponownie przez 1 s zamiast gubic. */
static uint8_t s_done_pending = 0;
static int32_t s_done_pos = 0;
static uint32_t s_done_deadline = 0;

static uint8_t cmd_push_back(const stepper_cmd_t *q)
{
    if (s_cmd_count >= STEPPER_QUEUE_LEN) return 0U;
    s_cmd_ring[s_cmd_tail] = *q;
    s_cmd_tail = (uint8_t)((s_cmd_tail + 1U) & STEPPER_QUEUE_MASK);
    ++s_cmd_count;
    return 1U;
}

static uint8_t cmd_push_front(const stepper_cmd_t *q)
{
    if (s_cmd_count >= STEPPER_QUEUE_LEN) return 0U;
    s_cmd_head = (uint8_t)((s_cmd_head - 1U) & STEPPER_QUEUE_MASK);
    s_cmd_ring[s_cmd_head] = *q;
    ++s_cmd_count;
    return 1U;
}

static uint8_t cmd_pop(stepper_cmd_t *q)
{
    if (s_cmd_count == 0U) return 0U;
    *q = s_cmd_ring[s_cmd_head];
    s_cmd_head = (uint8_t)((s_cmd_head + 1U) & STEPPER_QUEUE_MASK);
    --s_cmd_count;
    return 1U;
}

static void cmd_reset(void)
{
    s_cmd_head = 0U;
    s_cmd_tail = 0U;
    s_cmd_count = 0U;
}

static int32_t rd_i32le(const uint8_t *p)
{
    return (int32_t)((uint32_t)p[0]        |
                     ((uint32_t)p[1] << 8)  |
                     ((uint32_t)p[2] << 16) |
                     ((uint32_t)p[3] << 24));
}

static uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static void wr_i32le(uint8_t *p, int32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void tx_status(void)
{
    uint8_t pl[6];
    pl[0] = stepper_motion_busy(&s_motion) ? 1U : 0U;
    wr_i32le(&pl[1], stepper_motion_pos(&s_motion));
    pl[5] = truth_calibrated() ? STEPPER_REF_FLAG_CAL_OK : 0U;
    if (frame_tx_send(STEPPER_FRAME_STATUS, pl, sizeof(pl))) {
        ++g_stepper_tx_status;
    }
}

/* Rama odniesienia (prawda z truth): [zero i32][span i32][flags][homeDeg u16]. */
static void tx_ref(void)
{
    uint8_t pl[11];
    wr_i32le(&pl[0], truth_zero());
    wr_i32le(&pl[4], truth_span());
    pl[8] = truth_calibrated() ? STEPPER_REF_FLAG_CAL_OK : 0U;
    const uint16_t hd = truth_home_deg();
    pl[9] = (uint8_t)(hd & 0xFFU);
    pl[10] = (uint8_t)((hd >> 8) & 0xFFU);
    if (frame_tx_send(STEPPER_FRAME_REF, pl, sizeof(pl))) {
        ++g_stepper_tx_ref;
    }
}

static void tx_done(void)
{
    uint8_t pl[4];
    wr_i32le(pl, stepper_motion_pos(&s_motion));
    if (frame_tx_send(STEPPER_FRAME_DONE, pl, sizeof(pl))) {
        ++g_stepper_tx_done;
    } else {
        /* Ring TX pelny — nie gubimy DONE (apka czeka), tylko odloz retry. */
        s_done_pending = 1;
        s_done_pos = stepper_motion_pos(&s_motion);
        s_done_deadline = HAL_GetTick() + 1000U;
    }
}

static void tx_done_retry(void)
{
    uint8_t pl[4];
    if (!s_done_pending) return;
    if ((int32_t)(HAL_GetTick() - s_done_deadline) >= 0) {
        s_done_pending = 0;  /* po 1 s odpuszczamy — link i tak padl */
        return;
    }
    wr_i32le(pl, s_done_pos);
    if (frame_tx_send(STEPPER_FRAME_DONE, pl, sizeof(pl))) {
        s_done_pending = 0;
        ++g_stepper_tx_done;
    }
}

/* Handler ramki 0xB1 — kontekst main loop (frame_rx_process). ZERO
 * blokowania: tylko walidacja dlugosci + wrzut na ring. Ciezka robota
 * w stepper_poll. */
static void stepper_cmd_dispatch(uint8_t type, uint8_t seq,
                                 const uint8_t *payload, uint16_t len)
{
    (void)type;
    (void)seq;

    if (payload == NULL || len < 1U) {
        ++g_stepper_rx_bad;
        (void)frame_tx_ack(0U, STEPPER_FRAME_CMD);
        return;
    }

    /* Obrona: gdyby init nie zarejestrowal modulu, NACK zamiast HardFault. */
    if (!s_cmd_ready) {
        ++g_stepper_rx_bad;
        (void)frame_tx_ack(0U, STEPPER_FRAME_CMD);
        return;
    }

    stepper_cmd_t q = {0, 0, STEPPER_SPEED_DEF, STEPPER_ACCEL_DEF};
    uint8_t cmd = payload[0];

    switch (cmd) {
    case STEPPER_CMD_MOVE_REL:
    case STEPPER_CMD_MOVE_ABS:
        if (len != 9U) {   /* cmd(1) + i32(4) + u16(2) + u16(2) */
            ++g_stepper_rx_bad;
            (void)frame_tx_ack(0U, STEPPER_FRAME_CMD);
            return;
        }
        q.cmd   = cmd;
        q.arg1  = rd_i32le(&payload[1]);
        q.speed = rd_u16le(&payload[5]);
        q.accel = rd_u16le(&payload[7]);
        if (q.arg1 > STEPPER_MAX_MOVE)  q.arg1 = STEPPER_MAX_MOVE;
        if (q.arg1 < -STEPPER_MAX_MOVE) q.arg1 = -STEPPER_MAX_MOVE;
        break;

    case STEPPER_CMD_STOP:
    case STEPPER_CMD_STATUS:
    case STEPPER_CMD_SET_ZERO:
    case STEPPER_CMD_GET_REF:
        if (len != 1U) {
            ++g_stepper_rx_bad;
            (void)frame_tx_ack(0U, STEPPER_FRAME_CMD);
            return;
        }
        q.cmd = cmd;
        break;

    case STEPPER_CMD_SET_SPAN:
        if (len != 5U) {   /* cmd(1) + speed u16(2) + accel u16(2) */
            ++g_stepper_rx_bad;
            (void)frame_tx_ack(0U, STEPPER_FRAME_CMD);
            return;
        }
        q.cmd   = cmd;
        q.speed = rd_u16le(&payload[1]);
        q.accel = rd_u16le(&payload[3]);
        break;

    case STEPPER_CMD_MOVE_DEG:
        if (len != 7U) {   /* cmd(1) + deg u16(2) + speed u16(2) + accel u16(2) */
            ++g_stepper_rx_bad;
            (void)frame_tx_ack(0U, STEPPER_FRAME_CMD);
            return;
        }
        q.cmd   = cmd;
        q.arg1  = (int32_t)rd_u16le(&payload[1]);  /* stopnie 0..65535 */
        q.speed = rd_u16le(&payload[3]);
        q.accel = rd_u16le(&payload[5]);
        break;

    case STEPPER_CMD_SET_SPEED:
        if (len != 3U) {   /* cmd(1) + speed u16(2) */
            ++g_stepper_rx_bad;
            (void)frame_tx_ack(0U, STEPPER_FRAME_CMD);
            return;
        }
        q.cmd   = cmd;
        q.speed = rd_u16le(&payload[1]);
        break;

    case STEPPER_CMD_SET_HOME:
        if (len != 3U) {   /* cmd(1) + deg u16(2) */
            ++g_stepper_rx_bad;
            (void)frame_tx_ack(0U, STEPPER_FRAME_CMD);
            return;
        }
        q.cmd  = cmd;
        q.arg1 = (int32_t)rd_u16le(&payload[1]);  /* cel powrotu 0..360 */
        break;

    default:
        ++g_stepper_rx_bad;
        (void)frame_tx_ack(0U, STEPPER_FRAME_CMD);
        return;
    }

    /* STOP ma pierwszenstwo — na czolo kolejki. Reszta na koniec.
     * Ring pelny -> NACK, dekoder COBS nigdy nie staje. */
    uint8_t ok;
    if (q.cmd == STEPPER_CMD_STOP) {
        ok = cmd_push_front(&q);
    } else {
        ok = cmd_push_back(&q);
    }
    if (ok) {
        ++g_stepper_rx_cmd;
        s_last_cmd = q.cmd;      /* TEMP-DIAG */
        s_last_speed = q.speed;  /* TEMP-DIAG */
        (void)frame_tx_ack(1U, STEPPER_FRAME_CMD);
    } else {
        ++g_stepper_rx_bad;
        (void)frame_tx_ack(0U, STEPPER_FRAME_CMD);
    }
}

void stepper_init(void)
{
    stepper_hw_init();
    stepper_hw_release();
    stepper_motion_init(&s_motion);
    /* Prawda z flash: licznik startuje z odtworzonej pozycji (fizyka sie
     * nie zmienila, wiec licznik od razu znaczy to co trzeba). */
    {
        int32_t pos = 0L, span = 0L;
        uint8_t cal_ok = 0U;
        truth_init(&pos, &span, &cal_ok);
        stepper_motion_set_pos(&s_motion, pos);
        (void)span;
        (void)cal_ok;
    }
    s_cmd_head = 0U;
    s_cmd_tail = 0U;
    s_cmd_count = 0U;
    s_cmd_ready = 1U;
    frame_protocol_register(STEPPER_FRAME_CMD, stepper_cmd_dispatch);
}

/* TEMP-DIAG: odczyt do wyswietlenia na OLED (jeden test, potem usunać). */
void stepper_diag(uint8_t *cmd, uint16_t *speed, uint32_t *rxn,
                  int32_t *pos, uint8_t *busy)
{
    if (cmd) *cmd = s_last_cmd;
    if (speed) *speed = s_last_speed;
    if (rxn) *rxn = g_stepper_rx_cmd;
    if (pos) *pos = stepper_motion_pos(&s_motion);
    if (busy) *busy = stepper_motion_busy(&s_motion);
}

/* Bare-metal poll — wolac co iteracje superloop. BEZ bramki czasowej:
 * stepper_motion_poll() sam pilnuje harmonogramu krokow przez next_us
 * (rozdzielczosc ~us), wiec wołanie czesciej niz 1 ms tylko poprawia
 * precyzje (krok dokladnie w terminie, nie na najblizszym ticku HAL).
 * Przy multisekundowych blokadach superloop (np. erase flash ~20 ms)
 * zaleglosc nadrabia cap 4 interwalow w motion (bez "strzelania").
 * STOP z ringu ma latencje jednej iteracji petli. Nieblokujaca. */
void stepper_poll(void)
{
    static uint8_t booted = 0U;
    static uint8_t was_busy = 0U;
    stepper_cmd_t q;

    /* 0. Jednorazowo po starcie: oglos prawde (REF) + autonomiczny
     * powrot do zera gdy skalibrowany i nie stoi na zerze. */
    if (!booted) {
        booted = 1U;
        tx_ref();
        if (truth_home_needed()) {
            stepper_motion_move_abs(&s_motion, truth_home_steps(),
                                    (uint32_t)truth_home_speed(),
                                    (uint32_t)truth_home_accel());
        }
    }
    /* 1. Komendy z ringu (nie blokujemy — ruch musi byc pompowany). */
    while (cmd_pop(&q)) {
        switch (q.cmd) {
        case STEPPER_CMD_MOVE_REL:
            stepper_motion_move_rel(&s_motion, q.arg1, q.speed, q.accel);
            break;
        case STEPPER_CMD_MOVE_ABS:
            stepper_motion_move_abs(&s_motion, q.arg1, q.speed, q.accel);
            break;
        case STEPPER_CMD_STOP:
            s_done_pending = 0;
            stepper_motion_stop(&s_motion);
            stepper_hw_release();
            /* E-stop: wyczysc zalegle MOVE z ringu, zeby po STOP
             * silnik nie odpalal starych zadan. */
            cmd_reset();
            tx_status();   /* natychmiastowy odzew po STOP */
            break;
        case STEPPER_CMD_STATUS:
            tx_status();
            break;
        case STEPPER_CMD_MOVE_DEG: {
            /* Stopnie na kroki z WLASNEGO spanu (truth). Nieskalibrowany
             * albo zly zakres -> odrzucenie, ZEGAR jedzie fallbackiem. */
            uint16_t deg = (q.arg1 < 0) ? 0U : (uint16_t)q.arg1;
            if (deg > 360U) deg = 360U;
            if (!truth_calibrated()) {
                ++g_truth_rejected;
                break;
            }
            stepper_motion_move_abs(&s_motion, truth_deg_to_steps(deg),
                                    q.speed, q.accel);
            break;
        }
        case STEPPER_CMD_SET_ZERO:
            /* Adopcja zera TYLKO na postoju (w ruchu: drop, nie zgaduj). */
            if (stepper_motion_busy(&s_motion)) {
                ++g_truth_rejected;
                break;
            }
            stepper_motion_set_pos(&s_motion, 0L);
            if (truth_set_zero()) tx_ref();
            else ++g_truth_rejected;
            break;
        case STEPPER_CMD_SET_SPAN:
            /* Adopcja 90 stopni TYLKO na postoju + zapis od razu
             * + autonomiczny powrot do 0 podana predkoscia. */
            if (stepper_motion_busy(&s_motion)) {
                ++g_truth_rejected;
                break;
            }
            if (truth_set_span(stepper_motion_pos(&s_motion))) {
                tx_ref();
                stepper_motion_move_abs(&s_motion, truth_zero(),
                                        q.speed, q.accel);
            } else {
                ++g_truth_rejected;
            }
            break;
        case STEPPER_CMD_GET_REF:
            tx_ref();
            break;
        case STEPPER_CMD_SET_SPEED:
            /* Preferencja UI: dotyczy przyszlych jazd (powrot do zera),
             * wiec bezwarunkowo — nawet w ruchu (zapis odroczony). */
            truth_set_speed(q.speed, stepper_motion_pos(&s_motion),
                            stepper_motion_busy(&s_motion));
            break;
        case STEPPER_CMD_SET_HOME: {
            /* Cel powrotu po resecie (double-click presetu w Guitionie).
             * Bezwarunkowo (preferencja, nie ruch); persist + oglos REF. */
            uint16_t hd = (q.arg1 < 0) ? 0U : (uint16_t)q.arg1;
            if (hd > 360U) hd = 360U;
            truth_set_home(hd, stepper_motion_pos(&s_motion),
                           stepper_motion_busy(&s_motion));
            tx_ref();
            break;
        }
        default:
            break;
        }
    }

    /* 2. Pompa ruchu. Krawedz DONE -> ramka 0xB3 + zimne cewki.
     * Krawedz busy 1->0 (DONE albo STOP) -> uzbrojenie zapisu prawdy
     * (truth zapisze 2 s po postoju, w ticku nizej). */
    if (stepper_motion_busy(&s_motion)) {
        was_busy = 1U;
        int32_t before = stepper_motion_pos(&s_motion);
        if (stepper_motion_poll(&s_motion, stepper_hw_now_us())) {
            stepper_hw_release();
            tx_done();
        }
        int32_t after = stepper_motion_pos(&s_motion);
        g_stepper_steps_total += (uint32_t)(after >= before ? after - before
                                                            : before - after);
    } else {
        /* Idle: reszta superloop i tak dostaje CPU co 1 ms (bramka wyzej).
         * Ruch startuje w <2 ms od komendy — niezauwazalne przy 9600 baud. */
        tx_done_retry();  /* ewentualny zalegly DONE */
        if (was_busy) {
            was_busy = 0U;
            truth_on_motion_end(stepper_motion_pos(&s_motion));
        }
        truth_task_tick(stepper_motion_pos(&s_motion), 0U);
    }
}
