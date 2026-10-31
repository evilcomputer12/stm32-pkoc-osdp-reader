#include <Arduino.h>
#include <Wire.h>
#include <cstring>
#include <cstdio>
#include "atecc_adapter.h"
#include "entropy_pool.h"
#ifdef READER_SOFT_I2C
#include "soft_i2c.h"
#endif
extern "C" {
#include "cryptoauthlib.h"
}

AteccDiagnostics atecc_diagnostics = {};
static ATCAIfaceCfg config = {};
static bool busy;
static bool bus_started;
static EntropyPool entropy;
static uint32_t next_rng_attempt;

void reader_i2c_begin() {
#ifdef READER_SOFT_I2C
    reader_soft_i2c_begin();
#else
    if (bus_started) Wire.end();
    Wire.setSDA(PB7);
    Wire.setSCL(PB6);
    Wire.begin();
    Wire.setClock(100000);
#endif
    bus_started = true;
}

static void wake_pulse() {
    // ATECC wake is a physical SDA-low pulse, not an acknowledged I2C
    // transfer. A general-call NACK left this board's HAL in a failed state.
    // Release both lines with open-drain GPIO and restore the shared Wire bus.
#ifdef READER_SOFT_I2C
    reader_soft_i2c_begin();
    reader_soft_i2c_wake_pulse();
#else
    if (bus_started) Wire.end();
    bus_started = false;
    digitalWrite(PB6, HIGH); digitalWrite(PB7, HIGH);
    pinMode(PB6, OUTPUT_OPEN_DRAIN); pinMode(PB7, OUTPUT_OPEN_DRAIN);
    delayMicroseconds(10);
    digitalWrite(PB7, LOW); delayMicroseconds(80); digitalWrite(PB7, HIGH);
    delayMicroseconds(1500);
    reader_i2c_begin();
#endif
}

extern "C" {
ATCA_STATUS hal_i2c_init(ATCAIface, ATCAIfaceCfg *) { return ATCA_SUCCESS; }
ATCA_STATUS hal_i2c_post_init(ATCAIface) { return ATCA_SUCCESS; }
ATCA_STATUS hal_i2c_release(void *) { return ATCA_SUCCESS; }
ATCA_STATUS hal_i2c_send(ATCAIface iface, uint8_t word, uint8_t *data, int length) {
    if (length < 0 || (length && !data)) { return ATCA_BAD_PARAM; }
    if (iface->mIfaceCFG->atcai2c.address == 0 && length == 0) {
        wake_pulse();
        return ATCA_SUCCESS;
    }
    if (length > 155) return ATCA_BAD_PARAM;
    uint8_t packet[156]; packet[0] = word;
    if (length) memcpy(packet + 1, data, length);
    // CryptoAuthLib splits a response into a one-byte count read and a
    // remaining-data read. Use STM32duino's public HAL handle for these
    // bounded transfers; its interrupt/sequence path stalled on this board.
#ifdef READER_SOFT_I2C
    return reader_soft_i2c_write(iface->mIfaceCFG->atcai2c.address >> 1, packet, length + 1, reader_soft_i2c_atca_timeout_ms)
        ? ATCA_SUCCESS : ATCA_COMM_FAIL;
#else
    const auto status = HAL_I2C_Master_Transmit(Wire.getHandle(), iface->mIfaceCFG->atcai2c.address,
                                               packet, length + 1, 20);
    return status == HAL_OK ? ATCA_SUCCESS : ATCA_COMM_FAIL;
#endif
}
ATCA_STATUS hal_i2c_receive(ATCAIface, uint8_t address, uint8_t *data, uint16_t *length) {
    if (!data || !length) { return ATCA_BAD_PARAM; }
#ifdef READER_SOFT_I2C
    const bool ok = reader_soft_i2c_read(address >> 1, data, *length, reader_soft_i2c_atca_timeout_ms);
    if (!ok) *length = 0;
    return ok ? ATCA_SUCCESS : ATCA_COMM_FAIL;
#else
    const auto status = HAL_I2C_Master_Receive(Wire.getHandle(), address, data, *length, 20);
    if (status != HAL_OK) *length = 0;
    return status == HAL_OK ? ATCA_SUCCESS : ATCA_COMM_FAIL;
#endif
}
ATCA_STATUS hal_i2c_control(ATCAIface, uint8_t option, void *value, size_t length) {
    if (option == ATCA_HAL_CHANGE_BAUD && value && length == sizeof(uint32_t)) {
#ifndef READER_SOFT_I2C
        Wire.setClock(*static_cast<uint32_t *>(value)); return ATCA_SUCCESS;
#else
        return *static_cast<uint32_t *>(value) == 100000 ? ATCA_SUCCESS : ATCA_UNIMPLEMENTED;
#endif
    }
    return ATCA_UNIMPLEMENTED; // Modern CryptoAuthLib sends wake/idle/sleep itself.
}
void atca_delay_us(uint32_t us) { delayMicroseconds(us); }
void atca_delay_ms(uint32_t ms) { delay(ms); }
void hal_delay_us(uint32_t us) { delayMicroseconds(us); }
void hal_delay_ms(uint32_t ms) { delay(ms); }
}

