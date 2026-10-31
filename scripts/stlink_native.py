"""Native Homebrew OpenOCD 0.12.0 has a working reset path for this probe.

The bundled 0.12 development build verified flash but failed during reset in
both dapdirect and HLA modes. Keep upload_protocol/debug_tool=stlink and use
the observed working native executable. No package-cache modification.
"""
from pathlib import Path
import subprocess
Import("env")
openocd = next((str(p) for p in (Path('/opt/homebrew/bin/openocd'),
                               Path('/usr/local/bin/openocd')) if p.is_file()), None)
if openocd is None:
    raise RuntimeError("Install OpenOCD with: brew install open-ocd")

def upload(source, target, env):
    # Pass an argv list directly: Tcl commands and workspace paths contain
    # spaces, and must not be split a second time by SCons/shell expansion.
    firmware = Path(str(source[0])).resolve()
    return subprocess.call([
        openocd, '-f', 'interface/stlink.cfg', '-c', 'transport select hla_swd',
        '-f', 'target/stm32f1x.cfg', '-c', 'adapter speed 1000',
        '-c', 'program {' + str(firmware) + '} verify reset exit',
    ])

env.Replace(UPLOADCMD=upload)
