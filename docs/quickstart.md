# Run the reader yourself

## Resume the existing working bench

You do not need to reinstall tools or reflash just to read a card.

1. Keep the [working wiring](hardware.md) and both PN532 switches OFF. Connect the STM32's own USB, ST-Link as needed, and the controller's USB-to-RS-485 adapter.
2. Open the project folder in Finder. Double-click **Start OSDP Console.command**. Alternatively, open Terminal in the project folder and run `python3 scripts/start_console.py`.
3. In ACUConsole choose **Connections → Start Serial Connection**. The existing local configuration already contains the matching key. Wait for **connected with secure channel** for address 0.
4. Remove the working PKOC card at least 10 cm from the antenna for **5 seconds**.
5. Place it flat on/near the antenna, away from metal. Expect a **100 ms beep** and a **Received raw card data reply** with 256 bits.
6. Leave the card still. It should not repeatedly report the same presentation. Remove it for 5 seconds and present it again for another event.
7. Use **Connections → Stop Connections** before starting another controller/test program. Only one program can own the USB-to-RS-485 port.

A beep means authentication succeeded and an event was queued. Controller receipt is confirmed by its card-data log. The LED represents a controller command, not an automatic access decision. See [Feedback](feedback.md) for independent buzzer and LED tests.

## Fresh source setup on macOS Apple Silicon

The tested host is an Apple Silicon Mac. Windows/Linux and Intel Mac setup are not validated by this guide. The bootstrap currently publishes an `osx-arm64` console and uses Homebrew OpenOCD paths.

