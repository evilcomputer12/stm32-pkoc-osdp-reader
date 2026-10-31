#pragma once
#include <stdint.h>
#include <stddef.h>

struct ReaderMetrics {
    uint32_t rx_bytes, tx_bytes, tx_errors, service_gap_us, ticks, rx_errors, rx_edges;
};
extern ReaderMetrics reader_metrics;
bool reader_pd_begin();
void reader_pd_tick();
bool reader_pd_online();
bool reader_pd_secure();
int reader_rs485_tx_probe(bool capture_echo = false, bool *echo_matches = nullptr);
size_t reader_pd_size();
bool reader_enqueue(const uint8_t *credential, size_t bytes, uint16_t bits);
