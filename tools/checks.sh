#!/bin/bash
# Every check GitHub Actions runs (.github/workflows/build.yml), in its order,
# and a summary:  bash tools/checks.sh   (make checks; from Windows,
# wsl bash tools/build.sh checks). About a minute and a half.
#
# A check that fails does not stop the ones after it. What each printed is in
# build/checks/<name>.log; its last lines are shown when it fails. Exits 1 if
# any failed. Keep the list the same as the workflow's.
cd "$(dirname "$0")/.." || exit 1
[ -f .venv/bin/activate ] && source .venv/bin/activate
mkdir -p build/checks

names=()
results=()
failed=0

check() {
    local name=$1 start=$SECONDS result=ok
    shift
    printf '%-8s %s ... ' "$name" "$*"
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

echo
for i in "${!names[@]}"; do
    printf '  %-8s %s\n' "${names[$i]}" "${results[$i]}"
done
if [ "$failed" = 1 ]; then
    echo "FAILED: see build/checks/<name>.log"
    exit 1
fi
echo "OK: all ${#names[@]} checks pass"