1. Install Xcode Command Line Tools (`xcode-select --install`, if missing) and [Homebrew](https://brew.sh/).
2. Download/clone this repository into a folder you own. Open Terminal and change into that folder. All commands below run from the repository root.
3. Install the tools:

```sh
brew install platformio open-ocd stlink dotnet cmake
pio --version
dotnet --list-sdks
openocd --version
```

The pinned ACUConsole build needs the **.NET 10 SDK**. If a future Homebrew `dotnet` formula no longer includes it, install .NET 10 alongside the newer SDK. PlatformIO installs the pinned STM32 platform, Arduino core and compiler from `platformio.ini`.

4. Connect the USB-to-RS-485 adapter and prepare dependencies/configuration:

```sh
python3 scripts/bootstrap.py
```

This fetches the exact Git revisions in `dependencies.lock.json`, verifies existing checkouts against those revisions, creates a random local SCBK only when absent, generates the firmware key header and matching console configuration, builds ACUConsole/Interop, and builds `c8_reader_hsu`. It does **not** flash, enroll cards, change ATECC configuration, or lock anything.

If the adapter is not uniquely discovered, use its actual serial port:

```sh
python3 scripts/bootstrap.py --port /dev/cu.YOUR_RS485_PORT
```

`--port` is used when creating new console settings. The launcher later refreshes the port for a single WCH adapter without changing the key. `--skip-build` prepares dependencies/configuration without compiling.

5. With the current C8 wiring connected, flash explicitly:

```sh
pio run -e c8_reader_hsu -t upload
```

Expect **Verified OK** from OpenOCD. The C8 limits are enforced at **65,536 bytes Flash / 20,480 bytes RAM**. Do not select the historical C6 or NFC-only probe environments for normal operation.

6. Open the console:

```sh
python3 scripts/start_console.py
```

Then follow the card test above. The `.command` launcher provides the same operation from Finder.

## Preserve the local bench key

The following files are generated locally and ignored by Git:

- `config/bench-scbk.hex`: the controller test harness's 16-byte SCBK, stored as hex.
- `config/bench_key.h`: the same key compiled into the MCU firmware.
- `tools/acuconsole/appsettings.config`: console settings containing the matching key.

A **fresh checkout makes a different key**. Flash that checkout's firmware before using its controller configuration, or securely retain the existing bench configuration when resuming the same reader. Do not delete or manually mix these files to troubleshoot a connection. Bootstrap preserves an existing console configuration; it does not silently overwrite an unrelated controller key.

The configured SCBK is stored in MCU Flash, not protected in an ATECC slot. Do not publish `.bin`/`.elf` files from this bench: they embed it. Git ignores generated firmware and local settings.

## USB diagnostics

The MCU's USB CDC is separate from the RS-485 adapter. Create a small Python environment once:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install pyserial==3.5
.venv/bin/python scripts/diagnostics.py inqf
```

The script discovers STM32 VID/PID automatically. Use `--port /dev/cu.YOUR_STM32_PORT` if multiple STM32 CDC devices exist. It waits for a harmless `pong` before sending the selected commands.

| Command | Meaning |
|---|---|
| `p` | Firmware ping |
| `i` | OSDP online/secure state, UART counters and maximum service gap |
| `a` | ATECC discovery/revision and lock state |
| `n` | PN532 readiness, authentication attempts/results, SW and latency |
| `q` | NFC polling, selected-target status and transport errors |
| `f` | LED/PB0 logical states, physical pin readback and command/beep counters |
| `h` | Full command list |

`online=1 secure=1` is the useful connected state. In this pinned stack, `secure=1` may remain visible after `online=0`; the firmware requires **both** before using authenticated outputs or initiating a new card authentication.

`u` reinitializes peripherals and is refused while the reader is online or retains secure state. Intrusive GPIO/I2C/RS-485 probes are troubleshooting tools, not normal test steps. `s` queues a clearly marked synthetic test credential; it does not prove NFC or real-card authentication and does not request a read beep.

## Automated checks

Host tests do not require attached hardware:

```sh
sh scripts/test_host.sh
```

This runs the upstream AsymCred and OSDP-Embedded checks, all 20 reader suites, and an independent Python/OpenSSL verification of a wolfCrypt signature. The crypto fixture private keys are disposable software test values, not card or device secrets.

For physical OSDP tests, first stop ACUConsole's connection. Replace the port below with the actual RS-485 port. These commands read the local key file; do not paste its contents into a command or issue report.

```sh
# Real card, Secure Channel; present the card during the 60-second window.
dotnet tools/interop/bin/Release/net10.0/Interop.dll /dev/cu.YOUR_RS485_PORT config/bench-scbk.hex - 60 --require-card

# Incorrect-key rejection: deliberately changes only the harness's in-memory key.
dotnet tools/interop/bin/Release/net10.0/Interop.dll /dev/cu.YOUR_RS485_PORT wrong:config/bench-scbk.hex - 10
```

A correct-key run must end online/secure, with zero disconnects and at least one real-card event. The wrong-key test only passes if the peer actually answers the handshake but fails to establish Secure Channel. Reconnect with the correct key afterward. No KEYSET is performed.

## If something does not work

| Symptom | Check |
|---|---|
| Port busy | Stop the other ACUConsole/Interop/serial program; do not run two controllers |
| WCH adapter absent | USB data cable, adapter enumeration and an appropriate driver for the specific adapter |
| No OSDP replies | Common GND, DI→A9, RO→A10, DE+/RE→A8, module power and differential polarity; one failed module was replaced during this build |
| Secure Channel fails | Controller/firmware key pairing and correct port/address/baud; avoid altering working keys as a first step |
| PN532 not ready | Both switches OFF; B10→RX, B11←TX; correct VCC for this breakout; read diagnostics before moving wires |
| PN532 remains stalled after MCU reset | Remove the card, disconnect all power until module LEDs are fully off for 10 seconds, reconnect, then check readiness |
| No card event | Secure Channel active, compatible already-enrolled PKOC card, correct antenna placement and a confirmed removal interval |
| Beep but no LED | LED commands come from the controller; run the explicit [LED test](feedback.md) |

Do not treat a power cycle, one successful read, or a synthesized card event as proof that all hardware is healthy. [Verification](verification.md) records the actual scope.
