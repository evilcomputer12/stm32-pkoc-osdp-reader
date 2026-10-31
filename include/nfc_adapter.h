#pragma once
#include <stdint.h>
struct NfcDiagnostics {
    uint32_t firmware, attempts, verified, failed, reported, latency_ms;
    uint32_t polls, no_card, poll_errors;
    int target_status, live_stage, live_io, presence_status;
    int error, transport_status;
    uint16_t card_sw;
    bool ready, held;
    int init_stage, init_io;
    uint8_t init_frame[12];
    uint32_t apdu_ack_bytes, apdu_uart_errors;
    unsigned apdu_stage;
    uint8_t apdu_ins;
    bool apdu_ack;
    uint32_t reselections;
};
extern NfcDiagnostics nfc_diagnostics;
void reader_nfc_begin();
void reader_nfc_tick();
#ifndef READER_SOFT_I2C
int reader_nfc_address_probe();
#endif
