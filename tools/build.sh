#!/bin/bash
# Build inside WSL and check the SHA1:  wsl bash tools/build.sh [make args]
#   wsl bash tools/build.sh test [-v] [NAME...]   the tests (tests/run.py),
#   with their own output and exit code
#   wsl bash tools/build.sh checks                every check CI runs, and a
#   summary (tools/checks.sh)
#   wsl bash tools/build.sh rebushido             the ReBushido build, into
#   build/rebushido/ (docs/REBUSHIDO.md); rebushido-disc, rebushido-test and
#   rebushido-modern are make's targets too
#   wsl bash tools/build.sh quickboot QB_P1=ROSTER_TSUBAME ...   a build that
#   starts one fight; quickboot-disc for its disc image
#   wsl bash tools/build.sh playtest [NAME...]    the play tests in an emulator
#   with no window (tests/play/README.md): builds their discs, then runs them;
#   a VARIABLE=value among the names is given to make (RB_RELEASE=1)
#   wsl bash tools/build.sh port                  the native port, into
#   build/port/ (docs/PORT.md): a program for this host, which reads the
#   game from your image of the disc
#   wsl bash tools/build.sh rebushido-patch [RB_RELEASE=1]   a release's
#   patch, into build/release/ (docs/REBUSHIDO.md, "Making a release")
# Exits with make's status: a build that fails is not hidden by the filter.
cd "$(dirname "$0")/.." || exit 1
NOISE="^mipsel-linux-gnu-\\|Warning: missing .end\\|Assembler messages\\|macro invoked"
if [ "$1" = test ]; then
    shift
    exec python3 tests/run.py "$@"
fi
if [ "$1" = checks ]; then
    exec bash tools/checks.sh
fi
source .venv/bin/activate
if [ "$1" = playtest ]; then
    shift
    settings=()
    names=()
    for word in "$@"; do
        case "$word" in
            *=*) settings+=("$word") ;;
            *) names+=("$word") ;;
        esac
    done
    make -j8 playtest-discs "${settings[@]}" 2>&1 | grep -v "$NOISE" | tail -4
    [ "${PIPESTATUS[0]}" = 0 ] || exit 1
    exec python3 tools/playtest.py "${names[@]}"
fi
make -j8 "$@" 2>&1 | grep -v "$NOISE" | tail -20
exit "${PIPESTATUS[0]}"
