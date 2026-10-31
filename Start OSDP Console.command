#!/bin/zsh
set -e
cd "${0:A:h}"
export PATH="/opt/homebrew/bin:$PATH"
exec python3 scripts/start_console.py
