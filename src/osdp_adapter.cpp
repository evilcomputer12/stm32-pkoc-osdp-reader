#include <Arduino.h>
#include "reader.h"
#include "feedback.h"
#ifdef READER_NFC
#include "nfc_adapter.h"
#endif
#include "osdp/osdp_pd.h"
#include "osdp/osdp_commands.h"
#include "osdp/osdp_replies.h"
#ifdef READER_SECURE
#include "osdp_sc_wolfcrypt.h"
#include "bench_key.h"
static osdp_sc_wolfcrypt_t sc_context;
#endif

// USART1: PA9 TX, PA10 floating RX, PA8 DE+/RE. A bounded register driver
// avoids pulling the general-purpose Arduino UART TX machinery into the C6.
// SerialUSB remains diagnostics only. Interrupt RX continues during NFC work.
struct Usart1Rs485 {
    volatile uint16_t head = 0, tail = 0;
    volatile uint32_t overruns = 0;
    uint8_t rx[256];
};
static Usart1Rs485 osdpUsart1;
extern "C" void USART1_IRQHandler(void) {
    const uint32_t status = USART1->SR;
    if (status & (USART_SR_RXNE | USART_SR_ORE | USART_SR_FE | USART_SR_NE)) {
        const uint8_t value = USART1->DR; // SR then DR clears receive/error flags.
        if (status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE)) { ++osdpUsart1.overruns; }
        if (status & USART_SR_RXNE) {
            const uint16_t next = (osdpUsart1.head + 1U) & 255U;
            if (next == osdpUsart1.tail) { ++osdpUsart1.overruns; }
            else { osdpUsart1.rx[osdpUsart1.head] = value; osdpUsart1.head = next; }
        }
    }
}
static osdp_pd_t pd;
static uint8_t events[96];
static uint8_t reply_buffer[48];
static uint32_t queued_at;
static bool initialized;
ReaderMetrics reader_metrics = {};

static int receive(void *, uint8_t *out, size_t cap) {
    reader_metrics.rx_errors = osdpUsart1.overruns;
    static uint8_t last_rx = 1;
    const uint8_t rx = (GPIOA->IDR >> 10) & 1;
    if (rx != last_rx) { ++reader_metrics.rx_edges; last_rx = rx; }
    size_t length = 0;
    while (length < cap && osdpUsart1.head != osdpUsart1.tail) {
        out[length++] = osdpUsart1.rx[osdpUsart1.tail];
        osdpUsart1.tail = (osdpUsart1.tail + 1U) & 255U;
    }
    reader_metrics.rx_bytes += length;
    return static_cast<int>(length);
}

static int write_rs485(const uint8_t *data, size_t length, bool capture_echo) {
    // The receiver is physically disabled during TX (/RE=1). Suppress floating
    // RO/echo interrupts only for that half-duplex interval.
    if (!capture_echo) { USART1->CR1 &= ~USART_CR1_RXNEIE; }
    digitalWrite(PA8, HIGH);
    const uint32_t timeout = static_cast<uint32_t>((length * 10000U + 9599U) / 9600U) + 10U;
    const uint32_t start = millis();
    bool complete = true;
    for (size_t i = 0; i < length && complete; ++i) {
        while (!(USART1->SR & USART_SR_TXE)) {
            if (millis() - start >= timeout) { complete = false; break; }
        }
        if (complete) { USART1->DR = data[i]; }
    }
    while (complete && !(USART1->SR & USART_SR_TC)) {
        if (millis() - start >= timeout) { complete = false; }
    }
    // TC, not TXE: the last stop bit has physically left PA9.
    digitalWrite(PA8, LOW);
    if (!capture_echo) {
        (void)USART1->SR; (void)USART1->DR;
        USART1->CR1 |= USART_CR1_RXNEIE;
    }
    if (!complete) { ++reader_metrics.tx_errors; return 0; }
    reader_metrics.tx_bytes += length;
    return static_cast<int>(length);
}

