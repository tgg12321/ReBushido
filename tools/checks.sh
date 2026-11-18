#!/bin/bash
# Every check GitHub Actions runs (.github/workflows/build.yml), in its order,
# and a summary:  bash tools/checks.sh   (make checks; from Windows,
# wsl bash tools/build.sh checks). About three minutes.
#
# The first seven are the matching build's, with every switch of ReBushido
# off: the SHA1 among them is what shows that no change of ReBushido's
# reaches the original. The next three are the ReBushido build's
# (docs/REBUSHIDO.md), and the last is the native port's link (docs/PORT.md).
#
# A check that fails does not stop the ones after it. What each printed is in
# build/checks/<name>.log; its last lines are shown when it fails. Exits 1 if
# any failed. Keep the list the same as the workflow's. tools/gate.py reads
# the `check NAME COMMAND` lines below and runs them side by side: keep each
# on one line, and give a new one its place in gate.py's WAITS.
cd "$(dirname "$0")/.." || exit 1
[ -f .venv/bin/activate ] && source .venv/bin/activate
mkdir -p build/checks

names=()
results=()
failed=0

check() {
    local name=$1 start=$SECONDS result=ok
    shift
    printf '%-10s %s ... ' "$name" "$*"
    if ! "$@" > "build/checks/$name.log" 2>&1; then
        result=FAILED
        failed=1
    fi
    echo "$result ($((SECONDS - start))s)"
    if [ "$result" = FAILED ]; then
        tail -n 25 "build/checks/$name.log" | sed 's/^/    | /'
    fi
    names+=("$name")
    results+=("$result")
}

# These four need no build of the game.
check counts  python3 tools/count.py --check
check lint    python3 tools/lint.py --check
check modern  make modern
check test    make test
# The bytes, then the relink, which reads the objects the build made.
check game    make -j8 check
check overlay make movovl
check shift   make shift
# ReBushido: it compiles, links and fits (and quick boot's files compile
# with their switch on); then its tests and the modern compiler with the
# master switch on, which need no build. The play tests (make playtest)
# are not a check: they need the retail disc and an emulator.
check rb-build  make -j8 rebushido
check rb-test   make rebushido-test
check rb-modern make rebushido-modern
# The native port: the game's C and src/port/ link into a program for this
# host with no name missing, the port's own files with no warning, with
# 64-bit pointers and with 32-bit ones; no structure laid out otherwise in
# one of them or on the PlayStation that config/port_layout.txt does not
# name (tools/port_layout.py); no pointer of the 64-bit program made of a
# number that is not a 32-bit address (tools/port_casts.py); and no place
# where the order of evaluation may matter that has not been looked at
# (tools/port_order.py). (Its runs against the emulator are play tests,
# port_game and four more.)
check port      make port

echo
for i in "${!names[@]}"; do
    printf '  %-10s %s\n' "${names[$i]}" "${results[$i]}"
done
if [ "$failed" = 1 ]; then
    echo "FAILED: see build/checks/<name>.log"
    exit 1
fi
echo "OK: all ${#names[@]} checks pass"
