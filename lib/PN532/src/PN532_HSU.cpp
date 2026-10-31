#include "PN532_HSU.h"
#ifdef NFC_INTERFACE_HSU
#include <Arduino.h>
#include <cstring>
extern "C" void reader_service_wait();
static constexpr uint8_t ack[] = {0, 0, 0xff, 0, 0xff, 0};

void PN532_HSU::begin() {
    uart.begin();
    // Wakeup will cancel any old operation before entering Normal mode.
    resync = true;
}
void PN532_HSU::wakeup() {
    // Cancel an old operation while the independently powered module is
    // already awake. Do this BEFORE the cold-start wake + SAM sequence.
    if (!uart.write(ack, sizeof(ack))) { fail(PN532_TIMEOUT); return; }
    for (unsigned i = 0; i < 5; ++i) { reader_service_wait(); delay(1); }
    const uint8_t wake[17] = {0x55, 0x55};
    if (!uart.write(wake, sizeof(wake))) { fail(PN532_TIMEOUT); return; }
    for (unsigned i = 0; i < 2; ++i) { reader_service_wait(); delay(1); }
    resync = false; // PN532::begin sends SAMConfiguration next, without abort.
    while (uart.read() >= 0) {}
}
int16_t PN532_HSU::fail(int16_t status) {
    last_io = status; resync = true; return status;
}
bool PN532_HSU::receive(uint8_t *out, size_t length, uint32_t start, uint16_t timeout) {
    size_t used = 0;
    while (used < length) {
        if (uart.errors() != initial_errors) return false;
        const int value = uart.read();
        if (value >= 0) { out[used++] = uint8_t(value); continue; }
        if (millis() - start >= timeout) return false;
        reader_service_wait();
        delay(1);
    }
    return uart.errors() == initial_errors;
}
int8_t PN532_HSU::writeCommand(const uint8_t *header, uint8_t hlen,
                              const uint8_t *body, uint8_t blen) {
    stage = 1; last_io = 0;
    const unsigned length = 1U + hlen + blen;
    if (!header || !hlen || (blen && !body) || length > 254)
        return fail(PN532_NO_SPACE);
    // A host ACK aborts a timed-out PN532 exchange (UM0701-02 section 6.3).
    if (resync) {
        if (!uart.write(ack, sizeof(ack))) return fail(PN532_TIMEOUT);
        for (unsigned i = 0; i < 5; ++i) { reader_service_wait(); delay(1); }
        resync = false;
    }
    while (uart.read() >= 0) {}
    initial_errors = uart.errors();
    command = header[0];
    uint8_t frame[261] = {0, 0, 0xff, uint8_t(length), uint8_t(-length), 0xd4};
    memcpy(frame + 6, header, hlen);
    if (blen) memcpy(frame + 6 + hlen, body, blen);
    uint8_t sum = 0xd4;
    for (unsigned i = 0; i < hlen; ++i) sum += header[i];
    for (unsigned i = 0; i < blen; ++i) sum += body[i];
    frame[5 + length] = uint8_t(-sum);
    frame[6 + length] = 0;
    if (!uart.write(frame, length + 7)) return fail(PN532_TIMEOUT);
    stage = 2;
    uint8_t response[sizeof(ack)] = {};
    // Normal-mode entry can include cold-start oscillator/SAM startup. The
    // old 10 ms RF-command ACK budget is too short for that initialization;
    // Adafruit allows up to one second for SAMConfiguration.
    const uint16_t ack_timeout = command == 0x14 ? 1000 : PN532_ACK_WAIT_TIME;
    if (!receive(response, sizeof(response), millis(), ack_timeout)) return fail(PN532_TIMEOUT);
    memcpy(last_frame, response, sizeof(response));
    if (memcmp(response, ack, sizeof(ack))) return fail(PN532_INVALID_ACK);
    return 0;
}
int16_t PN532_HSU::readResponse(uint8_t *out, uint8_t capacity, uint16_t timeout) {
    stage = 3; last_io = 0;
    if (!out) return fail(PN532_INVALID_FRAME);
    if (!timeout) timeout = 1000; // Never allow unbounded firmware waits.
    const uint32_t start = millis();
    uint8_t prefix[7] = {};
    if (!receive(prefix, sizeof(prefix), start, timeout)) return fail(PN532_TIMEOUT);
    memcpy(last_frame, prefix, sizeof(prefix));
    const unsigned length = prefix[3];
    if (prefix[0] || prefix[1] || prefix[2] != 0xff || length < 2 || length == 255 ||
        uint8_t(length + prefix[4]) || prefix[5] != 0xd5 || prefix[6] != uint8_t(command + 1))
        return fail(PN532_INVALID_FRAME);
    const unsigned payload = length - 2;
    if (payload > capacity) return fail(PN532_NO_SPACE);
    stage = 4;
    if (!receive(out, payload, start, timeout)) return fail(PN532_TIMEOUT);
    uint8_t suffix[2] = {};
    if (!receive(suffix, sizeof(suffix), start, timeout)) return fail(PN532_TIMEOUT);
    uint8_t sum = 0xd5 + uint8_t(command + 1);
    for (unsigned i = 0; i < payload; ++i) sum += out[i];
    if (uint8_t(sum + suffix[0]) || suffix[1]) return fail(PN532_INVALID_FRAME);
    stage = 5;
    return payload;
}
#endif
