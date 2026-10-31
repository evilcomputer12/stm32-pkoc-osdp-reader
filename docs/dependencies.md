# Dependencies, adaptations and licenses

Git dependencies are fetched at exact commits in [dependencies.lock.json](../dependencies.lock.json). Platform/framework/compiler and registry library versions are pinned in [platformio.ini](../platformio.ini). Bootstrap checks existing Git dependency HEADs and tracked changes instead of silently overwriting them.

## Current reader

| Component | Role | License information in source |
|---|---|---|
| [AsymCred](https://github.com/Z-bit-Systems-LLC/AsymCred) | C11 PKOC reader/core | `LICENSE.md`: reader GPL-3.0-or-later or commercial; separate card applet Apache-2.0 |
| [OSDP-Embedded](https://github.com/Z-bit-Systems-LLC/OSDP-Embedded) | Core + PD + wolfCrypt port | `LICENSE.md`: GPL-3.0-or-later or commercial |
| [OSDP.Net](https://github.com/Z-bit-Systems-LLC/OSDP.Net) | Console and test controller | `LICENSE.txt`: Apache-2.0 |
| [Adafruit PN532](https://github.com/adafruit/Adafruit-PN532), 1.3.4 | UART startup/commands | `license.txt`: BSD 3-Clause |
| [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO), 1.17.4 | Adafruit dependency | `LICENSE`: MIT |
| [wolfSSL/wolfCrypt](https://github.com/wolfSSL/wolfssl) | AES-128, SHA-256; expanded host crypto exercises | `COPYING` and upstream GPL/commercial terms |
| [CryptoAuthLib](https://github.com/MicrochipTech/cryptoauthlib) | ATECC HAL, RNG, external verify | `license.txt`: Microchip terms, including use with Microchip products |
| [STM32duino](https://github.com/stm32duino/Arduino_Core_STM32) | Arduino STM32 core and ST HAL | Component-specific notices in the PlatformIO framework package |

The project-level [LICENSE](../LICENSE) covers project-authored integration, not every dependency. Third-party licenses are retained, not replaced. This repository publishes source integration and fetch instructions, not a binary release. License compatibility for distributing a combined binary, including CryptoAuthLib's hardware-use terms, has not been resolved by assigning a license to the integration code. Upstream commercial alternatives, if needed, are matters for the relevant authors; no such license is claimed here.

## Local integration choices

- **Adafruit source stays unchanged.** `ReaderAdafruitSerial` adapts HardwareSerial to the owned USART3 IRQ/ring and cooperative OSDP servicing. `AdafruitReader` uses the actual vendor startup/command/ACK API, then validates variable-length replies rather than truncating a 135-byte PKOC response to the vendor's stock 64-byte APDU buffer. Host parser tests simulate the vendor command API; physical checks exercise the actual library.
- **OSDP C/C++ compatibility overlay.** A pinned public C header names a parameter `template`. The build creates a one-token parameter rename in its own build directory. The dependency checkout is not edited. Core/PD/wolfCrypt port are linked into the MCU; ACU is host-only.
- **wolfCrypt configuration.** One project `user_settings.h` is selected globally. The core's automatic Arduino preset is suppressed. The reader uses AES-128 including decryption and SHA-256; ATECC supplies RNG and P-256 verification. Host exercises additionally enable software P-256, signing, ECDH and ASN.1. OS entropy is host-only; the MCU has no weak RNG fallback.
- **ATECC HAL.** A bounded Wire/I2C1 HAL, wake-before-discovery, a busy guard and entropy buffering keep operations coordinated. Write, lock, key generation and signing operations are disabled for the reader. No device slot is allocated or provisioned by this project.
- **Feedback.** Application-owned validated state drives pins because upstream PD observation state can include rejected commands. The enabled default build drives PB0; other builds leave it as input. OSDP.Net resets its device instance after NAK 0x09, so negative output tests run separately and wait for recovery.
- **Upload.** The project SCons action calls native Homebrew OpenOCD 0.12.0 with an argument list. This handled paths with spaces and program/verify/reset on the attached ST-Link. The bundled development OpenOCD previously failed during reset; no global tool cache was patched.

## Historical diagnostic builds

The normal target is **`c8_reader_hsu`**, using Adafruit. Earlier I2C, split-bus, software-I2C, C6, Seeed-HSU and isolated Elechouse/Adafruit probes are retained as diagnostic source and regression tests; they are not interchangeable working configurations.

`lib/PN532` is a project-owned, modified subset of the Seeed PN532 files from the pinned commit, retaining Adafruit/Seeed notices and [BSD license](../lib/PN532/LICENSE.md). Changes include checked frame lengths/ACK/checksums, bounded waits, a service hook, passive-target validation, APDU capacity/status handling, HSU framing and ISO14443-4 presence checks. I2C variants cover portable Wire, STM32 HAL polling and software-I2C transport. Ready polling reads the status byte before a complete frame. These changes were useful for diagnosis but did not establish a stable PN532 I2C reader on this bench.

The pinned Elechouse checkout supplies isolated comparison probes. Its HSU build script applies explicit byte casts to a generated build copy, leaving the checkout unchanged. Historical UART wake/SAM-first changes and the later Adafruit switch are covered by tests and the [verification history](verification.md#earlier-failures-and-changes).

[Jonathan Horvath / bytedreamer](https://github.com/bytedreamer), [Z-bit Systems](https://github.com/Z-bit-Systems-LLC), and the other upstream authors are credited in the [README](../README.md#credits) and [NOTICE](../NOTICE). Attribution does not imply endorsement or sponsorship.
