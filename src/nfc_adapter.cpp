#include <Arduino.h>
#include <Wire.h>
#ifdef READER_ADAFRUIT
#include <Adafruit_PN532.h>
#include "adafruit_serial.h"
#include "adafruit_reader.h"
#include "pn532_uart.h"
#elif defined(READER_PN532_HSU)
#include <PN532_HSU.h>
#include "pn532_uart.h"
#else
#include <PN532_I2C.h>
#endif
#ifndef READER_ADAFRUIT
#include <PN532.h>
#endif
#include <cstring>
#include "asymcred/asymcred_pkoc.h"
#include "wolfssl/wolfcrypt/sha256.h"
#include "wolfssl/wolfcrypt/hash.h"
#include "apdu_bridge.h"
#include "atecc_adapter.h"
#include "nfc_adapter.h"
#include "reader.h"
#include "feedback.h"
#include "presentation_guard.h"
#include "reader_i2c_pins.h"
#ifdef READER_PN532_SOFT_I2C
#include "soft_i2c.h"
#endif

#ifdef READER_ADAFRUIT
static ReaderAdafruitSerial pn_serial(reader_pn532_uart());
static Adafruit_PN532 adafruit(255, &pn_serial);
static AdafruitReader<Adafruit_PN532, ReaderAdafruitSerial> nfc(adafruit,pn_serial);
static auto &transport=nfc;
#elif defined(READER_PN532_HSU)
static PN532_HSU transport(reader_pn532_uart());
#else
#if defined(READER_SPLIT_I2C) && !defined(READER_PN532_SOFT_I2C)
static TwoWire pn532_wire(READER_PN532_SDA, READER_PN532_SCL);
static bool pn532_wire_started;
#else
static TwoWire &pn532_wire = Wire;
#endif
static PN532_I2C transport(pn532_wire);
#endif
#ifndef READER_ADAFRUIT
static PN532 nfc(transport);
#endif
static asymcred_pkoc_t pkoc;
static bool active;
static uint8_t consecutive_errors;
static PresentationGuard presentation;
static uint32_t next_poll, started;
NfcDiagnostics nfc_diagnostics = {};

#ifndef READER_SOFT_I2C
int reader_nfc_address_probe() {
#ifdef READER_PN532_HSU
    return -1; // No I2C address on HSU.
#elif defined(READER_PN532_SOFT_I2C)
    return reader_soft_i2c_write(0x24, nullptr, 0, reader_soft_i2c_pn_timeout_ms) ? 0 : 1;
#else
    return HAL_I2C_IsDeviceReady(pn532_wire.getHandle(), 0x24 << 1, 1, 20);
#endif
}
#endif

// I2C calls this between transactions; HSU can call it while its RX IRQ runs.
extern "C" void reader_service_wait() { reader_pd_tick(); }

static asymcred_status_t hash(void *, const uint8_t *data, size_t length, uint8_t out[32]) {
    if (length > UINT32_MAX) return ASYMCRED_ERR_INVALID_ARG;
    return wc_Sha256Hash(data, static_cast<word32>(length), out) == 0 ?
        ASYMCRED_OK : ASYMCRED_ERR_INVALID_STATE;
}
static asymcred_status_t verify(void *, const uint8_t pub[65], const uint8_t digest[32], const uint8_t sig[64]) {
    return reader_atecc_verify(pub, digest, sig) == 0 ? ASYMCRED_OK : ASYMCRED_ERR_BAD_SIGNATURE;
}
static asymcred_status_t random_bytes(void *, uint8_t *out, size_t length) {
    if (length > UINT32_MAX) return ASYMCRED_ERR_INVALID_ARG;
    return reader_rng_block(out, length) == 0 ? ASYMCRED_OK : ASYMCRED_ERR_INVALID_STATE;
}
static const asymcred_crypto_t crypto = {hash, verify, random_bytes, nullptr};
static const uint16_t versions[] = {ASYMCRED_PKOC_VERSION_1_1, ASYMCRED_PKOC_VERSION_1_0};

