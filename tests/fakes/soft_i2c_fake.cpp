#include "Wire.h"
#include "soft_i2c.h"
TwoWire *fake_soft_bus;
void reader_soft_i2c_begin() {}
bool reader_soft_i2c_pn532_frame(uint8_t address, uint8_t *data, size_t capacity, size_t *length, uint32_t) {
    const auto received = fake_soft_bus->requestFrom(address, capacity);
    for (size_t i=0; i<received; ++i) data[i]=fake_soft_bus->read();
    *length = received;
    return received != 0;
}
bool reader_soft_i2c_read(uint8_t address, uint8_t *data, size_t count, uint32_t) {
    const auto received = fake_soft_bus->requestFrom(address, count);
    for (size_t i=0; i<received; ++i) data[i]=fake_soft_bus->read();
    return received==count;
}
bool reader_soft_i2c_write(uint8_t address, const uint8_t *data, size_t count, uint32_t) {
    fake_soft_bus->beginTransmission(address);
    const auto written=fake_soft_bus->write(data,count);
    return fake_soft_bus->endTransmission()==0 && written==count;
}
