# Bench verification — 2026-10-31

This records observed results on one STM32F103C8T6 reader, one working enrolled PKOC card, the listed ATECC/PN532 modules and a WCH RS-485 adapter. It separates host tests, physical tests and unresolved scope.

The operating firmware is **`c8_reader_hsu`**, using Adafruit PN532 1.3.4 and BusIO 1.17.4. The buzzer-enabled image occupies **60,236 bytes Flash / 10,524 bytes static RAM** under enforced C8 limits of 65,536 / 20,480 bytes. ST-Link programming completed with **Verified OK**. The NFC-only probe is not the operating image.

[Sanitized exact log excerpts](evidence/bench-summary.txt) and [reader host-test output](evidence/reader-host-tests.txt) are committed. Raw logs, capture files, private configuration and local troubleshooting notes remain excluded from Git. Card fingerprints and host filesystem paths are removed from the public excerpts.

## Physical results

| Check | Observed result and scope |
|---|---|
| RS-485 / Secure Channel | Correct-key online/secure operation with the replacement transceiver; earlier wrong-key checks received peer handshake cryptograms but never established SC |
| Real card authentication | Fresh challenge, verified signature, SW=9000 and encrypted 256-bit card events received by actual OSDP.Net |
| Held-card guard | Earlier continuous 60-sample/minute observation retained the same report count; controller reconnect did not emit another held-card event |
| Removal and re-presentation | Confirmed absence followed by exactly one new received event, authentication 267 ms; 150-second controller run had zero disconnects |
| Adafruit startup | Full Adafruit cold boot reached PN532 firmware 0x32010607/ready=1; subsequent MCU-only reflashes also recovered. Cold-boot evidence predates the presentation/buzzer edits; the startup driver remained the same |
| LED | User visually confirmed on/off/timed operation. Secure commands and actual PC13 states passed. Feedback/NFC tests were performed on successive builds; no new LED visual test is claimed for the final documentation-only preparation |
| Buzzer | With confirmed S→PB0, +→3.3 V, −→GND, two 300 ms beeps were heard. Secure pattern/expiry/Off commands passed; PB0 readback was HIGH during sound and LOW afterward |
| Final buzzer-enabled controller run | Zero disconnects and exactly one received real encrypted card event; 90 seconds of observation after the buzzer test |
| Final reader health | PN532 ready, no failed authentications or NFC poll/UART errors in the buzzer-enabled observations; observed maximum PD service gap about 67.1 ms |

During the final controller run, one read-beep request accompanied one reported event, despite two successful verifications including reselection. A later diagnostic after the harness stopped showed a second queued read/beep; it did **not** establish a second controller delivery. Read-beep counters are not acoustic recordings. The firmware can still consider the connection online briefly after the controller stops; it clears outputs/events when the loss is detected.

## Host and build checks

Pre-commit checks re-ran **4 upstream AsymCred suites, 31 upstream OSDP suites, all 20 reader suites, and independent OpenSSL verification**, all passing. Bootstrap rebuilt the console/Interop and firmware while preserving the existing key, generated header and controller configuration byte-for-byte. See [pre-commit evidence](evidence/precommit-checks.txt).

The reader CMake suite has **20 passing tests** under AddressSanitizer and UndefinedBehaviorSanitizer. Coverage includes:

- Entropy buffering and no nested refill consumption.
- PN532 portable/STM32/software-I2C/HSU frame and deadline behavior.
- Adafruit adapter ACK byte counts, frame validation, APDU capacity and error recovery.
- Presentation session loss, clean absence, verified-credential suppression, randomized/reused UIDs, transport errors and clock wrap.
- Feedback authorization, atomic LED validation, timings, Off precedence and disconnected clearing.
- Crypto exercises and the actual OSDP wolfCrypt port's AES, keys, CBC, MAC, payload, wrap, PD, ACU, loopback and replay cases.

The Adafruit host adapter test uses a fake vendor command API; it is not an RF simulation or an electrical test. `scripts/test_host.sh` additionally runs upstream suites and independent Python/OpenSSL verification. Test fixture keys are disposable software values.

Bootstrap, ACUConsole/Interop builds, the firmware build and host checks can be repeated using [Quickstart](quickstart.md). Documentation/publication preparation does not require another flash or a change to the installed bench key.

## Earlier failures and changes

- The original C6 was replaced by a C8 at the user's request; C6 capacity is not silently enlarged.
- An original RS-485 module failed physical receive tests. A replacement passed Secure Channel and wrong-key rejection after wiring/driver troubleshooting.
- Shared-I2C, split-I2C and software-I2C PN532 trials had stalls/RF errors. Some briefly delivered a card event but did not establish stability. The final PN532 transport is HSU on PB10/PB11, with ATECC separately on PB6/PB7.
- Earlier HSU startup/recovery attempts and an isolated Adafruit probe failed during the investigation. Those observations did not prove a wiring fault. The full Adafruit firmware subsequently passed a real cold boot.
- An early full-Adafruit authentication timed out while OSDP remained connected. Later diagnostics and startup retry recovered operation.
- Earlier presentation logic emitted duplicates for a stationary card, including after controller reconnect. The guard now retains the reported verified credential across RF session loss and requires clean absence before rearming it.
- The earlier guard build's initial lift/reposition sequence included a failed authentication; no invalid/incomplete credential was delivered. Later successful checks do not erase that failed trial.

## Limits

This is bench evidence, not certification. A wider card/module matrix, long-duration soak, worst-case stack/heap measurement, every power sequence and formal OSDP conformance remain unverified. RF interruptions can affect absence detection; the measured clean interval is a design threshold, not proof of physical movement under all conditions.

Buzzer S-pin current is unmeasured. GPIO readback and sound confirm function, not electrical loading. The SCBK is compiled into readable MCU Flash; ATECC is not used to protect that key. No card enrollment, ATECC configuration writes, provisioning, key generation or lock changes were performed. There is no local access policy, relay implementation, tamper enforcement or production firmware-update trust mechanism.
