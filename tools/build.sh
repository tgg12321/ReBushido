#!/bin/bash
# Build inside WSL and check the SHA1:  wsl bash tools/build.sh [make args]
#   wsl bash tools/build.sh test [-v] [NAME...]   the tests (tests/run.py),
#   with their own output and exit code
#   wsl bash tools/build.sh checks                every check CI runs, and a
#   summary (tools/checks.sh)
# Exits with make's status: a build that fails is not hidden by the filter.
cd "$(dirname "$0")/.." || exit 1
if [ "$1" = test ]; then
    shift
    exec python3 tests/run.py "$@"
fi
if [ "$1" = checks ]; then
    exec bash tools/checks.sh
fi
source .venv/bin/activate
make -j8 "$@" 2>&1 | grep -v "^mipsel-linux-gnu-\|Warning: missing .end\|Assembler messages\|macro invoked" | tail -20
exit "${PIPESTATUS[0]}"