bool reader_atecc_begin() {
    if (busy) { return false; }
    busy = true;
    entropy.clear();
    next_rng_attempt = 0;
    atecc_diagnostics.ready = false;
    // Discover the locked device's actual address from its wake response.
    // A sleeping device is expected to NACK ordinary address scans.
    uint8_t found = 0;
    for (unsigned attempt = 0; attempt < 2 && !found; ++attempt) {
        for (unsigned candidate = 7; candidate < 0x78; ++candidate) {
            // Try the factory address first, but discover the configured
            // address if different. Wake immediately before EACH probe: a
            // complete scan can exceed the device's awake watchdog period.
            const uint8_t address = candidate == 7 ? 0x60 : candidate;
            if (address == 0x24) { continue; } // Never consume PN532 responses.
            wake_pulse();
            delay(attempt ? 30 : 3);
            uint8_t response[4];
#ifdef READER_SOFT_I2C
            const auto read_status = reader_soft_i2c_read(address, response, sizeof(response), reader_soft_i2c_atca_timeout_ms) ? HAL_OK : HAL_ERROR;
#else
            const auto read_status = HAL_I2C_Master_Receive(Wire.getHandle(), address << 1,
                                                           response, sizeof(response), 20);
#endif
            if (read_status == HAL_OK && hal_check_wake(response, sizeof(response)) == ATCA_SUCCESS) {
                found = address; break;
            }
        }
    }
    atecc_diagnostics.address = found;
    if (!found) { atecc_diagnostics.status = ATCA_NO_DEVICES; reader_i2c_begin(); busy = false; return false; }
    config.iface_type = ATCA_I2C_IFACE;
    config.devtype = ATECC608;
    config.atcai2c.address = found << 1;
    config.atcai2c.bus = 0;
    config.atcai2c.baud = 100000;
    config.wake_delay = 1500;
    config.rx_retries = 20;
    ATCA_STATUS result = atcab_init(&config);
    if (result == ATCA_SUCCESS) { result = atcab_info(atecc_diagnostics.revision); }
    if (result == ATCA_SUCCESS) { result = atcab_read_serial_number(atecc_diagnostics.serial); }
    if (result == ATCA_SUCCESS) { result = atcab_read_config_zone(atecc_diagnostics.config); }
    atecc_diagnostics.status = result;
    atecc_diagnostics.ready = result == ATCA_SUCCESS;
    (void)atcab_sleep();
    busy = false;
    if (atecc_diagnostics.ready) { reader_rng_maintain(); reader_rng_maintain(); }
    return atecc_diagnostics.ready;
}

