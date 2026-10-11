/**
 * Handler ramki 0xB0 (Brightness) z kanału ZEGAR↔STM32.
 *
 * 1. Walidacja: payload_len == 1, value <= 100
 * 2. PWM_Reg_SetDuty(value) — z wbudowaną inwersją dla 7447
 * 3. ACK do ZEGAR: ramka 0xA2 z payload [ok, related_type]
 */

#include "brightness_handler.h"
#include "pwm_reg.h"
#include "frame_protocol.h"

void brightness_handler_init(void)
{
    frame_protocol_register(FRAME_TYPE_BRIGHTNESS, brightness_handler_dispatch);
}

void brightness_handler_dispatch(uint8_t type, uint8_t seq,
                                 const uint8_t *payload, uint16_t len)
{
    (void)type;
    (void)seq;

    /* Payload = [value 0..100] */
    if (len != 1U || payload == NULL) {
        (void)frame_tx_ack(0U, FRAME_TYPE_BRIGHTNESS);  /* NACK */
        return;
    }

    uint8_t value = payload[0];
    if (value > 100U) {
        (void)frame_tx_ack(0U, FRAME_TYPE_BRIGHTNESS);
        return;
    }

    PWM_Reg_SetDuty(value);
    (void)frame_tx_ack(1U, FRAME_TYPE_BRIGHTNESS);      /* ACK */
}
