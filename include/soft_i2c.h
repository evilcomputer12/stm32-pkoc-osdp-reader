#pragma once
#include <stddef.h>
#include <stdint.h>
void reader_soft_i2c_begin();
void reader_soft_i2c_wake_pulse();
#ifdef READER_I2C_WEAK_PULLUPS
constexpr uint32_t reader_soft_i2c_pn_timeout_ms = 160;
constexpr uint32_t reader_soft_i2c_atca_timeout_ms = 100;
#else
constexpr uint32_t reader_soft_i2c_pn_timeout_ms = 40;
constexpr uint32_t reader_soft_i2c_atca_timeout_ms = 20;
#endif
bool reader_soft_i2c_read(uint8_t address, uint8_t *data, size_t length, uint32_t timeout_ms);
bool reader_soft_i2c_write(uint8_t address, const uint8_t *data, size_t length, uint32_t timeout_ms);
bool reader_soft_i2c_pn532_frame(uint8_t address, uint8_t *data, size_t capacity, size_t *length, uint32_t timeout_ms);
