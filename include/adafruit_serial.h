#pragma once
#include <Arduino.h>
#include "pn532_uart_io.h"
extern "C" void reader_service_wait();

// Adafruit's HardwareSerial interface over the independent USART3 RX ring.
// This keeps the Arduino core from taking ownership of OSDP's USART1 IRQ.
class ReaderAdafruitSerial final : public HardwareSerial {
    PN532Uart &uart;
    int cached = -1;
    uint32_t tx_errors = 0;
public:
    uint32_t consumed = 0;
    explicit ReaderAdafruitSerial(PN532Uart &physical) : uart(physical) {}
    void begin(unsigned long) override { uart.begin(); cached=-1; consumed=0; setTimeout(20); }
    void begin(unsigned long baud, uint16_t) override { begin(baud); }
    void end() override { cached=-1; }
    int available() override {
        reader_service_wait();
        if (cached < 0) cached=uart.read();
        return cached >= 0 ? 1 : 0;
    }
    int peek() override { available(); return cached; }
    int read() override {
        reader_service_wait();
        int value=cached; cached=-1;
        if (value < 0) value=uart.read();
        if (value >= 0) ++consumed;
        return value;
    }
    void flush() override {} // Physical write waits for transmission complete.
    size_t write(uint8_t value) override { return write(&value,1); }
    size_t write(const uint8_t *data, size_t length) override {
        if (uart.write(data,length)) return length;
        ++tx_errors; return 0;
    }
    operator bool() override { return true; }
    uint32_t errors() const { return uart.errors()+tx_errors; }
    void idle() { reader_service_wait(); delay(1); }
};