static bool restart_discovery() {
    // An MCU-only reset can leave PN532/card selection state powered. Explicit
    // field-off time resets even a held card that no longer answers REQA.
    nfc.inRelease(0);
    if (!nfc.setRFField(0, 0)) return false;
    for (unsigned i = 0; i < 20; ++i) { reader_service_wait(); delay(1); }
    return nfc.setRFField(0, 1);
}

void reader_nfc_begin() {
    active = false;
    consecutive_errors = 0;
    presentation.reset();
    nfc_diagnostics.ready = false;
    nfc_diagnostics.held = false;
    nfc_diagnostics.presence_status = 0;
#if defined(READER_SPLIT_I2C) && !defined(READER_PN532_SOFT_I2C)
    // Reset only the PN532 controller; ATECC wake/reset never touches I2C2.
    if (pn532_wire_started) pn532_wire.end();
    pn532_wire_started = true;
#endif
    nfc.begin();
    nfc_diagnostics.firmware = nfc.getFirmwareVersion();
#ifdef READER_ADAFRUIT
    // One bounded retry lets the adapter abort a PN532 operation that
    // survived an MCU-only reset. It does not loop forever on missing hardware.
    if (!nfc_diagnostics.firmware) nfc_diagnostics.firmware = nfc.getFirmwareVersion();
#endif
    nfc_diagnostics.init_stage = transport.stage;
    nfc_diagnostics.init_io = transport.last_io;
    memcpy(nfc_diagnostics.init_frame, transport.last_frame, sizeof(transport.last_frame));
    if (nfc_diagnostics.firmware) {
        nfc_diagnostics.ready = nfc.SAMConfig() && nfc.setPassiveActivationRetries(0);
        if (nfc_diagnostics.ready) nfc_diagnostics.ready = restart_discovery();
    }
    next_poll = millis();
}

static void finish(asymcred_status_t status) {
    nfc_diagnostics.error = status;
    nfc_diagnostics.card_sw = asymcred_pkoc_card_sw(&pkoc);
    nfc_diagnostics.transport_status = nfc.lastExchangeStatus;
    nfc_diagnostics.latency_ms = millis() - started;
    if (status != ASYMCRED_OK) ++nfc_diagnostics.failed;
    active = false;
    // No retry until clean absence is detected: failures cannot flood events.
    // Keep the ISO14443-4 card selected. Repeated discovery or release would
    // make a held card appear absent. Diagnose 0x06 checks the active target.
    next_poll = millis() + 250;
}

