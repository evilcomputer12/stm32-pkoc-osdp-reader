// Seeed PN532_I2C transport, project-owned bounds/error-handling adaptation.
// Original interface/frame sequence: Seeed-Studio/PN532, pinned in dependencies.lock.json.
#include "PN532Interface.h"
#ifdef NFC_INTERFACE_I2C
#include "PN532_I2C.h"
#include <Arduino.h>
#include <cstring>
#if defined(READER_SOFT_I2C) || defined(READER_PN532_SOFT_I2C)
#define PN532_USE_SOFT_I2C
#endif
#ifdef PN532_USE_SOFT_I2C
#include "soft_i2c.h"
#endif
static constexpr uint8_t address = 0x24;
extern "C" void reader_service_wait() __attribute__((weak));
static void pause_one_ms() {
    if (reader_service_wait) { reader_service_wait(); }
    delay(1);
}
int PN532_I2C::waitReady(size_t count, uint16_t timeout) {
    const uint32_t start = millis();
    const uint32_t bounded = timeout ? timeout : 1000;
#ifdef PN532_USE_SOFT_I2C
    do {
        receive_position = 0;
        last_io = reader_soft_i2c_pn532_frame(address, received_frame, sizeof(received_frame), &received_count, reader_soft_i2c_pn_timeout_ms) ? HAL_OK : HAL_ERROR;
        if (last_io == HAL_OK && received_count) {
            memcpy(last_frame, received_frame, received_count < sizeof(last_frame) ? received_count : sizeof(last_frame));
            if (read() & 1) {
                if (count && received_count != count) return PN532_INVALID_FRAME;
                return 0;
            }
        }
        pause_one_ms();
    } while (millis() - start < bounded);
    return PN532_TIMEOUT;
#else
    const auto receive_bytes = [this](size_t amount) -> size_t {
#ifdef ARDUINO_ARCH_STM32
        receive_position = 0;
#ifdef PN532_USE_SOFT_I2C
        last_io = reader_soft_i2c_read(address, received_frame, amount, 40) ? HAL_OK : HAL_ERROR;
#else
        last_io = HAL_I2C_Master_Receive(_wire->getHandle(), address << 1,
            received_frame, amount, 40);
#endif
        if (last_io != HAL_OK) return 0;
        memcpy(last_frame, received_frame, amount < sizeof(last_frame) ? amount : sizeof(last_frame));
        return amount;
#else
        return _wire->requestFrom(address, amount);
#endif
    };
#ifdef ARDUINO_ARCH_STM32
    if (count > sizeof(received_frame)) return PN532_NO_SPACE;
#endif
    do {
        // NXP UM0701-02 section 6.2.4: after NOT READY the host must STOP.
        // Poll only the status byte; never clock a speculative full frame
        // while the PN532 is busy with a radio transaction.
        if (receive_bytes(1) == 1 && (read() & 1)) {
            if (receive_bytes(count) != count) return PN532_INVALID_FRAME;
            if (!(read() & 1)) return PN532_INVALID_FRAME;
            return 0;
        }
        pause_one_ms();
    } while (millis() - start < bounded);
    return PN532_TIMEOUT;
#endif
}
bool PN532_I2C::sendFrame(const uint8_t *data, size_t count) {
#ifdef ARDUINO_ARCH_STM32
#ifdef PN532_USE_SOFT_I2C
    last_io = reader_soft_i2c_write(address, data, count, reader_soft_i2c_pn_timeout_ms) ? HAL_OK : HAL_ERROR;
#else
    last_io = HAL_I2C_Master_Transmit(_wire->getHandle(), address << 1,
        const_cast<uint8_t *>(data), count, 40);
#endif
    return last_io == HAL_OK;
#else
    _wire->beginTransmission(address);
    const size_t written = _wire->write(data, count);
    return _wire->endTransmission() == 0 && written == count;
#endif
}
PN532_I2C::PN532_I2C(TwoWire &wire) : _wire(&wire), command(0) {}
void PN532_I2C::begin() {
#ifdef PN532_USE_SOFT_I2C
    reader_soft_i2c_begin();
#else
    _wire->begin(); _wire->setClock(100000);
#endif
}
void PN532_I2C::wakeup() { for (unsigned i = 0; i < 500; ++i) { pause_one_ms(); } }
int8_t PN532_I2C::writeCommand(const uint8_t *header, uint8_t hlen, const uint8_t *body, uint8_t blen) {
    stage = 1;
    if (!header || !hlen || (blen && !body) || unsigned(hlen) + blen > 253) { return PN532_INVALID_FRAME; }
    command = header[0];
    const uint8_t length = hlen + blen + 1;
    uint8_t frame[261] = {0, 0, 0xff, length, uint8_t(-length), PN532_HOSTTOPN532};
    size_t written = 6;
    uint8_t sum = PN532_HOSTTOPN532;
    for (uint8_t i = 0; i < hlen; ++i) { frame[written++] = header[i]; sum += header[i]; }
    for (uint8_t i = 0; i < blen; ++i) { frame[written++] = body[i]; sum += body[i]; }
    frame[written++] = uint8_t(-sum); frame[written++] = 0;
    if (!sendFrame(frame, written)) { return PN532_INVALID_FRAME; }
    return readAckFrame();
}
int16_t PN532_I2C::getResponseLength(uint8_t *, uint8_t, uint16_t timeout) {
    stage = 3;
    const int ready = waitReady(6, timeout);
    if (ready) { return ready; }
    if (read() != 0 || read() != 0 || read() != 0xff) { return PN532_INVALID_FRAME; }
    const uint8_t length = read();
    if (uint8_t(length + read()) != 0 || length < 2 || length == 0xff) { return PN532_INVALID_FRAME; }
    // NACK requests retransmission after inspecting LEN; the full frame is one read.
    const uint8_t nack[] = {0, 0, 0xff, 0xff, 0, 0};
    stage = 4;
    if (!sendFrame(nack, sizeof(nack))) { return PN532_INVALID_FRAME; }
    return length;
}
int16_t PN532_I2C::readResponse(uint8_t *buffer, uint8_t capacity, uint16_t timeout) {
    if (!buffer || !capacity) { return PN532_NO_SPACE; }
#ifdef PN532_USE_SOFT_I2C
    stage = 5;
    const int ready = waitReady(0, timeout);
    if (ready) return ready;
    if (received_count < 10) return PN532_INVALID_FRAME;
    const int16_t expected = received_frame[4];
    if (expected < 2 || received_count != size_t(expected) + 8) return PN532_INVALID_FRAME;
    if (expected - 2 > capacity) return PN532_NO_SPACE;
#else
    const int16_t expected = getResponseLength(buffer, capacity, timeout);
    if (expected < 0) { return expected; } // Check signed result before narrowing.
    if (expected < 2) { return PN532_INVALID_FRAME; }
    if (expected - 2 > capacity) { return PN532_NO_SPACE; }
    stage = 5;
    const int ready = waitReady(size_t(expected) + 8, timeout);
    if (ready) { return ready; }
#endif
    if (read() != 0 || read() != 0 || read() != 0xff) { return PN532_INVALID_FRAME; }
    const uint8_t length = read();
    if (length != expected || uint8_t(length + read()) != 0) { return PN532_INVALID_FRAME; }
    const uint8_t response_command = command + 1;
    if (read() != PN532_PN532TOHOST || read() != response_command) { return PN532_INVALID_FRAME; }
    uint8_t sum = PN532_PN532TOHOST + response_command;
    const uint8_t payload = length - 2;
    for (uint8_t i = 0; i < payload; ++i) { buffer[i] = read(); sum += buffer[i]; }
    if (uint8_t(sum + read()) != 0 || read() != 0) { return PN532_INVALID_FRAME; }
    stage = 6;
    return payload;
}
int8_t PN532_I2C::readAckFrame() {
    stage = 2;
    const uint8_t expected[] = {0, 0, 0xff, 0, 0xff, 0};
    const int ready = waitReady(sizeof(expected) + 1, PN532_ACK_WAIT_TIME);
    if (ready) { return ready; }
    for (uint8_t value : expected) { if (read() != value) { return PN532_INVALID_ACK; } }
    return 0;
}
#endif
