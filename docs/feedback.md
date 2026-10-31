# Buzzer and onboard LED

Feedback operates while the reader is online with OSDP Secure Channel established. The current `c8_reader_hsu` build enables the buzzer; other environments default to disabled unless explicitly configured.

## Card-read beep

The tested HW-512 is wired **S→PB0, +→3.3 V, −→GND**. A new, successfully authenticated PKOC credential requests one **100 ms beep** after its event is queued. A detected UID, failed signature, synthetic test event or suppressed duplicate does not request a beep.

The user heard the switched-DC test pattern. No PWM is needed for this module. The S-pin load current was not measured; [Hardware](hardware.md#buzzer-qualification) explains the limits of that result and how to disable the output for another setup.

A beep confirms authentication and queueing, not a grant or proof of controller receipt. Check ACUConsole's card-data log for delivery. Explicit controller Off takes precedence over a local read beep; detected disconnection clears the output.

## Manual buzzer test in ACUConsole

Start the serial connection and wait for Secure Channel. Choose **Commands → Reader Buzzer Control**, device **0**:

| Field | Value |
|---|---|
| Reader Number | 0 |
| Tone | Default (0x02) |
| ON Time (x100ms) | 3 |
| OFF Time (x100ms) | 3 |
| Count | 2 |

Press **Send**. Expect two 300 ms beeps separated by 300 ms. Selecting **Off (0x01)** stops the buzzer. Use count 1 and on/off 1/1 for one 100 ms beep. Avoid count 0 for a bounded test: the underlying OSDP timing semantics allow continuous operation.

## Onboard LED test

The LED is internally connected to **PC13**, active LOW; it requires no added wire. Firmware drives it with a slow open-drain output. Logical **Green** means lit and **Black** means off; the physical LED keeps its actual colour. Other colours/LED numbers are rejected.

Choose **Commands → Reader LED Control**, device **0**, reader **0**, LED **0**. The dialog walks through basic, temporary and permanent settings. For a two-second indication use:

| Field | Value |
|---|---|
| Temporary control | Set temporary and start timer |
| Temporary on / off times | 1 / 0 |
| Temporary on / off colours | Green / Black |
| Temporary timer | 20 (2 seconds) |
| Permanent control | Set permanent state |
| Permanent on / off colours | Black / Black |
| Permanent on / off times | 1 / 0 |

The LED should light for two seconds and then go off. The stock console does not implement a credential allowlist or automatically send a grant indication for each authenticated card. A controller/application must decide when to send this command.

## Diagnostics and verification

USB `f` reports logical states, physical readback, command counters and read-beep requests. While the LED is lit, expect `LED_PC13=1 pin=0`; while the buzzer output is HIGH, expect `buzzer_PB0=1 buzzer_pin=1 buzzer_enabled=1`.

`read_beeps` counts requested read beeps, not a microphone measurement. Physical buzzer tests received secure ACKs, verified PB0 HIGH/LOW and automatic expiry, then sent explicit Off. The user confirmed hearing the two beeps. The user also visually confirmed the earlier on/off/timed LED sequence. See [Verification](verification.md) and the [sanitized excerpts](evidence/bench-summary.txt).

The feedback state tests cover authorization, whole-LED-batch validation, timer expiry, wraparound, controller-Off precedence, no automatic LED grant, disconnected clearing and rejection while the buzzer is disabled. The MCU GPIO electrical load is outside host-test coverage.
