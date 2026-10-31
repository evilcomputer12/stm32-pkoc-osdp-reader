# How the reader works

The STM32 is an OSDP **Peripheral Device (PD)**. OSDP.Net on the Mac is the controller/ACU. Native STM32 USB exposes diagnostics, not a PC/SC smart-card reader or an OSDP controller connection.

## Authentication to delivery

1. The PN532 discovers an ISO14443-4 target over its independent USART3 connection.
2. With OSDP online and Secure Channel established, AsymCred starts the PKOC SELECT/AUTHENTICATE sequence.
3. Fresh randomness comes from the ATECC. SHA-256 uses wolfCrypt; external P-256 ECDSA verification uses CryptoAuthLib and the ATECC.
4. A complete successful result must have `signature_verified` before credential extraction. PN532 status/framing and card status words are checked; a detected UID is not sufficient.
5. The verified public-key X coordinate is encoded as a **256-bit RAW** credential for reader 0 and queued through the PD event queue.
6. Successful queueing of a new presentation requests the **100 ms read beep**. The controller receives the event in a poll response over Secure Channel. Queueing and actual controller receipt are distinct.
7. Access authorization belongs to the controller. This firmware has no relay, credential allowlist, door policy or automatic LED grant.

## Independent communication paths

| Path | Implementation |
|---|---|
| OSDP | USART1 PA9/PA10, 9600 baud, 256-byte IRQ RX ring, direction PA8 |
| PN532 | USART3 PB10/PB11, 115200 baud, 512-byte IRQ RX ring |
| ATECC | I2C1 PB6/PB7, 100 kHz, configured-address discovery after wake |
| Diagnostics | Native USB CDC; does not carry OSDP traffic |

The RS-485 writer waits for UART **TC**, the final physical stop bit, before returning A8 to receive. It uses bounded waits and counts RX/TX failures. USB output does not indefinitely block the reader waiting for a terminal.

NFC transport waits call the cooperative reader service hook so OSDP continues to run. The UARTs retain separate interrupt ownership rather than sharing a generic Arduino serial ISR. ATECC operations use a busy guard and buffered entropy to avoid re-entering the secure element while other work requests randomness. None of this turns every cryptographic operation into a fully asynchronous state machine; measured service gaps and further timing limits are in [Verification](verification.md).

## Why the Adafruit adapter exists

Adafruit PN532 1.3.4 and BusIO 1.17.4 are used without modifying their installed source. The actual library supplies UART wake/startup, command framing and ACK handling. `ReaderAdafruitSerial` adapts that API to the project's USART3 driver and cooperative service hook.

The stock 64-byte APDU receive path is too small for the observed **135-byte PKOC AUTHENTICATE response**. `AdafruitReader` checks a complete variable-length response: preamble, LEN/LCS, direction/response command, checksum, postamble, capacity and deadline. It requires six consumed ACK bytes, propagates UART errors, rejects chained/incomplete responses, and performs bounded resynchronization after an error. The APDU bridge preserves card SW1/SW2 and avoids narrowing a capacity of 256 to an 8-bit zero.

Startup follows the actual Adafruit wake/Normal-mode sequence. A failed initial firmware-version query receives one bounded abort/resync retry, which helps when the MCU resets while the PN532 retains an outstanding operation. Three consecutive live transport failures mark NFC unavailable; this is not an unlimited auto-recovery loop.

## Presentations and duplicates

`PresentationGuard` distinguishes loss of the selected RF session from confirmed card absence. Three complete selected-target timeouts start fresh discovery while retaining the last reported credential. Clean no-target discovery for at least **one second and three samples** rearms the presentation. Transport failures interrupt that absence proof.

A reselected target is authenticated again. Duplicate suppression compares the **verified 32-byte credential**, not a UID that may be random or reused. The same reported credential is suppressed until confirmed absence; a different verified credential may be reported immediately. The credential cache clears on explicit reinitialization/reboot. Reboot can therefore allow a fresh read of a still-present card.

Card authentication begins only while both online and secure. Events expire after two seconds and are cleared on detected loss of connection/security; there is no persistent offline credential backlog. There is necessarily a detection interval between the controller stopping and the reader noticing it. A beep during that interval does not prove delivery.

## Feedback and security boundaries

Application-owned LED/buzzer state is used because the pinned PD library also observes commands that the application rejects. The GPIOs follow only validated commands. LED batches are validated completely before applying any record. Secure reader-0 LED-0 Green/Black and default-tone/Off buzzer commands are supported; unsupported commands are rejected.

PC13 is active LOW/open-drain. PB0 is active HIGH when enabled. Temporary LED/buzzer timers, clock wrap, controller Off precedence and disconnected clearing have host coverage. Physical tests exercised the actual pins and user-observed sound/light.

The local bench SCBK is generated by bootstrap and compiled into Flash. ATECC supplies RNG and signature verification; it does not protect this SCBK from physical Flash extraction. Default-key installation, persistent KEYSET, enrollment, configuration writes and ATECC lock operations are not implemented. Physical/debug-port protection, firmware authenticity, tamper controls, long-term credential policy and production certification are outside the demonstrated scope.