void reader_rng_maintain() {
    if (!atecc_diagnostics.ready || busy || entropy.available() > 32 ||
        int32_t(millis() - next_rng_attempt) < 0) return;
    busy = true;
    uint8_t block[32];
    const auto status = atcab_random(block);
    if (status == ATCA_SUCCESS) (void)entropy.append(block, sizeof(block));
    else next_rng_attempt = millis() + 1000;
    for (volatile uint8_t *p = block; p < block + sizeof(block); ++p) { *p = 0; }
    (void)atcab_sleep(); busy = false;
}

extern "C" int reader_rng_block(unsigned char *out, unsigned int length) {
    // This callback can run inside a PN532 wait via OSDP servicing. It must
    // never wake/reset/reconfigure the shared I2C bus. Consume each hardware
    // RNG byte once; insufficient entropy is a hard failure, with no fallback.
    if (!atecc_diagnostics.ready) return -1;
    return entropy.take(out, length);
}

int reader_atecc_verify(const uint8_t pub[65], const uint8_t hash[32], const uint8_t sig[64]) {
    atecc_diagnostics.verify_status = ATCA_NOT_INITIALIZED;
    atecc_diagnostics.verified = false;
    if (!pub || !hash || !sig || pub[0] != 4 || !atecc_diagnostics.ready || busy) { return -1; }
    busy = true;
    bool verified = false;
    const ATCA_STATUS result = atcab_verify_extern(hash, sig, pub + 1, &verified);
    atecc_diagnostics.verify_status = result;
    atecc_diagnostics.verified = verified;
    (void)atcab_sleep(); busy = false;
    return result == ATCA_SUCCESS && verified ? 0 : -1;
}

size_t reader_i2c_probe(char *out, size_t capacity) {
    if (!out || capacity < 32 || busy) return 0;
    busy = true;
#ifndef READER_SOFT_I2C
    Wire.end();
#endif
    bus_started = false;
    digitalWrite(PB6, HIGH); digitalWrite(PB7, HIGH);
    pinMode(PB6, OUTPUT_OPEN_DRAIN); pinMode(PB7, OUTPUT_OPEN_DRAIN);
    delayMicroseconds(20);
    const bool idle_scl = digitalRead(PB6), idle_sda = digitalRead(PB7);
    size_t count = snprintf(out, capacity, "GPIO I2C SCL=%u SDA=%u ACK:", idle_scl, idle_sda);
    // ATECC wake pulse; open drain only. No config/data bytes are written.
    digitalWrite(PB7, LOW); delayMicroseconds(80);
    digitalWrite(PB7, HIGH); delay(30);
    for (uint8_t address = 8; idle_scl && idle_sda && address < 0x78; ++address) {
        digitalWrite(PB7, LOW); delayMicroseconds(20); // START
        uint8_t value = address << 1;
        bool clock_ok = true;
        for (unsigned bit = 0; bit < 8; ++bit) {
            digitalWrite(PB6, LOW);
            digitalWrite(PB7, value & 0x80 ? HIGH : LOW);
            delayMicroseconds(20); digitalWrite(PB6, HIGH);
            const uint32_t start = micros();
            while (!digitalRead(PB6) && micros() - start < 1000) {}
            clock_ok &= digitalRead(PB6);
            delayMicroseconds(20); value <<= 1;
        }
        digitalWrite(PB6, LOW); digitalWrite(PB7, HIGH);
        delayMicroseconds(20); digitalWrite(PB6, HIGH); delayMicroseconds(20);
        const bool ack = clock_ok && digitalRead(PB6) && !digitalRead(PB7);
        digitalWrite(PB6, LOW); digitalWrite(PB7, LOW); delayMicroseconds(20);
        digitalWrite(PB6, HIGH); delayMicroseconds(20);
        digitalWrite(PB7, HIGH); delayMicroseconds(20); // STOP
        if (ack && count + 5 < capacity) count += snprintf(out + count, capacity - count, " %02x", address);
    }
    if (count + 3 < capacity) count += snprintf(out + count, capacity - count, "\r\n");
    reader_i2c_begin();
    busy = false;
    return count;
}
