#pragma once
#include "feedback_config.h"
#include "osdp/osdp_types.h"
#include <stddef.h>
#include <stdint.h>
struct FeedbackDiagnostics {
    uint32_t led_commands, buzzer_commands, read_beeps;
    bool led_on, buzzer_on;
};
extern FeedbackDiagnostics feedback_diagnostics;
void reader_feedback_begin();
void reader_feedback_tick(bool authorized);
void reader_feedback_card_read();
osdp_status_t reader_feedback_command(uint8_t command, const uint8_t *, size_t, bool authorized);