static int transmit(void *, const uint8_t *data, size_t length) {
    return write_rs485(data, length, false);
}

int reader_rs485_tx_probe(bool capture_echo, bool *echo_matches) {
    // Explicit USB bench command only. Stop the controller before using this;
    // never inject diagnostic text into an established OSDP connection.
    if (echo_matches) { *echo_matches = false; }
    if (!initialized || reader_pd_online() || reader_pd_secure()) { return -1; }
    static const uint8_t pattern[] = "STM32 RS485 TEST\r\n";
    // Echo capture requires /RE on GND, or A9-to-A10 with RO disconnected.
    const uint16_t first = osdpUsart1.head;
    const uint32_t errors = osdpUsart1.overruns;
    const int written = write_rs485(pattern, sizeof(pattern) - 1, capture_echo);
    if (capture_echo && echo_matches) {
        delayMicroseconds(200); // Allow the final RX IRQ to finish after TX TC.
        const uint16_t length = (osdpUsart1.head - first) & 255U;
        bool matched = length == sizeof(pattern) - 1 && errors == osdpUsart1.overruns;
        for (uint16_t i = 0; matched && i < length; ++i) {
            matched = osdpUsart1.rx[(first + i) & 255U] == pattern[i];
        }
        *echo_matches = matched;
    }
    return written;
}

static uint32_t now(void *) { return millis(); }

static osdp_status_t command(void *, uint8_t code, const uint8_t *data, size_t length,
                             osdp_pd_reply_t *reply) {
    reply->payload = reply_buffer;
    reply->payload_len = 0;
    switch (code) {
    case OSDP_CMD_POLL:
        if (length != 0) { return OSDP_ERR_BAD_LENGTH; }
        reply->code = OSDP_REPLY_ACK;
        return OSDP_OK;
    case OSDP_CMD_ID: {
        if (length != 1) { return OSDP_ERR_BAD_LENGTH; }
        // Unassigned vendor value: this is a personal bench reader, not a
        // product manufactured by Z-bit or ST. Serial is derived from MCU UID.
        const osdp_pdid_t id = {{0, 0, 0}, 1, 1,
            HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2(), 0, 1, 0};
        reply->code = OSDP_REPLY_PDID;
        return osdp_pdid_build(&id, reply_buffer, sizeof(reply_buffer), &reply->payload_len);
    }
    case OSDP_CMD_LSTAT:
        if (length != 0) { return OSDP_ERR_BAD_LENGTH; }
        reply_buffer[0] = 0; reply_buffer[1] = 0;
        reply->code = OSDP_REPLY_LSTATR; reply->payload_len = 2;
        return OSDP_OK;
    case OSDP_CMD_RSTAT:
        if (length != 0) { return OSDP_ERR_BAD_LENGTH; }
#ifdef READER_NFC
        reply_buffer[0] = nfc_diagnostics.ready ? 0 : OSDP_RSTATR_NOT_CONNECTED;
#else
        reply_buffer[0] = OSDP_RSTATR_NOT_CONNECTED;
#endif
        reply->code = OSDP_REPLY_RSTATR; reply->payload_len = 1;
        return OSDP_OK;
    case OSDP_CMD_LED:
    case OSDP_CMD_BUZ:
        reply->code = OSDP_REPLY_ACK;
        return reader_feedback_command(code, data, length, reader_pd_online() && reader_pd_secure());
    case OSDP_CMD_OUT:
        return OSDP_ERR_INVALID_ARG; // No relay/door output is fitted.
    default:
        return OSDP_ERR_NOT_SUPPORTED; // Includes COMSET and persistent KEYSET.
    }
}