void reader_nfc_tick() {
    if (!nfc_diagnostics.ready) return;
    if (active) {
        if (!reader_pd_online() || !reader_pd_secure()) {
            finish(ASYMCRED_ERR_INVALID_STATE); return;
        }
        uint8_t command[96], response[256];
        size_t command_length = 0, response_length = 0;
        asymcred_status_t status = asymcred_pkoc_next_apdu(&pkoc, command, sizeof(command), &command_length);
        if (status == ASYMCRED_OK) {
            status = reader_apdu_exchange(nfc, command, command_length, response, sizeof(response), &response_length);
#ifdef READER_ADAFRUIT
            nfc_diagnostics.apdu_ins = command_length>1 ? command[1] : 0;
            nfc_diagnostics.apdu_stage = transport.stage;
            nfc_diagnostics.apdu_ack = transport.acknowledged;
            nfc_diagnostics.apdu_ack_bytes = transport.ack_bytes;
            nfc_diagnostics.apdu_uart_errors = transport.uart_errors;
#endif
        }
        if (status == ASYMCRED_OK) status = asymcred_pkoc_feed(&pkoc, response, response_length);
        if (status != ASYMCRED_OK) { finish(status); return; }
        if (!asymcred_pkoc_pending(&pkoc)) {
            asymcred_pkoc_result_t result;
            status = asymcred_pkoc_result(&pkoc, &result);
            if (status == ASYMCRED_OK && result.signature_verified) {
                ++nfc_diagnostics.verified;
                uint8_t credential[32]; size_t count = 0, bits = 0;
                status = asymcred_pkoc_credential(result.public_key, ASYMCRED_PKOC_CRED_256BIT,
                                                  credential, sizeof(credential), &count, &bits);
                if (status == ASYMCRED_OK && !presentation.was_reported(credential,count) &&
                    reader_enqueue(credential, count, bits)) {
                    presentation.note_reported(credential,count);
                    ++nfc_diagnostics.reported;
                    reader_feedback_card_read();
                }
            } else if (status == ASYMCRED_OK) status = ASYMCRED_ERR_BAD_SIGNATURE;
            finish(status);
        }
        return;
    }
    if (int32_t(millis() - next_poll) < 0) return;
    next_poll = millis() + 250;
    if (presentation.held && !presentation.refind) {
        const int result = nfc.checkTargetPresence();
        ++nfc_diagnostics.polls;
        nfc_diagnostics.target_status = result == 0 ? 1 : (result == 1 ? 0 : -1);
        nfc_diagnostics.live_stage = transport.stage;
        nfc_diagnostics.live_io = transport.last_io;
        nfc_diagnostics.presence_status = result;
        if (result == 0) { presentation.selected_present(); consecutive_errors = 0; }
        else if (result == 1) { // Complete PN532 response: RF target timeout.
            consecutive_errors = 0;
            ++nfc_diagnostics.no_card;
            if (presentation.selected_timeout()) {
                nfc_diagnostics.ready = restart_discovery();
            }
        } else {
            presentation.error();
            ++nfc_diagnostics.poll_errors;
            if (result < 0 && ++consecutive_errors >= 3) nfc_diagnostics.ready = false;
        }
        return;
    }
    // A presentation starts only when its verified event can be delivered.
    // Keep polling idle while offline so a card held during SC setup is usable.
    if (!reader_pd_online() || !reader_pd_secure()) return;
    uint8_t uid[7], length = 0;
    const bool found = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, &length, 80, true);
    ++nfc_diagnostics.polls;
    nfc_diagnostics.target_status = nfc.lastTargetStatus;
    nfc_diagnostics.live_stage = transport.stage;
    nfc_diagnostics.live_io = transport.last_io;
    if (!found) {
        if (nfc.lastTargetStatus == 0) { ++nfc_diagnostics.no_card; consecutive_errors = 0; }
        else {
            ++nfc_diagnostics.poll_errors;
            if (++consecutive_errors >= 3) nfc_diagnostics.ready = false;
        }
        // Confirm physical absence using fresh discovery, not loss of an
        // ISO14443-4 selected session. Errors break the clean-absence run.
        if (nfc.lastTargetStatus == 0) presentation.discovery_absent(millis());
        else presentation.error();
        nfc_diagnostics.held = presentation.held;
        return;
    }
    consecutive_errors = 0;
    const bool reselected = presentation.refind;
    const bool fresh = presentation.discovery_found(uid,length);
    nfc_diagnostics.held = presentation.held;
    nfc_diagnostics.reselections = presentation.reselections;
    // Reauthenticate a reselected target even if its UID is unchanged. Only
    // the verified credential suppresses duplicate reports; randomized or
    // reused UIDs must not substitute for card identity.
    if (!fresh && !reselected) return;
    ++nfc_diagnostics.attempts;
    started = millis();
    asymcred_pkoc_config_t config = {};
    // Public bench identifier, not a key or an access-control authorization.
    const uint32_t identity[3] = {HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()};
    memcpy(config.reader_id + 16, identity, sizeof(identity));
    config.supported_versions = versions;
    config.supported_version_count = sizeof(versions) / sizeof(versions[0]);
    config.require_signature = true;
    const auto status = asymcred_pkoc_begin(&pkoc, &config, &crypto);
    if (status != ASYMCRED_OK) { finish(status); return; }
    active = true;
}
