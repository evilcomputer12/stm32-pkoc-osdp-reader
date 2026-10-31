#pragma once
#include <stddef.h>
#include <stdint.h>
struct PN532Uart {
    virtual void begin() = 0;
    virtual int read() = 0;
    virtual bool write(const uint8_t *, size_t) = 0;
    virtual uint32_t errors() const = 0;
};
