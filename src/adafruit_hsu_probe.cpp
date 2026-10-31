#include <Arduino.h>
#include <Adafruit_PN532.h>
#include <cstdio>

// Isolated comparison: official Adafruit startup, STM32duino UART driver,
// no OSDP, ATECC, LED feedback or card writes. No reset input is wired.
static Uart pn_serial(PB11, PB10);
static Adafruit_PN532 pn(255, &pn_serial);
static uint32_t firmware, elapsed;
static bool normal, initialized;
static char output[256];
static size_t pending, offset;
static volatile uint32_t raw_edges;
static void count_rx_edge() { ++raw_edges; }

static int raw_probe() {
    while (pn_serial.read() >= 0) {}
    uint8_t received[48] = {};
    size_t used = 0, total = 0;
    raw_edges = 0;
    attachInterrupt(digitalPinToInterrupt(PB11), count_rx_edge, CHANGE);
    const uint8_t wake[] = {0x55, 0x00, 0x00};
    const uint8_t sam[] = {0,0,0xff,5,0xfb,0xd4,0x14,1,0x14,1,2,0};
    const uint8_t version[] = {0,0,0xff,2,0xfe,0xd4,2,0x2a,0};
    pn_serial.write(wake, sizeof(wake)); pn_serial.flush(); delay(2);
    for (unsigned phase=0; phase<2; ++phase) {
        if (!phase) pn_serial.write(sam, sizeof(sam));
        else pn_serial.write(version, sizeof(version));
        const uint32_t start = millis();
        while (millis()-start < 1000) {
            const int value = pn_serial.read();
            if (value >= 0) { ++total; if (used < sizeof(received)) received[used++]=uint8_t(value); }
        }
    }
    detachInterrupt(digitalPinToInterrupt(PB11));
    int count = snprintf(output, sizeof(output), "RAW HSU 115200 SAM+version RX_edges=%lu RX_bytes=%u bytes=", raw_edges, unsigned(total));
    for (size_t i=0;i<used;++i) count += snprintf(output+count, sizeof(output)-count, "%02x", received[i]);
    count += snprintf(output+count, sizeof(output)-count, "\r\n");
    return count;
}

static void initialize() {
    if (initialized) pn_serial.end();
    initialized = true;
    const uint32_t start = millis();
    pn.begin();
    pinMode(PB11, INPUT);
    normal = pn.SAMConfig();
    firmware = pn.getFirmwareVersion();
    elapsed = millis() - start;
}
void setup() {
    digitalWrite(PA8, LOW); pinMode(PA8, OUTPUT);
    SerialUSB.begin(115200);
    delay(100); // Let both independently powered boards finish starting.
    initialize();
}
void loop() {
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
    else if (command == 'e') count = raw_probe();
    else if (command == 'u' || command == 'n' || command == 'd') {
        if (command == 'u') initialize();
        count = snprintf(output, sizeof(output),
            "ADAFRUIT 1.3.4 HSU NFC-only SAM=%u fw=%08lx init_ms=%lu B11=%u B10=%u uptime_ms=%lu\r\n",
            normal, firmware, elapsed, digitalRead(PB11), digitalRead(PB10), millis());
    }
    pending = count > 0 ? min(size_t(count), sizeof(output) - 1) : 0;
    offset = 0;
}
