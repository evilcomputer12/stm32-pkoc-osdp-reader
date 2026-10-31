# Hardware and wiring

The operating target is **STM32F103C8T6**, not the earlier C6. Its documented limits are 64 KiB Flash and 20 KiB SRAM. The build enforces those limits even though this ST-Link reports 128 KiB Flash. The clock configuration is 8 MHz HSE → 72 MHz CPU / 48 MHz USB.

[Purchase references and the no-sponsorship statement](../README.md#hardware-and-where-i-bought-it) are in the README. The links identify the owner's purchases; they do not substitute for inspecting another module's labels, circuit or voltage requirements.

## Connections used on the working bench

Disconnect all supplies before rewiring. Board labels A9/B10 mean MCU pins PA9/PB10.

| Module signal | STM32 / other connection |
|---|---|
| ATECC SCL | PB6, I2C1 clock |
| ATECC SDA | PB7, I2C1 data |
| ATECC VCC / GND | 3.3 V / common GND |
| PN532 SCL/RX | PB10, USART3 TX |
| PN532 SDA/TX | PB11, USART3 RX |
| PN532 VCC / GND | USB-powered board 5 V / common GND, on the tested Elechouse VCC breakout |
| PN532 mode | Switch 1 OFF, switch 2 OFF: HSU, 115200 baud |
| RS-485 DI | PA9, USART1 TX |
| RS-485 RO | PA10, USART1 RX, floating digital input |
| RS-485 DE and /RE | Joined together to PA8 |
| RS-485 VCC / GND | 5 V / common GND on the tested module |
| RS-485 A and B | Controller USB-to-RS-485 differential pair |
| HW-512 S / + / − | PB0 / 3.3 V / GND |
| ST-Link SWDIO / SWCLK | PA13 / PA14, plus GND and target-voltage connection appropriate to the probe |
| Native USB | Board USB connector (PA11/PA12), data cable to the Mac |
| Onboard LED | PC13, already connected internally, active LOW |

ATECC and PN532 no longer share an I2C bus. The PN532's SCL/SDA markings are reused as RX/TX in HSU mode; this is why the wires stay on the same module header while its mode changes. Do not swap TX/RX blindly: MCU TX must reach PN532 RX.

The final RS-485 wiring has **DE and /RE both on A8**. A8 LOW receives; A8 HIGH transmits. Earlier loopback configurations (/RE tied to GND or A9 joined to A10 with RO disconnected) were temporary diagnostics and must not remain in the operating reader.

## Power and signal details

- ATECC remains at 3.3 V. PN532 VCC is 5 V only for the tested Elechouse-style breakout with a VCC input; do not apply that rule to a pin explicitly marked 3.3 V or to a bare PN532 IC.
- The MAX485-style module is powered at 5 V. PA10 is used as a five-volt-tolerant digital RX input without internal pulls. Keep the STM32 powered while another powered module drives its pins; five-volt tolerance does not apply to an unpowered chip or its supply pins.
- The tested PN532 UART uses the breakout's logic interface. Check level shifting before substituting a module with different UART voltage levels.
- Use common GND among reader, transceiver and controller adapter. Inspect the board's supply path before mixing USB and external supplies; do not connect competing 5 V sources together.
- A/B labels differ between vendors. Verify polarity and termination for the actual pair. Inspect existing termination/bias resistors instead of adding termination indiscriminately.
- The onboard LED is driven as a slow open-drain output on PC13. It requires no new wiring.

The MCU reference is the [ST STM32F103x8/xB datasheet](https://www.st.com/resource/en/datasheet/stm32f103c8.pdf). PN532 command/interface behavior is described by the [NXP user manual](https://www.nxp.com/docs/en/user-guide/141520.pdf). Board-level power arrangements must still be checked on the particular breakout.

## Buzzer qualification

The user confirmed S→PB0, +→3.3 V, −→GND and previous direct operation on the same Blue Pill. Two timed switched-DC beeps were then heard with this firmware. PB0 is active HIGH; PWM was unnecessary for this tested module.

The module reportedly has no separate transistor. Its S-pin load current and internal circuit have not been measured. Audible success is a functional test, not an electrical load qualification. ST specifies 8 mA at normal GPIO output levels, 20 mA with reduced voltage margin, and 25 mA as an absolute maximum. A suitable low-current input/load may be driven directly; a higher-current buzzer needs a driver. The `+` mark alone does not distinguish active and passive buzzers, and other HW-512 variants may differ.

The current `c8_reader_hsu` build explicitly enables PB0. For a build without a qualified buzzer connection, change its flag in `platformio.ini` to `-D READER_BUZZER_ENABLED=0`, rebuild and flash; PB0 then stays an input and OSDP buzzer commands are rejected. The flag indicates enablement, not a measured current rating or fitted transistor.
