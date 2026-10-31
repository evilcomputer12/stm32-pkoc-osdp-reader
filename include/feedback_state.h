#pragma once
#include <stddef.h>
#include <stdint.h>
#include "osdp/osdp_led_state.h"
#include "osdp/osdp_buz_state.h"

// Application-owned state: the upstream PD mirrors even rejected output
// commands, so those mirrors must not drive the physical pins directly.
struct FeedbackState {
    osdp_led_t led = {};
    osdp_buz_t buzzer = {};
    uint32_t read_started = 0;
    bool read_beep = false;

    void disconnected() { led = {}; buzzer = {}; read_beep = false; }
    void card_read(uint32_t now) { read_started = now; read_beep = true; }
    void advance(uint32_t now) {
        if (read_beep && uint32_t(now - read_started) >= 100) read_beep = false;
        if (led.temp_active && uint32_t(now - led.temp_started_ms) >= led.temp_duration_ms)
            led.temp_active = false;
        const uint32_t cycle = (uint32_t(buzzer.on_time_100ms) + buzzer.off_time_100ms) * 100;
        if (buzzer.active && buzzer.count && uint32_t(now - buzzer.started_ms) >= cycle * buzzer.count)
            buzzer.active = false;
    }
    bool led_on(uint32_t now) const { return osdp_led_color(&led, now) == OSDP_LED_GREEN; }
    bool sounding(uint32_t now) const {
        return (read_beep && uint32_t(now - read_started) < 100) || osdp_buz_sounding(&buzzer, now);
    }
    static bool supported_color(uint8_t color) {
        return color == OSDP_LED_BLACK || color == OSDP_LED_GREEN;
    }
    osdp_status_t apply_led(const uint8_t *data, size_t length, uint32_t now, bool authorized) {
        if (!authorized) return OSDP_ERR_INVALID_ARG;
        if (!data || !length || length % OSDP_LED_RECORD_BYTES) return OSDP_ERR_BAD_LENGTH;
        auto next = led; // Validate the entire batch before changing outputs.
        for (size_t i = 0; i < length; i += OSDP_LED_RECORD_BYTES) {
            osdp_led_record_t record = {}; size_t count = 0;
            if (osdp_led_decode(data + i, OSDP_LED_RECORD_BYTES, &record, 1, &count) != OSDP_OK)
                return OSDP_ERR_BAD_LENGTH;
            if (record.reader_no || record.led_no || record.temp_control_code > 2 || record.perm_control_code > 1)
                return OSDP_ERR_INVALID_ARG;
            if (record.temp_control_code == OSDP_LED_TEMP_SET &&
                (!supported_color(record.temp_on_color) || !supported_color(record.temp_off_color)))
                return OSDP_ERR_INVALID_ARG;
            if (record.perm_control_code == OSDP_LED_PERM_SET &&
                (!supported_color(record.perm_on_color) || !supported_color(record.perm_off_color)))
                return OSDP_ERR_INVALID_ARG;
            osdp_led_apply(&next, &record, now);
        }
        led = next;
        return OSDP_OK;
    }
    osdp_status_t apply_buzzer(const uint8_t *data, size_t length, uint32_t now,
                               bool authorized, bool buzzer_enabled) {
        if (!authorized || !buzzer_enabled) return OSDP_ERR_INVALID_ARG;
        osdp_buz_cmd_t command = {};
        if (osdp_buz_decode(data, length, &command) != OSDP_OK) return OSDP_ERR_BAD_LENGTH;
        if (command.reader_no || (command.tone_code != OSDP_BUZ_TONE_OFF && command.tone_code != OSDP_BUZ_TONE_DEFAULT))
            return OSDP_ERR_INVALID_ARG;
        osdp_buz_apply(&buzzer, &command, now);
        read_beep = false; // Explicit controller sound/off command takes priority.
        return OSDP_OK;
    }
};
