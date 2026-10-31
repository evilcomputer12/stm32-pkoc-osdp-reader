#pragma once
#include "PN532Interface.h"
#include <stddef.h>
#include "pn532_uart_io.h"

// Bounded UART abstraction: the firmware owns USART3 independently of OSDP's
// USART1. Host tests inject fragmented/error frames through the same interface.

// Normal HSU framing follows Elechouse/PN532 and NXP UM0701-02. Unlike the
// vendor implementation, lengths >127, partial reads and total deadlines are
// checked, and waits service OSDP. Extended frames are deliberately rejected.
class PN532_HSU : public PN532Interface {
public:
    explicit PN532_HSU(PN532Uart &uart) : uart(uart) {}
    void begin() override;
    void wakeup() override;
    int8_t writeCommand(const uint8_t *, uint8_t, const uint8_t * = nullptr, uint8_t = 0) override;
    int16_t readResponse(uint8_t *, uint8_t, uint16_t = 1000) override;
    int last_io = 0;
    unsigned stage = 0;
    uint8_t last_frame[12] = {};
private:
    PN532Uart &uart;
    uint8_t command = 0;
    uint32_t initial_errors = 0;
    bool resync = false;
    bool receive(uint8_t *, size_t, uint32_t, uint16_t);
    int16_t fail(int16_t status);
};
