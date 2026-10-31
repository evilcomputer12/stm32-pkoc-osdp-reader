#pragma once
#include <stdint.h>
#include <stddef.h>
struct AteccDiagnostics {
    uint8_t address, revision[4], serial[9], config[128];
    int status;
    bool ready;
    int verify_status;
    bool verified;
};
extern AteccDiagnostics atecc_diagnostics;
void reader_i2c_begin();
bool reader_atecc_begin();
void reader_rng_maintain(); // Only between complete PN532 transactions.
size_t reader_i2c_probe(char *out, size_t capacity);
int reader_atecc_verify(const uint8_t pub[65], const uint8_t hash[32], const uint8_t sig[64]);
extern "C" int reader_rng_block(unsigned char *out, unsigned int length);
