# STM32 Blue Pill PKOC reader with OSDP Secure Channel

A working bench reader built around an **STM32F103C8T6**, **PN532**, **ATECC secure element**, and **RS-485**. It authenticates an existing PKOC card using a fresh challenge and signature verification, then sends the verified 256-bit credential to **OSDP.Net over OSDP Secure Channel**.

The default firmware is complete and has been flashed and tested on real hardware. It uses **Adafruit PN532 over HSU/UART**, gives a short beep for a new authenticated read, and lets the controller operate the Blue Pill's onboard LED. The controller makes the access decision; a valid signature or beep alone is not an access grant.

This is a learning and community bench project, not a certified access-control product. The source, pinned dependencies, host tests and observed limitations are included so others can understand and reproduce the work.

## Credits

Special thanks to **[Jonathan Horvath / bytedreamer](https://github.com/bytedreamer)** and **[Z-bit Systems, LLC](https://github.com/Z-bit-Systems-LLC)**. Their projects provide the central building blocks:

| Upstream project | Role here |
|---|---|
| [AsymCred](https://github.com/Z-bit-Systems-LLC/AsymCred) | Embedded PKOC protocol and credential authentication flow |
| [OSDP-Embedded](https://github.com/Z-bit-Systems-LLC/OSDP-Embedded) | OSDP peripheral device and Secure Channel |
| [OSDP.Net](https://github.com/Z-bit-Systems-LLC/OSDP.Net) | Desktop controller, ACUConsole and interoperability tests |
| [Cred-Bench](https://github.com/Z-bit-Systems-LLC/Cred-Bench) | Related desktop credential tooling/reference; not part of this MCU firmware |

Thanks also to [Adafruit](https://github.com/adafruit/Adafruit-PN532), [STM32duino](https://github.com/stm32duino/Arduino_Core_STM32), [wolfSSL](https://github.com/wolfSSL/wolfssl), [Microchip](https://github.com/MicrochipTech/cryptoauthlib), [Seeed Studio](https://github.com/Seeed-Studio/PN532), [Elechouse](https://github.com/elechouse/PN532), and their contributors. This repository supplies the board integration, adapters, presentation handling, diagnostics and tests; it does not claim authorship of the upstream libraries or endorsement from their authors.

## Hardware and where I bought it

**I chose Temu and AliExpress because they are viable options for shipping to my country. This project is not sponsored. I am sharing the links and what worked for me to help others.**

These are the purchase references supplied for this build. Check the selected listing variant against the wiring guide; the links do not establish that every board or card sold under a listing is identical. The storefront contents could not be independently inspected during documentation preparation.

| Hardware | Purchase reference | Notes |
|---|---|---|
| ATECC crypto-chip module | [AliExpress](https://www.aliexpress.com/item/1005012191391337.html) | Already-configured, locked device used for RNG and external P-256 verification |
| STM32F103C8T6 Blue Pill with ST-Link | [Temu](https://share.temu.com/FkH90Crz5WB) | C8: 64 KiB Flash, 20 KiB RAM, enforced even if the probe reports more Flash |
| PN532 module | [Temu](https://share.temu.com/7f1O068VzmB) | Elechouse-style board; both switches OFF for HSU |
| Cards | [AliExpress](https://www.aliexpress.com/item/1005006610737323.html) | Tested with an existing working PKOC credential; a newly bought card is not automatically enrolled |
| RS-485 modules | [Temu](https://share.temu.com/4g978Gv2vGB) | Reader-side transceiver with DI, RO, DE and /RE |
| USB-to-RS-485 adapter | No purchase link supplied | WCH/CH34x adapter on the Mac controller side |
| HW-512 buzzer | No purchase link supplied | S→PB0, +→3.3 V, −→GND on the tested module; see the electrical limitation in the wiring guide |

Also needed: USB data cables, jumper wires and sound solder joints. The ST-Link programs the MCU, the MCU's own USB carries diagnostics, and the USB-to-RS-485 adapter carries OSDP. They are three different connections.

## Working wiring

Make wiring changes with all power disconnected. Full supply and module notes are in [Hardware](docs/hardware.md).

| STM32 connection | Destination |
|---|---|
| PB6 / B6 | ATECC SCL, I2C1 |
| PB7 / B7 | ATECC SDA, I2C1 |
| 3.3 V and GND | ATECC supply and ground |
| PB10 / B10, USART3 TX | PN532 SCL/RX in HSU mode |
| PB11 / B11, USART3 RX | PN532 SDA/TX in HSU mode |
| USB-powered 5 V and GND | Tested Elechouse PN532 VCC and GND |
| PA9 / A9 | RS-485 DI |
| PA10 / A10 | RS-485 RO |
| PA8 / A8 | RS-485 DE and /RE joined together |
| 5 V and GND | Tested RS-485 module supply |
| PB0 / B0 | HW-512 S; its + goes to 3.3 V and − to GND |
| PC13 | Onboard LED, already wired on the Blue Pill |
| PA13 / PA14 | ST-Link SWDIO / SWCLK, with common GND and the appropriate target-voltage connection |

**PN532 switches: 1 OFF, 2 OFF.** The current reader uses UART for the PN532; ATECC remains on I2C. RS-485 A/B connect to the controller adapter's differential pair, with common GND. A/B naming can differ between vendors.

## Start here

For your next hands-on session, use **[the step-by-step guide](docs/quickstart.md)**. It separates reopening the already-working reader from installing and building a fresh copy.

On the Mac already used for this project:

1. Connect the reader and USB-to-RS-485 adapter.
2. Double-click **Start OSDP Console.command** in this folder.
3. Choose **Connections → Start Serial Connection** and wait for **connected with secure channel**.
4. Remove the card for at least **5 seconds**, then present it. Expect a short beep and a **Received raw card data reply** entry.
5. Leave the card still. Remove and re-present it for another read.

The console uses address **0**, **9600 baud**, CRC and Secure Channel, with **200 ms** polling/reply timeout. A held card is not deliberately resent merely because the controller reconnects.

### Build from source on macOS Apple Silicon

Install Xcode Command Line Tools and Homebrew first, then run from the repository root:

```sh
brew install platformio open-ocd stlink dotnet cmake
python3 scripts/bootstrap.py
pio run -e c8_reader_hsu -t upload
```

Bootstrap fetches exact dependency commits, prepares a local random bench Secure Channel key if none exists, publishes ACUConsole, builds the interoperability tool and builds the firmware. **Only the explicit upload command flashes hardware.** Use the .NET 10 SDK for the pinned console build. Other operating systems need host-tool and serial-port adjustments.

Each fresh checkout generates its own local key. Preserve the existing `config/` directory when resuming this bench. A controller and firmware built with different keys will not establish Secure Channel. The firmware contains the key in Flash, so prebuilt images and private configuration are deliberately excluded from Git.

New cards and unconfigured secure elements need a separate provisioning workflow. This repository does not enroll cards, lock ATECC slots, write its configuration, or generate persistent device keys. Use the existing working credential for the documented reproduction steps.

## Buzzer and LED tests

For two audible test beeps: **Commands → Reader Buzzer Control**, device/reader **0**, tone **Default (0x02)**, ON **3**, OFF **3**, count **2**, then **Send**. Time fields are in 100 ms units.

The onboard LED accepts secure **Green** and **Black** commands for reader **0**, LED **0**. Its physical colour is unchanged. See [Feedback](docs/feedback.md) for the two-second indication and exact fields. The stock console does not automatically apply an access policy to every received card.

## What has been verified

- C8 firmware: **60,236 bytes Flash / 10,524 bytes static RAM**, flashed and read-back verified.
- Real PKOC authentication and encrypted 256-bit card delivery to OSDP.Net.
- Secure Channel and rejection of an incorrect bench key.
- Held-card suppression and a confirmed removal/re-presentation cycle.
- Adafruit cold startup on the tested module and subsequent MCU-only restart recovery.
- Onboard LED commands, audible buzzer commands, timed expiry and GPIO readback.
- **20 reader host test suites** under AddressSanitizer/UBSan, plus separately runnable upstream/independent crypto checks.

See [Verification and limits](docs/verification.md) for evidence, failed earlier trials and unverified scope. Static RAM usage is not a worst-case stack/heap measurement. These checks are not OSDP certification or a production security review.

## Finding your way around

| Path | Purpose |
|---|---|
| `src/reader_main.cpp` | Startup, main loop and USB diagnostics |
| `src/nfc_adapter.cpp` | Discovery, authentication and event/beep decisions |
| `src/osdp_adapter.cpp` | RS-485 transport, PD, Secure Channel and event queue |
| `src/atecc_adapter.cpp` | ATECC RNG, external verification and I2C HAL |
| `src/pn532_uart.cpp` | Independent PN532 USART3 transport |
| `include/adafruit_reader.h` | Checked PN532 responses and larger PKOC APDU replies |
| `include/presentation_guard.h` | Removal/reselection and duplicate suppression |
| `src/feedback.cpp` / `include/feedback_state.h` | LED and buzzer outputs/timing |
| `tools/interop/` | OSDP.Net automated controller checks |
| `tests/` | Crypto, parser, presentation, feedback and Secure Channel tests |
| `dependencies.lock.json` / `platformio.ini` | Pinned Git dependencies, platform and registry libraries |

[Architecture](docs/architecture.md) explains how the pieces cooperate. [Dependencies](docs/dependencies.md) explains the adaptations and historical diagnostic builds. Raw bench captures and the original chat handoff stay local; [sanitized evidence](docs/evidence/bench-summary.txt) is included for review.

## License

Project-authored integration code and documentation are provided under **GPL-3.0-or-later**; see [LICENSE](LICENSE) and [NOTICE](NOTICE). Third-party code keeps its own copyright and license. The vendored Seeed/Adafruit PN532 files retain their BSD notices; fetched dependencies have the terms listed in [Dependencies](docs/dependencies.md). No blanket license claim is made for third-party components or a distributed combined firmware image.
