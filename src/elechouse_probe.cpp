#include <Arduino.h>
#ifdef ELECHOUSE_PROBE_HSU
#include <PN532_HSU.h>
#else
#include <Wire.h>
#include <PN532_I2C.h>
#endif
#include <PN532.h>
#include <cstdio>

// Deliberately isolated vendor comparison: no controller or credential events.
#ifdef ELECHOUSE_PROBE_HSU
static Uart pn_serial(PB11, PB10); // STM32duino 3 HardwareSerial implementation: RX, TX
static PN532_HSU pn_transport(pn_serial);
static constexpr const char *transport_name = "HSU";
#else
static TwoWire pn_wire(PB11, PB10);
static PN532_I2C pn_transport(pn_wire);
static constexpr const char *transport_name = "I2C";
#endif
static PN532 pn(pn_transport);
static uint32_t firmware, polls, found, absent, errors, next_poll;
static int last_status;
static bool initialized;
static char output[256];
static size_t pending, offset;

static void initialize() {
#ifdef ELECHOUSE_PROBE_HSU
    if (initialized) pn_serial.end();
#else
    if (initialized) pn_wire.end();
#endif
    initialized = true;
    pn.begin();
#ifdef ELECHOUSE_PROBE_HSU
    pinMode(PB11, INPUT); // FT receive pin: no internal pull-up to the MCU rail
#endif
    firmware = pn.getFirmwareVersion();
    if (firmware) {
        // These vendor methods return false for valid empty responses.
        // Preserve the vendor code and check communication independently.
        pn.SAMConfig();
        pn.setPassiveActivationRetries(0);
        firmware = pn.getFirmwareVersion();
    }
    next_poll = millis();
}

void setup() {
    digitalWrite(PA8, LOW); pinMode(PA8, OUTPUT); // RS-485 receive only
    SerialUSB.begin(115200);
    initialize();
}

void loop() {
    if (firmware && int32_t(millis() - next_poll) >= 0) {
        next_poll = millis() + 250;
        // Use the unmodified Elechouse transport. Parse the target reply here
        // so clean absence and malformed/timeout responses remain distinct.
        const uint8_t command[] = {0x4a, 1, 0};
        uint8_t reply[64];
        last_status = pn_transport.writeCommand(command, sizeof(command));
        if (last_status == 0)
            last_status = pn_transport.readResponse(reply, sizeof(reply), 80);
        ++polls;
        if (last_status == 1 && reply[0] == 0) ++absent;
        else if (last_status >= 6 && reply[0] == 1 && reply[5] > 0 &&
                 reply[5] <= 7 && last_status >= 6 + reply[5]) ++found;
        else ++errors;
    }
    if (!SerialUSB.dtr()) { pending = offset = 0; return; }
    if (offset < pending) {
        const size_t amount = min(SerialUSB.availableForWrite(), pending - offset);
        if (amount) offset += SerialUSB.write(reinterpret_cast<const uint8_t *>(output + offset), amount);
        return;
    }
    if (!SerialUSB.available()) return;
    const int command = SerialUSB.read();
    int count = 0;
    if (command == 'p') count = snprintf(output, sizeof(output), "pong\r\n");
    else if (command == 'u') {
        initialize();
        count = snprintf(output, sizeof(output), "Elechouse reinit fw=%08lx\r\n", firmware);
    } else if (command == 'd') {
        count = snprintf(output, sizeof(output), "Elechouse live firmware=%08lx\r\n", pn.getFirmwareVersion());
    } else if (command == 'c') {
#ifdef ELECHOUSE_PROBE_HSU
        count = snprintf(output, sizeof(output), "HSU uses USART3 at 115200; no I2C address probe\r\n");
#else
        pn_wire.beginTransmission(uint8_t(0x24));
        const unsigned status = pn_wire.endTransmission();
        count = snprintf(output, sizeof(output), "Wire address probe= %u (0=ACK), HAL_error=%lu\r\n",
                         status, HAL_I2C_GetError(pn_wire.getHandle()));
#endif
    } else if (command == 'e') {
        const uint8_t request = 0x02;
        uint8_t reply[64] = {};
        const int sent = pn_transport.writeCommand(&request, 1);
#ifndef ELECHOUSE_PROBE_HSU
        const uint32_t error_after_send = HAL_I2C_GetError(pn_wire.getHandle());
#endif
        const int received = sent == 0 ? pn_transport.readResponse(reply, sizeof(reply), 1000) : -99;
#ifdef ELECHOUSE_PROBE_HSU
        count = snprintf(output, sizeof(output),
            "Elechouse HSU GetFirmware sent=%d received=%d bytes=%02x%02x%02x%02x\r\n",
            sent, received, reply[0], reply[1], reply[2], reply[3]);
#else
        count = snprintf(output, sizeof(output),
            "Elechouse GetFirmware sent=%d received=%d send_HAL_error=%lu final_HAL_error=%lu bytes=%02x%02x%02x%02x\r\n",
            sent, received, error_after_send, HAL_I2C_GetError(pn_wire.getHandle()),
            reply[0], reply[1], reply[2], reply[3]);
#endif
    } else if (command == 'i' || command == 'n' || command == 'q') {
        count = snprintf(output, sizeof(output),
            "ELECHOUSE %s NFC-only fw=%08lx polls=%lu targets=%lu no_card=%lu errors=%lu status=%d B11=%u B10=%u uptime_ms=%lu\r\n",
            transport_name, firmware, polls, found, absent, errors, last_status, digitalRead(PB11), digitalRead(PB10), millis());
    } else if (command == 'h') {
        count = snprintf(output, sizeof(output), "p=ping n/q=counters d=firmware c=address probe e=vendor frame result u=reinit; OSDP disabled\r\n");
    }
    pending = count > 0 ? min(size_t(count), sizeof(output) - 1) : 0;
    offset = 0;
}
