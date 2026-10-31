#!/usr/bin/env python3
"""Launch the official console with the current, uniquely detected WCH port."""
import json
import os
from pathlib import Path
import shutil
import sys

root = Path(__file__).resolve().parent.parent
folder = root / 'tools/acuconsole'
ports = sorted(Path('/dev').glob('cu.wchusbserial*'))
if len(ports) != 1:
    sys.exit('Connect exactly one WCH USB-RS485 adapter before starting the console.')
dotnet = shutil.which('dotnet')
if not dotnet or not (folder/'ACUConsole.dll').is_file():
    sys.exit('Run python3 scripts/bootstrap.py first to build the console.')
settings_file = folder/'appsettings.config'
settings = json.loads(settings_file.read_text())
settings['SerialConnectionSettings']['PortName'] = str(ports[0])
settings_file.write_text(json.dumps(settings, indent=2)+'\n')
settings_file.chmod(0o600)
print(f'OSDP.Net: {ports[0]}, 9600 baud. Select Connections → Start Serial Connection.', flush=True)
os.chdir(folder)
os.execv(dotnet, [dotnet, 'ACUConsole.dll'])
