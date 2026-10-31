#!/usr/bin/env python3
"""USB diagnostics only; does not open the RS-485 controller port."""
import argparse
import time
import serial
from serial.tools import list_ports
port_errors = (serial.SerialException, OSError, IndexError)
try:
    import termios
    port_errors += (termios.error,)
except ImportError:
    pass

parser=argparse.ArgumentParser()
parser.add_argument('--port')
parser.add_argument('commands',nargs='?',default='ian')
args=parser.parse_args()
deadline=time.monotonic()+40
while time.monotonic()<deadline:
    ports=[p.device for p in list_ports.comports() if (p.vid,p.pid)==(0x0483,0x5740)]
    if not args.port and len(ports)>1: raise SystemExit('Specify --port: more than one STM32 CDC device')
    device=None
    try:
        device=serial.Serial(args.port or ports[0],115200,timeout=.25)
        device.dtr=True
        time.sleep(.2);device.reset_input_buffer()
        device.write(b'p')
        pong_deadline=min(deadline,time.monotonic()+3)
        connected=False
        while time.monotonic()<pong_deadline:
            if b'pong' in device.readline():
                connected=True
                break
        if connected: break
    except port_errors:
        # A reset may leave a stale macOS device node briefly. Re-enumerate;
        # only the harmless ping is retried, never a requested command.
        pass
    if device is not None:
        try: device.close()
        except port_errors: pass
    time.sleep(.25)
else:
    raise SystemExit('STM32 USB CDC did not answer pong. Check its data cable and firmware startup.')
with device:
    for command in args.commands:
        device.write(command.encode());time.sleep(.2)
        deadline=time.monotonic()+3
        response=bytearray()
        while time.monotonic()<deadline:
            block=device.read(1024)
            if block: response+=block
            elif response: break
        print(response.decode(errors='replace'),end='')