bool reader_pd_begin() {
    reader_feedback_begin();
    digitalWrite(PA8, LOW);
    pinMode(PA8, OUTPUT);
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_USART1_CLK_ENABLE();
    GPIO_InitTypeDef pins = {};
    pins.Pin = GPIO_PIN_9; pins.Mode = GPIO_MODE_AF_PP;
    pins.Speed = GPIO_SPEED_FREQ_HIGH; pins.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &pins);
    pins.Pin = GPIO_PIN_10; pins.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOA, &pins);
    USART1->CR1 = 0; USART1->CR2 = 0; USART1->CR3 = 0;
    USART1->BRR = (HAL_RCC_GetPCLK2Freq() + 4800U) / 9600U;
    USART1->CR1 = USART_CR1_TE | USART_CR1_RE | USART_CR1_RXNEIE | USART_CR1_UE;
    HAL_NVIC_SetPriority(USART1_IRQn, 1, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    osdp_pd_init(&pd, 0);
    const osdp_pd_transport_t transport = {receive, transmit, now, nullptr};
    osdp_pd_set_transport(&pd, &transport);
    osdp_pd_set_command_handler(&pd, command, nullptr);
    osdp_pd_set_event_queue(&pd, events, sizeof(events));
#ifdef READER_SECURE
    osdp_sc_crypto_t crypto = {};
    if (osdp_sc_wolfcrypt_aes128(&sc_context, &crypto) != OSDP_OK ||
        osdp_sc_wolfcrypt_rng(&sc_context, &crypto) != OSDP_OK) { return false; }
    // The vtable's shared user stays bound to sc_context for AES and RNG.
    osdp_pd_set_sc_crypto(&pd, &crypto);
    osdp_pd_set_sc_scbk(&pd, bench_scbk);
    const uint32_t serial = HAL_GetUIDw0() ^ HAL_GetUIDw1() ^ HAL_GetUIDw2();
    const uint8_t cuid[8] = {0, 0, 0, 1, 1, uint8_t(serial), uint8_t(serial >> 8), uint8_t(serial >> 16)};
    osdp_pd_set_sc_cuid(&pd, cuid);
#endif
    const osdp_pdcap_record_t caps[] = {{3, 1, 0}, {4, 2, 1},
#if READER_BUZZER_ENABLED
        {5, 2, 0},
#endif
        {13, 0, 1}, {16, 2, 0}};
    // The library supplies FC 8/9/10 from its actual CRC, SC and buffer state.
    initialized = osdp_pd_set_pdcap(&pd, caps, sizeof(caps)/sizeof(caps[0]), nullptr) == OSDP_OK;
    return initialized;
}

void reader_pd_tick() {
    if (!initialized) { return; }
    static uint32_t previous;
    const uint32_t current = micros();
    if (previous != 0 && current - previous > reader_metrics.service_gap_us) {
        reader_metrics.service_gap_us = current - previous;
    }
    previous = current;
    ++reader_metrics.ticks;
#ifdef READER_SECURE
    if (!reader_pd_secure()) { osdp_pd_clear_events(&pd); }
#endif
    if (osdp_pd_event_pending(&pd) && millis() - queued_at > 2000) {
        osdp_pd_clear_events(&pd);
    }
    osdp_pd_tick(&pd);
    reader_feedback_tick(reader_pd_online() && reader_pd_secure());
}

bool reader_pd_online() { return initialized && osdp_pd_is_online(&pd); }
bool reader_pd_secure() { return initialized && osdp_pd_sc_established(&pd); }
size_t reader_pd_size() { return sizeof(pd); }

bool reader_enqueue(const uint8_t *credential, size_t bytes, uint16_t bits) {
    if (!reader_pd_online()) { return false; }
#ifdef READER_SECURE
    if (!reader_pd_secure()) { return false; }
#endif
    uint8_t payload[36]; size_t written = 0;
    const osdp_raw_t raw = {0, OSDP_RAW_FORMAT_RAW, bits, credential, bytes};
    if (osdp_raw_build(&raw, payload, sizeof(payload), &written) != OSDP_OK) { return false; }
    if (osdp_pd_enqueue_event(&pd, OSDP_REPLY_RAW, payload, written) != OSDP_OK) { return false; }
    queued_at = millis();
    return true;
}
