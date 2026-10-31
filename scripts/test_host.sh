#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
cmake -S .deps/asymcred -B .build-host/asymcred -DASYMCRED_SANITIZE=ON
cmake --build .build-host/asymcred -j 6
ctest --test-dir .build-host/asymcred --output-on-failure
cmake -S .deps/osdp-embedded -B .build-host/osdp -DOSDP_SANITIZE=ON
cmake --build .build-host/osdp -j 6
ctest --test-dir .build-host/osdp --output-on-failure
cmake -S tests -B .build-host/reader
cmake --build .build-host/reader -j 6
ctest --test-dir .build-host/reader --output-on-failure
if [ ! -x .build-host/venv/bin/python ]; then
    python3 -m venv .build-host/venv
fi
.build-host/venv/bin/pip install 'cryptography==47.0.0'
.build-host/venv/bin/python tests/verify_independent.py
