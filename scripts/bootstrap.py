#!/usr/bin/env python3
"""Fetch exact reviewed sources and prepare the local bench/controller."""
import argparse
import base64
import json
import os
from pathlib import Path
import secrets
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
def run(*args):
    subprocess.run(args, cwd=ROOT, check=True)

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--skip-build', action='store_true')
    parser.add_argument('--port', help='RS-485 port for new controller settings; otherwise discover a single WCH port')
    args = parser.parse_args()
    for command in ('git', 'pio', 'dotnet', 'openocd', 'cmake'):
        if not shutil.which(command):
            raise SystemExit('Missing '+command+'. On this Mac: brew install platformio open-ocd stlink dotnet cmake')
    deps = ROOT / '.deps'; deps.mkdir(exist_ok=True)
    for name, info in json.loads((ROOT/'dependencies.lock.json').read_text()).items():
        path = deps/name
        if not path.exists():
            run('git','init',str(path))
            run('git','-C',str(path),'remote','add','origin',info['url'])
            run('git','-C',str(path),'fetch','--depth','1','origin',info['commit'])
            run('git','-C',str(path),'checkout','--detach','FETCH_HEAD')
        actual = subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
        dirty = subprocess.check_output(['git','-C',str(path),'diff','--name-only','HEAD'],text=True).strip()
        if actual != info['commit'] or dirty:
            raise SystemExit(f'{name}: source differs from lock; review it before replacing anything')
        print(name,actual)
    config = ROOT/'config'; config.mkdir(exist_ok=True)
    keyfile = config/'bench-scbk.hex'
    if not keyfile.exists():
        descriptor = os.open(keyfile,os.O_WRONLY|os.O_CREAT|os.O_EXCL,0o600)
        with os.fdopen(descriptor,'w') as file:
            file.write(secrets.token_hex(16)+'\n')
    key = bytes.fromhex(keyfile.read_text().strip())
    if len(key) != 16: raise SystemExit('Existing bench SCBK must contain exactly 16 bytes')
    header = config/'bench_key.h'
    header.write_text('#pragma once\n#include <stdint.h>\nstatic const uint8_t bench_scbk[16] = {'+
                      ','.join('0x%02x'%x for x in key)+'};\n')
    header.chmod(0o600); keyfile.chmod(0o600)
    controller = ROOT/'tools/acuconsole'; controller.mkdir(parents=True,exist_ok=True)
    settingsfile = controller/'appsettings.config'
    if not settingsfile.exists():
        port = args.port
        if not port:
            candidates = sorted(Path('/dev').glob('cu.wchusbserial*'))
            if len(candidates) != 1:
                raise SystemExit('Connect one WCH RS-485 adapter or specify --port for new controller settings')
            port = str(candidates[0])
        settings = {'SerialConnectionSettings': {'PortName': port, 'BaudRate':9600,'ReplyTimeout':200},
                    'Devices':[{'Name':'STM32 C8 PKOC reader','Address':0,'UseSecureChannel':True,'UseCrc':True,
                                'SecureChannelKey':base64.b64encode(key).decode()}],
                    'PollingInterval':200,'IsTracing':True}
        settingsfile.write_text(json.dumps(settings,indent=2)+'\n')
        settingsfile.chmod(0o600)
    if not args.skip_build:
        run('dotnet','publish',str(deps/'osdp-net/src/ACUConsole/ACUConsole.csproj'),'-c','Release',
            '-f','net10.0','-r','osx-arm64','--self-contained','false','-o',str(controller))
        run('dotnet','build','tools/interop/Interop.csproj','-c','Release')
        run('pio','run','-e','c8_reader_hsu')
    print('Dependencies and bench configuration ready. PN532 HSU switches must both be OFF. Flash with pio run -e c8_reader_hsu -t upload.')
if __name__ == '__main__': main()
