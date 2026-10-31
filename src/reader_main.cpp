#include <Arduino.h>
#include <cstdio>
#include <cstring>
#include "reader.h"
#include "feedback.h"
#include "reader_i2c_pins.h"
#ifdef READER_NFC
#include "nfc_adapter.h"
#endif
#ifdef READER_SECURE
#include "atecc_adapter.h"
#include <Wire.h>
#include "../tests/crypto_fixtures.h"
#define READER_MODE "secure-required"
#else
#define READER_MODE "plain bench"
#endif

static char output[256];
static size_t pending, offset;

void setup() {
    SerialUSB.begin(115200);
#ifdef READER_SECURE
    reader_i2c_begin();
    reader_atecc_begin();
#endif
    reader_pd_begin();
#ifdef READER_NFC
    reader_nfc_begin();
#endif
}

void loop() {
#ifdef READER_SECURE
    reader_rng_maintain();
#endif
    reader_pd_tick();
#ifdef READER_NFC
    reader_nfc_tick();
#endif
    if (!SerialUSB.dtr()) { pending = offset = 0; return; }
    if (offset < pending) {
        const size_t available = SerialUSB.availableForWrite();
        const size_t amount = min(available, pending - offset);
        if (amount) { offset += SerialUSB.write(reinterpret_cast<const uint8_t *>(output + offset), amount); }
        return;
    }
    if (SerialUSB.available() <= 0) { return; }
    const int command = SerialUSB.read();
    int count = 0;
    // These bench operations take over the shared I2C bus and can take seconds
    // when a device is missing. Do not interrupt a running controller session.
    if ((command == 'u' || command == 'b' || command == 'c') &&
        (reader_pd_online() || reader_pd_secure())) {
        pending = snprintf(output, sizeof(output),
            "REFUSED: stop the OSDP controller and wait for offline before intrusive I2C diagnostics\r\n");
        offset = 0;
        return;
    }
    switch (command) {
    case 'f': {
        const auto &d = feedback_diagnostics;
        count = snprintf(output, sizeof(output),
            "Feedback LED_PC13=%u pin=%u buzzer_PB0=%u buzzer_pin=%u buzzer_enabled=%u LED_commands=%lu BUZ_commands=%lu read_beeps=%lu\r\n",
            d.led_on, digitalRead(PC13), d.buzzer_on, digitalRead(PB0), READER_BUZZER_ENABLED,
            d.led_commands, d.buzzer_commands, d.read_beeps);
        break;
    }
#ifdef READER_NFC
    case 'q': {
        const auto &d = nfc_diagnostics;
        count = snprintf(output, sizeof(output),
            "PN532 polls=%lu no_card=%lu poll_errors=%lu target=%d presence=%d stage=%d IO=%d reselect=%lu pin_data=%u pin_clock_tx=%u bus=" READER_PN532_BUS "\r\n",
            d.polls, d.no_card, d.poll_errors, d.target_status, d.presence_status, d.live_stage, d.live_io,
            d.reselections,
            digitalRead(READER_PN532_SDA), digitalRead(READER_PN532_SCL));
        break;
    }
    case 'n': {
        const auto &d = nfc_diagnostics;
        count = snprintf(output, sizeof(output),
            "PN532 fw=%08lx ready=%u held=%u attempts=%lu verified=%lu failed=%lu reported=%lu latency_ms=%lu error=%d transport=%d SW=%04x INS=%02x step=%u ACK=%u/%lu UART=%lu\r\n",
            d.firmware, d.ready, d.held, d.attempts, d.verified, d.failed, d.reported,
            d.latency_ms, d.error, d.transport_status, d.card_sw, d.apdu_ins,
            d.apdu_stage, d.apdu_ack, d.apdu_ack_bytes, d.apdu_uart_errors);
        break;
    }
    case 'd': {
        const auto &d = nfc_diagnostics;
        count = snprintf(output, sizeof(output), "PN532 init stage=%d IO=%d bytes=", d.init_stage, d.init_io);
        for (uint8_t value : d.init_frame) count += snprintf(output + count, sizeof(output) - count, "%02x", value);
        count += snprintf(output + count, sizeof(output) - count, "\r\n");
        break;
    }
#endif
    case 'p': count = snprintf(output, sizeof(output), "pong\r\n"); break;
    case 't': case 'e': {
        bool echo_matches = false;
        const int written = reader_rs485_tx_probe(command == 'e', &echo_matches);
        count = snprintf(output, sizeof(output), "RS485 TX probe bytes=%d echo_match=%u (-1=refused while online)\r\n", written, echo_matches);
        break;
    }
    case 'l':
        count = snprintf(output, sizeof(output), "RS485 pins PA8_DE_RE=%u PA9_TX=%u PA10_RX=%u\r\n",
                         digitalRead(PA8), digitalRead(PA9), digitalRead(PA10));
        break;
    case 'i':
        count = snprintf(output, sizeof(output),
            "PD " READER_MODE " address=0 baud=9600 online=%u secure=%u sizeof_pd=%u\r\n"
            "rx=%lu tx=%lu tx_errors=%lu rx_errors=%lu rx_edges=%lu max_gap_us=%lu uptime_ms=%lu\r\n",
            reader_pd_online(), reader_pd_secure(), static_cast<unsigned>(reader_pd_size()),
            reader_metrics.rx_bytes, reader_metrics.tx_bytes, reader_metrics.tx_errors,
            reader_metrics.rx_errors, reader_metrics.rx_edges, reader_metrics.service_gap_us, millis());
        break;
    case 's': {
        uint8_t synthetic[32];
        for (size_t i = 0; i < sizeof(synthetic); ++i) { synthetic[i] = static_cast<uint8_t>(i); }
        count = snprintf(output, sizeof(output), "SYNTHETIC 256-bit event %s\r\n",
                         reader_enqueue(synthetic, sizeof(synthetic), 256) ? "queued" : "rejected/offline");
        break;
    }
#ifdef READER_SECURE
    case 'v': {
        if (!atecc_diagnostics.ready) {
            count = snprintf(output, sizeof(output), "ATECC verify NOT RUN: device unavailable\r\n"); break;
        }
        const bool valid = reader_atecc_verify(test_pub_a, test_digest, test_signature_raw) == 0;
        const int valid_status = atecc_diagnostics.verify_status;
        uint8_t corrupted[64]; memcpy(corrupted, test_signature_raw, sizeof(corrupted)); corrupted[5] ^= 1;
        (void)reader_atecc_verify(test_pub_a, test_digest, corrupted);
        const bool rejected = atecc_diagnostics.verify_status == 0 && !atecc_diagnostics.verified;
        count = snprintf(output, sizeof(output), "ATECC external verify valid=%u status=%d corrupted_rejected=%u status=%d\r\n",
                         valid, valid_status, rejected, atecc_diagnostics.verify_status);
        break;
    }
    case 'c': {
#ifdef READER_SOFT_I2C
        count = reader_i2c_probe(output, sizeof(output));
#else
#ifdef READER_NFC
        const int pn = reader_nfc_address_probe();
#else
        const int pn = HAL_I2C_IsDeviceReady(Wire.getHandle(), 0x24 << 1, 1, 20);
#endif
        const unsigned at = HAL_I2C_IsDeviceReady(Wire.getHandle(), 0x60 << 1, 1, 20);
        count = snprintf(output, sizeof(output), "I2C probe PN532=%d (-1=HSU, no I2C address) ATECC=%u (0=ACK; sleeping ATECC may NACK) PN_bus=" READER_PN532_BUS "\r\n", pn, at);
#endif
        break;
    }
    case 'u':
        reader_i2c_begin();
        reader_atecc_begin();
#ifdef READER_NFC
        reader_nfc_begin();
#endif
        count = snprintf(output, sizeof(output), "Peripheral reinitialization finished; use a and n for results\r\n");
        break;
    case 'b': count = reader_i2c_probe(output, sizeof(output)); break;
    case 'a': {
        const auto &d = atecc_diagnostics;
        count = snprintf(output, sizeof(output),
            "ATECC ready=%u address=0x%02x status=%d revision=%02x%02x%02x%02x LockValue=%02x LockConfig=%02x SDA=%u SCL=%u\r\n",
            d.ready, d.address, d.status, d.revision[0], d.revision[1], d.revision[2], d.revision[3],
            d.config[86], d.config[87], digitalRead(PB7), digitalRead(PB6));
        break;
    }
    case 'r': {
        uint8_t sample[32];
        count = snprintf(output, sizeof(output), "ATECC RNG status=%d\r\n", reader_rng_block(sample, sizeof(sample)));
        memset(sample, 0, sizeof(sample));
        break;
    }
#endif
    case 'h': case '?':
        count = snprintf(output, sizeof(output), "p=ping i=OSDP a=ATECC r=RNG v=verify n=PN532 q=NFC polling f=feedback b=GPIO probe c=Wire probe u=reinit s=SYNTHETIC t=TX probe e=TX echo probe l=RS485 pins h=help\r\n"); break;
    default: return;
    }
    pending = count > 0 ? min(static_cast<size_t>(count), sizeof(output) - 1) : 0;
    offset = 0;
}
