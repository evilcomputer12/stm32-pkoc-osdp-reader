#!/usr/bin/env python3
"""Bench wiring probe. Stop ACUConsole/Interop before running this script.

Uses explicit USB 't' to transmit a public test string, then sends a checksum
OSDP POLL in the opposite direction. This does not test Secure Channel.
"""
import argparse
import time
import serial
from serial.tools import list_ports

PATTERN = b'STM32 RS485 TEST\r\n'

def unique_port(vid, pid):
    matches = [p.device for p in list_ports.comports() if (p.vid, p.pid) == (vid, pid)]
    if len(matches) != 1:
        raise SystemExit(f'Expected one {vid:04x}:{pid:04x} device; specify its port explicitly')
    return matches[0]


def usb_command(usb, command):
    usb.reset_input_buffer()
    usb.write(command.encode())
    response = bytearray()
    deadline = time.monotonic() + 2
    while time.monotonic() < deadline:
        block = usb.read(512)
        if block:
            response += block
        elif response:
            break
    print(response.decode(errors='replace').strip() or 'USB: no response')
    return bytes(response)


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--rs485')
parser.add_argument('--usb')
parser.add_argument('--echo', action='store_true', help='Capture TX echo; requires /RE on GND and DE on A8')
args = parser.parse_args()
usb_path = args.usb or unique_port(0x0483, 0x5740)
rs485_path = args.rs485 or unique_port(0x1a86, 0x7523)
print(f'USB={usb_path} RS485={rs485_path} baud=9600')
with serial.Serial(usb_path, 115200, timeout=.1, write_timeout=2) as usb:
    usb.dtr = True
    time.sleep(.2)
    if b'pong' not in usb_command(usb, 'p'):
        raise SystemExit('Reader is not ready')
    usb_command(usb, 'l')
    usb_command(usb, 'i')
    with serial.Serial(rs485_path, 9600, timeout=.5, write_timeout=2) as bus:
        for rts, dtr in ((False, False), (True, False), (False, True), (True, True)):
            bus.rts, bus.dtr = rts, dtr
            time.sleep(.1)
            bus.reset_input_buffer()
            print(f'RTS={int(rts)} DTR={int(dtr)}')
            response = usb_command(usb, 'e' if args.echo else 't')
            if f'bytes={len(PATTERN)} '.encode() not in response:
                raise SystemExit('TX probe was refused or failed; stop the controller first')
            received = bus.read(256)
            print(f'MCU->adapter bytes={len(received)} match={received == PATTERN} hex={received.hex()}')
            bus.reset_input_buffer()
            for _ in range(3):
                bus.write(bytes.fromhex('ff53000700006046'))
                bus.flush()
                time.sleep(.1)
            received = bus.read(256)
            print(f'POLL response bytes={len(received)} hex={received.hex()}')
            usb_command(usb, 'l')
            usb_command(usb, 'i')
