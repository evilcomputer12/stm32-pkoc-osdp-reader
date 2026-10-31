#include <Arduino.h>
#include <Wire.h>
#include <PN532_I2C.h>
#include <PN532.h>
#include <cstdio>
#include <cstring>
#include "atecc_adapter.h"

static PN532_I2C transport(Wire);
static PN532 nfc(transport);
static uint32_t firmware;
static char output[256];
static size_t pending, offset;
static bool sam;

void setup() {
    SerialUSB.begin(115200);
    reader_i2c_begin();
    nfc.begin();
    firmware = nfc.getFirmwareVersion();
    if (firmware) { sam = nfc.SAMConfig() && nfc.setPassiveActivationRetries(0); }
    reader_atecc_begin();
}

void loop() {
    reader_rng_maintain();
    if (!SerialUSB.dtr()) { pending = offset = 0; return; }
    if (offset < pending) {
        size_t n = min(static_cast<size_t>(SerialUSB.availableForWrite()), pending - offset);
        if (n) { offset += SerialUSB.write(reinterpret_cast<const uint8_t *>(output + offset), n); }
        return;
    }
    if (SerialUSB.available() <= 0) { return; }
    int count = 0;
    switch (SerialUSB.read()) {
    case 'p': count = snprintf(output, sizeof(output), "pong\r\n"); break;
    case 'i':
        count = snprintf(output, sizeof(output), "PN532 firmware=%08lx SAM=%u I2C1=PB6/PB7@100kHz uptime=%lu\r\n", firmware, sam, millis()); break;
    case 'a': {
        if (!atecc_diagnostics.ready) { reader_atecc_begin(); }
        const auto &d = atecc_diagnostics;
        count = snprintf(output, sizeof(output),
            "ATECC ready=%u address=0x%02x status=%d revision=%02x%02x%02x%02x\r\n"
            "config.address=0x%02x LockValue=0x%02x LockConfig=0x%02x\r\n",
            d.ready, d.address, d.status, d.revision[0], d.revision[1], d.revision[2], d.revision[3],
            d.config[16] >> 1, d.config[86], d.config[87]); break;
    }
    case 'r': {
        uint8_t random[32];
        const int status = reader_rng_block(random, sizeof(random));
        count = snprintf(output, sizeof(output), "ATECC RNG status=%d\r\n", status); break;
    }
    case 'n': {
        uint8_t uid[7], length = 0;
        const bool present = firmware && sam && nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &length, 80, true);
        count = snprintf(output, sizeof(output), "Card present=%u uid_length=%u (detection only; NOT authenticated)\r\n", present, length); break;
    }
    case 'h': case '?': count = snprintf(output, sizeof(output), "p=ping i=PN532 a=ATECC r=RNG n=detect card h=help\r\n"); break;
    default: return;
    }
    pending = count > 0 ? min(static_cast<size_t>(count), sizeof(output) - 1) : 0;
    offset = 0;
}
