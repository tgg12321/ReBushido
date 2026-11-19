#!/bin/bash
# The build's copy of the checkout on WSL's own disk (BUILD.md, "Where the
# build runs"). A checkout on the Windows drive (/mnt/c/...) builds three to
# five times slower than one on WSL's ext4: every compile reads its source
# and headers through the drive's file server. So tools/build.sh and
# tools/gate.py build in a copy of the sources under
# ~/.cache/rebushido-build/<checkout>/ and bring build/ back:
#
#   bash tools/mirror.sh run COMMAND...   copy the sources in, run COMMAND
#                                         there, bring build/ back; exits
#                                         with COMMAND's status
#   bash tools/mirror.sh in               only copy the sources in, and
#                                         print the copy's path
#   bash tools/mirror.sh out              only bring build/ back
#   bash tools/mirror.sh path             the copy's path
#
# Only the sources go in (rsync: what changed since the last time, deleted
# files deleted); build/ is the copy's own and comes back by what changed
# in it, never deleting anything here. The disc image, the virtualenv and
# the built compiler are linked, not copied. The commit's hash, which the
# copy has no .git to tell, is given to the build by RB_GIT_HASH.
#
# Off with RB_BUILD_IN_PLACE=1, and where the checkout is not on /mnt/ or
# rsync is missing: then COMMAND runs here, as it always did.
set -o pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)

in_place() {
    [ "${RB_BUILD_IN_PLACE:-0}" = 1 ] && return 0
    case "$ROOT" in /mnt/*) ;; *) return 0 ;; esac
    command -v rsync > /dev/null || return 0
    command -v flock > /dev/null || return 0
    return 1
}

# One copy per checkout: its folder's name and a hash of its path.
MIRROR="${RB_MIRROR_DIR:-$HOME/.cache/rebushido-build}/$(basename "$ROOT")-$(printf '%s' "$ROOT" | sha1sum | cut -c1-10)"
LOCK="$MIRROR.lock"

git_hash() {
    local h
    h=$(git -C "$ROOT" rev-parse --short HEAD 2> /dev/null) || h=
    if [ -n "$h" ] && ! git -C "$ROOT" diff --quiet HEAD 2> /dev/null; then
        h=$h-dirty
    fi
    echo "${h:-unknown}"
}

sync_in() {
    mkdir -p "$MIRROR"
    (
        flock 9
        # the sources: everything but what is built, scratch, git and what
        # is linked below. What git ignores is neither copied nor deleted
        # there: the overlay's movovl/build/, and whatever else a build
        # writes beside its sources, stays the copy's own.
        rsync -a --delete --filter=':- .gitignore' \
            --exclude=/build --exclude=/tmp --exclude=/.git --exclude=/.venv \
            --exclude=/.claude --exclude=/tools/gcc-2.7.2 --exclude=/tools/sdl2-mingw \
            --exclude='/*.bin' --exclude='/*.cue' --exclude='/*.iso' \
            --exclude='__pycache__' \
            "$ROOT/" "$MIRROR/" || exit 1
        for link in .venv tools/sdl2-mingw; do
            if [ -e "$ROOT/$link" ] && [ ! -L "$MIRROR/$link" ]; then
                ln -sfn "$ROOT/$link" "$MIRROR/$link"
            fi
        done
        # the compiler is run thousands of times: a copy, not a link
        if [ -f "$ROOT/tools/gcc-2.7.2/build/cc1" ]; then
            mkdir -p "$MIRROR/tools/gcc-2.7.2/build"
            rsync -a "$ROOT/tools/gcc-2.7.2/build/cc1" "$MIRROR/tools/gcc-2.7.2/build/cc1" || exit 1
        fi
        # the play tests' discs, images as large as the disc, are made where
        # the emulator reads them: copying them back would cost more than
        # it saves
        mkdir -p "$ROOT/build/play" "$MIRROR/build"
        [ -L "$MIRROR/build/play" ] || { rm -rf "${MIRROR:?}/build/play"; ln -s "$ROOT/build/play" "$MIRROR/build/play"; }
        # your image of the disc, at the root as the build expects it
        for image in "$ROOT"/*.cue "$ROOT"/*.bin; do
            [ -e "$image" ] || continue
            [ -L "$MIRROR/$(basename "$image")" ] || ln -sfn "$image" "$MIRROR/$(basename "$image")"
        done
        for link in "$MIRROR"/*.cue "$MIRROR"/*.bin; do
            [ -L "$link" ] && [ ! -e "$link" ] && rm -f "$link"
        done
        exit 0
    ) 9> "$LOCK"
}

sync_out() {
    [ -d "$MIRROR/build" ] || return 0
    (
        flock 9
        # what the copy built, newer than what is here; nothing deleted. Not
        # the compilers' objects, lists of inputs and dumps, which only the
        # build reads: thousands of small files, which take seconds only to
        # look at on the Windows drive. Nor the folders that runs in place
        # write (IN_PLACE of tools/gate.py; tools/build.sh runs those in
        # place too): a copy's old build/rollback/ brought back while
        # port-rollback ran here replaced the files it had just written
        # (docs/NETPLAY.md, "What failed in the gate").
        rsync -a --update --prune-empty-dirs \
            --exclude='*.o' --exclude='*.d' --exclude='*.gimple' --exclude='*.ssa' --exclude=/play \
            --exclude=/rollback --exclude=/stage-select --exclude=/net --exclude=/budget --exclude=/bench --exclude=/gate \
            "$MIRROR/build/" "$ROOT/build/"
        status=$?
        # 24: files that vanished while it copied, the temporary files of
        # a build still running beside (the gate's steps); that build
        # brings back its own when it ends
        [ $status = 24 ] && status=0
        exit $status
    ) 9> "$LOCK"
}

case "$1" in
    path)
        in_place && echo "$ROOT" || echo "$MIRROR"
        ;;
    in)
        if in_place; then echo "$ROOT"; exit 0; fi
        sync_in || { echo "mirror: could not copy the sources to $MIRROR" >&2; exit 1; }
        echo "$MIRROR"
        ;;
    out)
        in_place || sync_out
        ;;
    run)
        shift
        # a release asks git whether the tree is clean: the copy has no git
        case " $* " in *" RB_RELEASE=1 "*) RB_RELEASE=1 ;; esac
        # RB_IN_MIRROR: where it runs is decided (tools/build.sh asks)
        export RB_IN_MIRROR=1
        if in_place || [ "${RB_RELEASE:-0}" = 1 ]; then
            cd "$ROOT" && exec "$@"
        fi
        sync_in || { echo "mirror: could not copy the sources to $MIRROR" >&2; exit 1; }
        export RB_GIT_HASH="${RB_GIT_HASH:-$(git_hash)}"
        (cd "$MIRROR" && "$@")
        status=$?
        sync_out || { echo "mirror: could not bring build/ back from $MIRROR" >&2; [ $status = 0 ] && status=1; }
        # what is cleaned there is cleaned here too, which nothing brings back
        case " $* " in *" clean "*) (cd "$ROOT" && make clean > /dev/null) ;; esac
        exit $status
        ;;
    *)
        echo "usage: bash tools/mirror.sh run COMMAND... | in | out | path" >&2
        exit 2
        ;;
esac
