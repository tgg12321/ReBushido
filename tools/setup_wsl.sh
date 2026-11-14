#!/bin/bash
# Set up the build tools on Ubuntu 24.04 (WSL or native), from the repo root:
#   bash tools/setup_wsl.sh
# Installs the MIPS binutils and preprocessor, makes the Python virtualenv the
# build script activates, and builds the compiler (tools/build_cc1.sh).
set -euo pipefail
cd "$(dirname "$0")/.."

echo "=== System packages ==="
sudo apt-get update
sudo apt-get install -y     build-essential     binutils-mipsel-linux-gnu     cpp-mipsel-linux-gnu     python3     python3-venv     git

echo "=== Python virtualenv ==="
[ -d .venv ] || python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt

echo "=== Compiler (GCC 2.7.2 cc1) ==="
bash tools/build_cc1.sh

echo "=== Check ==="
mipsel-linux-gnu-as --version | head -1
mipsel-linux-gnu-ld --version | head -1
ls -l tools/gcc-2.7.2/build/cc1

echo
echo "Done. Build and check with:  bash tools/build.sh check"
