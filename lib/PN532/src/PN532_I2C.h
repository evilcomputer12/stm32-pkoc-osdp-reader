/**
 * @modified picospuch
 */

#ifndef __PN532_I2C_H__
#define __PN532_I2C_H__

#include <Wire.h>
#include <stddef.h>
#include "PN532Interface.h"

class PN532_I2C : public PN532Interface
{
public:
    PN532_I2C(TwoWire &wire);
    int last_io = 0;
    unsigned stage = 0;
    uint8_t last_frame[12] = {};

    void begin();
    void wakeup();
    virtual int8_t writeCommand(const uint8_t *header, uint8_t hlen, const uint8_t *body = 0, uint8_t blen = 0);
    int16_t readResponse(uint8_t buf[], uint8_t len, uint16_t timeout);

private:
    TwoWire *_wire;
    uint8_t command;

    int8_t readAckFrame();
    int waitReady(size_t count, uint16_t timeout);
    bool sendFrame(const uint8_t *data, size_t count);
#ifdef ARDUINO_ARCH_STM32
    uint8_t received_frame[263];
    size_t receive_position = 0;
    size_t received_count = 0;
#endif
    int16_t getResponseLength(uint8_t buf[], uint8_t len, uint16_t timeout);

    inline uint8_t write(uint8_t data)
    {
#if ARDUINO >= 100
        return _wire->write(data);
#else
        return _wire->send(data);
#endif
    }

    inline uint8_t read()
    {
#ifdef ARDUINO_ARCH_STM32
        return received_frame[receive_position++];
#elif ARDUINO >= 100
        return _wire->read();
#else
        return _wire->receive();
#endif
    }
};

#endif
