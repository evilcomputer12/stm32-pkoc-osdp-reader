#include <Arduino.h>
#include "feedback.h"
#include "feedback_state.h"
#include "reader.h"

static FeedbackState state;
FeedbackDiagnostics feedback_diagnostics = {};

void reader_feedback_begin() {
    __HAL_RCC_GPIOC_CLK_ENABLE();
    digitalWrite(PC13, HIGH); // Blue Pill LED is active LOW.
    GPIO_InitTypeDef pin = {};
    pin.Pin = GPIO_PIN_13; pin.Mode = GPIO_MODE_OUTPUT_OD;
    pin.Pull = GPIO_NOPULL; pin.Speed = GPIO_SPEED_FREQ_LOW; // PC13: 2 MHz, sink only.
    HAL_GPIO_Init(GPIOC, &pin);
#if READER_BUZZER_ENABLED
    __HAL_RCC_GPIOB_CLK_ENABLE();
    digitalWrite(PB0, LOW);
    pin.Pin = GPIO_PIN_0; pin.Mode = GPIO_MODE_OUTPUT_PP;
    pin.Pull = GPIO_NOPULL; pin.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOB, &pin);
#else
    pinMode(PB0, INPUT); // Buzzer output disabled for this build.
#endif
    state.disconnected();
}
void reader_feedback_tick(bool authorized) {
    if (!authorized) state.disconnected();
    const uint32_t now = millis();
    state.advance(now);
    feedback_diagnostics.led_on = authorized && state.led_on(now);
    feedback_diagnostics.buzzer_on = READER_BUZZER_ENABLED && authorized && state.sounding(now);
    digitalWrite(PC13, feedback_diagnostics.led_on ? LOW : HIGH);
#if READER_BUZZER_ENABLED
    digitalWrite(PB0, feedback_diagnostics.buzzer_on ? HIGH : LOW);
#endif
}
void reader_feedback_card_read() {
    if (!reader_pd_online() || !reader_pd_secure()) return;
    ++feedback_diagnostics.read_beeps;
    state.card_read(millis());
    reader_feedback_tick(true);
}
osdp_status_t reader_feedback_command(uint8_t command, const uint8_t *data, size_t length, bool authorized) {
    osdp_status_t result = OSDP_ERR_INVALID_ARG;
    if (command == OSDP_CMD_LED) {
        result = state.apply_led(data, length, millis(), authorized);
        if (result == OSDP_OK) ++feedback_diagnostics.led_commands;
    } else if (command == OSDP_CMD_BUZ) {
        result = state.apply_buzzer(data, length, millis(), authorized, READER_BUZZER_ENABLED);
        if (result == OSDP_OK) ++feedback_diagnostics.buzzer_commands;
    }
    reader_feedback_tick(authorized);
    return result;
}
