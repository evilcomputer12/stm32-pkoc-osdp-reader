#include <Arduino.h>
#include <cstring>

// Toolchain/USB build baseline only. No NFC, crypto, I2C or OSDP operations.
// See README.md for the board-clock verification needed before hardware use.
void setup() {
    SerialUSB.begin(115200);
    // Do not wait for a terminal: startup must work with USB disconnected.
}

void loop() {
    // One command per iteration, with no readString()/timeout-based reads.
    if (SerialUSB.available() <= 0) {
        return;
    }

    const int command = SerialUSB.read();
    const char *reply;
    switch (command) {
    case 'p':
        reply = "pong\r\n";
        break;
    case 'i':
        reply = "STM32F103C6 USB build baseline; flash=32768 ram=10240\r\n";
        break;
    case 'h':
    case '?':
        reply = "p: ping; i: build info; h: help\r\n";
        break;
    default:
        return;
    }

    // The pinned USB write() can wait for queue space. Only submit a complete
    // short reply when it fits; discard replies if the terminal is unavailable.
    // dtr() avoids USBSerial::operator bool(), which includes a 10 ms delay.
    const size_t length = std::strlen(reply);
    if (SerialUSB.dtr() && SerialUSB.availableForWrite() >= static_cast<int>(length)) {
        SerialUSB.write(reinterpret_cast<const uint8_t *>(reply), length);
    }
}
