#!/bin/bash
# Build the project's compiler, GCC 2.7.2's cc1 for MIPS, from source.
#
#   bash tools/build_cc1.sh [dir]        (dir defaults to tools/gcc-2.7.2)
#
# Clones decompals/mips-gcc-2.7.2 at the pinned commit into <dir>, applies
# tools/cc1-reorg-negate-rtx-decl.patch (a missing declaration that makes cc1
# crash on a 64-bit host; it changes no generated code) and builds
# <dir>/build/cc1, the path the Makefile uses. What is pinned and why the
# flags are what they are: docs/COMPILER.md.
#
# Does nothing if <dir>/build/cc1 is already there. Needs git, make and a host
# gcc; about two minutes.
set -euo pipefail

PIN=43d1cdb67ed135879869b5266f01efaaada5e35a
URL=https://github.com/decompals/mips-gcc-2.7.2.git

cd "$(dirname "$0")/.."
PATCH="$PWD/tools/cc1-reorg-negate-rtx-decl.patch"
DIR="${1:-tools/gcc-2.7.2}"

if [ -x "$DIR/build/cc1" ]; then
    echo "build_cc1: $DIR/build/cc1 is already built"
    exit 0
fi
if [ -e "$DIR" ]; then
    echo "build_cc1: $DIR exists but has no build/cc1; move it away and run again" >&2
    exit 1
fi

git clone -q "$URL" "$DIR"
cd "$DIR"
git checkout -q "$PIN"
git apply "$PATCH"

# The 1995 configure script only knows 32-bit x86 hosts by name; the host
# compiler it then uses is whatever `cc` is. (In a path with a space in it
# the script cannot set up the C++ subdirectory and says so in build.log;
# cc1 does not need it.)
./configure --target=mips-mips-gnu --with-gnu-as \
    --host=i386-pc-linux --build=i386-pc-linux >build.log 2>&1 ||
    { tail -20 build.log >&2; exit 1; }

# The parsers are checked in generated; modern bison and gperf cannot
# regenerate them, so make must see them as newer than their sources.
touch c-parse.c c-parse.h c-gperf.h

# Unoptimised, except combine.c: this is the build the project's code was
# matched with. -fgnu89-inline: c-gperf.h has an `__inline` function with no
# `static`, which does not link under the C99 rule.
# The host compiler's temporary files go to /dev/shm where there is one: under
# WSL they have been seen to vanish from /tmp mid-build.
[ -w /dev/shm ] && export TMPDIR=/dev/shm
make combine.o CFLAGS="-O -g -fgnu89-inline" >>build.log 2>&1 ||
    { tail -20 build.log >&2; exit 1; }
make cc1 CFLAGS="-g -fgnu89-inline" >>build.log 2>&1 ||
    { tail -20 build.log >&2; exit 1; }

mkdir -p build
cp cc1 build/cc1
echo "build_cc1: built $DIR/build/cc1"
