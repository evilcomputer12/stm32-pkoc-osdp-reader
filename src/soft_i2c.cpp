#include <Arduino.h>
#include "soft_i2c.h"
#if defined(READER_SOFT_I2C) || defined(READER_PN532_SOFT_I2C)
namespace {
#ifdef READER_SPLIT_I2C
constexpr uint32_t scl = GPIO_PIN_10, sda = GPIO_PIN_11;
#else
constexpr uint32_t scl = GPIO_PIN_6, sda = GPIO_PIN_7;
#endif
uint32_t started, limit;
#ifdef READER_I2C_WEAK_PULLUPS
void configure(uint32_t pins, uint32_t mode) {
#ifdef READER_SPLIT_I2C
    uint32_t crh = GPIOB->CRH;
    if (pins & scl) crh = (crh & ~0x00000f00UL) | (mode << 8);
    if (pins & sda) crh = (crh & ~0x0000f000UL) | (mode << 12);
    GPIOB->CRH = crh;
#else
    uint32_t crl = GPIOB->CRL;
    if (pins & scl) crl = (crl & ~0x0f000000UL) | (mode << 24);
    if (pins & sda) crl = (crl & ~0xf0000000UL) | (mode << 28);
    GPIOB->CRL = crl;
#endif
}
// STM32F1 has no pull-up in output-open-drain mode. Emulate open drain
// with input-pull-up for release and open-drain output for LOW. Neither
// transition ever selects a push-pull output or actively drives HIGH.
void low(uint32_t pin) { GPIOB->BRR = pin; configure(pin, 5); }
void release(uint32_t pin) { GPIOB->BSRR = pin; configure(pin, 8); }
void half() { delayMicroseconds(20); } // <=25 kHz; bench diagnostic only
#else
void low(uint32_t pin) { GPIOB->BRR = pin; }
void release(uint32_t pin) { GPIOB->BSRR = pin; }
void half() { delayMicroseconds(5); }
#endif
bool level(uint32_t pin) { return (GPIOB->IDR & pin) != 0; }
bool expired() { return uint32_t(micros() - started) >= limit; }
bool clock_high() {
    release(scl);
    while (!level(scl)) if (expired()) return false;
    return true;
}
bool stop() {
    low(scl); low(sda); half();
    const bool high = clock_high(); half(); release(sda); half();
    return high && level(sda);
}
bool start(uint32_t timeout_ms) {
    started = micros(); limit = timeout_ms * 1000U;
    release(sda);
    if (!clock_high()) return false;
    half(); // Allow the released data line to rise before testing bus-idle.
    if (!level(sda)) return false;
    half(); low(sda); half(); low(scl);
    return true;
}
bool send(uint8_t value) {
    if (expired()) return false;
    for (unsigned bit = 0; bit < 8; ++bit) {
        if (value & 0x80) release(sda); else low(sda);
        half(); if (!clock_high()) return false;
        half(); low(scl); value <<= 1;
    }
    release(sda); half(); if (!clock_high()) return false;
    const bool ack = !level(sda); half(); low(scl);
    return ack;
}
bool receive(uint8_t &value, int ack) {
    if (expired()) return false;
    value = 0; release(sda);
    for (unsigned bit = 0; bit < 8; ++bit) {
        half(); if (!clock_high()) return false;
        value = uint8_t((value << 1) | level(sda));
        half(); low(scl);
    }
    // Mode 2 ACKs READY but NACKs NOT READY, before issuing STOP.
    if (ack == 1 || (ack == 2 && (value & 1))) low(sda); else release(sda);
    half(); if (!clock_high()) return false;
    half(); low(scl); release(sda);
    return true;
}
}
void reader_soft_i2c_begin() {
    __HAL_RCC_GPIOB_CLK_ENABLE();
#ifdef READER_SPLIT_I2C
    __HAL_RCC_I2C2_FORCE_RESET(); __HAL_RCC_I2C2_RELEASE_RESET();
    release(scl | sda);
    pinMode(PB10, OUTPUT_OPEN_DRAIN); pinMode(PB11, OUTPUT_OPEN_DRAIN);
#else
    __HAL_RCC_I2C1_FORCE_RESET(); __HAL_RCC_I2C1_RELEASE_RESET();
    release(scl | sda);
    pinMode(PB6, OUTPUT_OPEN_DRAIN); pinMode(PB7, OUTPUT_OPEN_DRAIN);
#endif
    release(scl | sda);
    half();
    started = micros(); limit = 2000;
    // A reset can leave a slave partway through a byte. Give it up to nine
    // clocks and STOP, using open drain only; never drive against a slave.
    for (unsigned i = 0; i < 9 && !level(sda) && level(scl); ++i) {
        low(scl); half(); if (!clock_high()) break; half();
    }
    if (level(scl)) (void)stop();
}
void reader_soft_i2c_wake_pulse() {
    release(scl | sda); half();
    low(sda); delayMicroseconds(80);
    release(sda); delayMicroseconds(1500);
}
bool reader_soft_i2c_write(uint8_t address, const uint8_t *data, size_t length, uint32_t timeout_ms) {
    if ((!data && length) || address > 0x7f || !timeout_ms || !start(timeout_ms)) return false;
    bool ok = send(uint8_t(address << 1));
    for (size_t i = 0; ok && i < length; ++i) ok = send(data[i]);
    const bool stopped = stop();
    return ok && stopped;
}
bool reader_soft_i2c_read(uint8_t address, uint8_t *data, size_t length, uint32_t timeout_ms) {
    if (!data || !length || address > 0x7f || !timeout_ms || !start(timeout_ms)) return false;
    bool ok = send(uint8_t((address << 1) | 1));
    for (size_t i = 0; ok && i < length; ++i) ok = receive(data[i], i + 1 < length);
    const bool stopped = stop();
    return ok && stopped;
}
bool reader_soft_i2c_pn532_frame(uint8_t address, uint8_t *data, size_t capacity, size_t *length, uint32_t timeout_ms) {
    if (!data || !length || capacity < 7 || address > 0x7f || !timeout_ms) return false;
    *length = 0;
    if (!start(timeout_ms)) return false;
    bool ok = send(uint8_t((address << 1) | 1)) && receive(data[0], 2);
    if (ok && !(data[0] & 1)) {
        *length = 1;
        return stop();
    }
    // Read LEN in the same I2C transaction, then exactly the remaining bytes.
    // This avoids truncating a frame and requesting retransmission by NACK.
    for (size_t i = 1; ok && i < 6; ++i) ok = receive(data[i], 1);
    size_t total = 0;
    if (ok && data[1] == 0 && data[2] == 0 && data[3] == 0xff) {
        if ((data[4] == 0 && data[5] == 0xff) || (data[4] == 0xff && data[5] == 0)) total = 7;
        else if (uint8_t(data[4] + data[5]) == 0) total = size_t(data[4]) + 8;
    }
    ok = ok && total >= 7 && total <= capacity;
    for (size_t i = 6; ok && i < total; ++i) ok = receive(data[i], i + 1 < total);
    const bool stopped = stop();
    if (ok && stopped) *length = total;
    return ok && stopped;
}
#endif
