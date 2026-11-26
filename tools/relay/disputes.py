"""The owner's tool for the ranked ladder's disputes (docs/NETPLAY.md,
section 17; tools/relay/README.md, "Disputes").

A match whose two reports disagree is held disputed by the lobby, unrated,
with the replays of its fights from each side (`disputes/match-N-a.rbu`,
`-b.rbu`). This lists them, plays each side's replays back through the
port's `--replay-check` (which says whether a replay's own hashes hold and,
from the game's state as the playback leaves it, who won each fight), says
which side's report the replays bear out, and on the owner's word settles
the match: rated as the replays show (or as the owner says), or void, the
decision written into the store with its reason, kept as long as the match.

The Python 3 standard library alone (3.8 or newer), as the server is. On the
VPS, as root, beside the running service (SQLite lets the two write the
store one at a time):

    python3 /opt/rebushido-relay/disputes.py list
    python3 /opt/rebushido-relay/disputes.py show 7
    python3 /opt/rebushido-relay/disputes.py export 7 --out /root/dispute-7
    python3 /opt/rebushido-relay/disputes.py resolve 7 --winner replays --verdict /root/verdict-7.json \\
        --reason "both sides' replays show Chie winning 3 to 1"
    python3 /opt/rebushido-relay/disputes.py resolve 7 --void --reason "neither side's replays play back"

The replays are played by the port, which needs the disc image: the VPS has
neither, so `export` copies a dispute's two uploads and what the store says
of it into a folder, which is checked on a PC that has both:

    python tools/relay/disputes.py check --from DIR --image GAME.cue --program rebushido-headless.exe \\
        --save verdict-7.json

and `verdict-7.json` copied back for `resolve --verdict`. Where the store and
the port are on one machine, `check 7 --state DIR --image ... --program ...`
reads the store directly, and `resolve 7 --winner replays --image ...
--program ...` checks and settles at once.

`--state` is the ladder's folder (the service's own on the VPS,
/var/lib/private/rebushido-relay, by default). The program must be of the
build the match was played with (the players' setup code says it): a replay
says "not this program's" to any other.
"""

import argparse
import hashlib
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import ladder  # noqa: E402

STATE = "/var/lib/private/rebushido-relay"
REPLAY_MAGIC = b"RBREPLAY"
REPLAY_HEAD = 12
CHUNK_HEAD = 8
WINNER_WORDS = {ladder.WON_FIRST: "first", ladder.WON_SECOND: "second", ladder.WON_DRAW: "draw",
                ladder.WON_NONE: "none"}
SIDES = ("a", "b")
# what --replay-check says (src/port/replay.c, port_ReplayEnd)
HELD = re.compile(r"replay: (.+?): (\d+) frames played, (\d+) hashes the same as recorded"
                  r"(?:, the winner by its state (-?\d+))?")
SAID = re.compile(r"replay: (.+?): (.+)$")
CHECK_SECONDS = 600


# --- what the store says -----------------------------------------------------


def stamp(seconds):
    return time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime(seconds)) if seconds else "-"


def report_of(raw):
    """A side's signed report as the store kept it, in words."""
    if raw is None:
        return None
    r = ladder.parse_rank(raw)
    if r is None:
        return {"unreadable": True}
    return {"reason": ladder.REASON_NAMES.get(r.reason, str(r.reason)), "winner": WINNER_WORDS.get(r.winner, "?"),
            "winner_code": r.winner, "rule": r.rule, "count": r.count, "frames": r.frames,
            "digest": r.digest.hex()}


def describe(store, m, dispute_days=60):
    """What the store holds of a match: its players, why it is disputed,
    both reports, the replays kept of each side."""
    kept = store.replays_of(m["id"])
    out = {"match": m["id"], "created": m["created"], "state": ladder.STATE_NAMES.get(m["state"], "?"),
           "why": m["why"], "count": m["count"], "stage": m["stage"], "players": {}, "reports": {}, "replays": {}}
    for side, column in enumerate(SIDES):
        key = m[column]
        player = store.player(key)
        out["players"][column] = {"name": player["name"] if player else "?", "key": key.hex(),
                                  "rating": round(player["rating"]) if player else None}
        out["reports"][column] = report_of(m[column + "_report"])
        if side in kept:
            path, size, received = kept[side]
            out["replays"][column] = {"file": os.path.basename(path), "bytes": size, "received": received,
                                      "kept_until": received + dispute_days * ladder.SECONDS_A_DAY}
        else:
            out["replays"][column] = None
    return out


def tell(info, settled=None):
    """A match in lines, for the owner."""
    a, b = info["players"]["a"], info["players"]["b"]
    lines = ["match %d, %s: %s (%s, the first, its host) against %s (%s, the second); first to %d, stage %d" %
             (info["match"], stamp(info["created"]), a["name"], a["rating"], b["name"], b["rating"], info["count"],
              info["stage"]),
             "  %s: %s" % (info["state"], info["why"])]
    for side in SIDES:
        name = info["players"][side]["name"]
        r = info["reports"][side]
        if r is None:
            lines.append("  %s's report: none" % name)
        elif r.get("unreadable"):
            lines.append("  %s's report: unreadable" % name)
        else:
            lines.append("  %s's report: %s, winner %s, first to %d, %d frames, digest %s..." %
                         (name, r["reason"], r["winner"], r["count"], r["frames"], r["digest"][:16]))
        kept = info["replays"][side]
        if kept is None:
            lines.append("  %s's replays: none uploaded" % name)
        else:
            lines.append("  %s's replays: %s, %d bytes, kept until %s" %
                         (name, kept["file"], kept["bytes"], stamp(kept["kept_until"])))
    if settled:
        lines.append("  settled %s: %s%s, because: %s" % (
            stamp(settled["decided"]), ladder.STATE_NAMES.get(settled["state"], "?"),
            "" if settled["state"] != ladder.M_RATED else ", the %s won" % WINNER_WORDS.get(settled["winner"]),
            settled["reason"]))
    return lines


# --- a replay, read and played ----------------------------------------------


def read_replay(path):
    """What a replay file says of itself (src/port/replay.c): its META and
    its hashes' bytes, or None where it is no replay."""
    with open(path, "rb") as f:
        data = f.read()
    if len(data) < REPLAY_HEAD or not data.startswith(REPLAY_MAGIC):
        return None
    chunks = {}
    at = REPLAY_HEAD
    while at + CHUNK_HEAD <= len(data):
        tag = data[at:at + 4]
        size = struct.unpack_from("<I", data, at + 4)[0]
        chunks.setdefault(tag, data[at + CHUNK_HEAD:at + CHUNK_HEAD + size])
        at += CHUNK_HEAD + size
        if tag == b"END ":
            break
    meta = {}
    for line in chunks.get(b"META", b"").decode("ascii", "replace").splitlines():
        word, _, value = line.partition(" ")
        meta[word] = value
    return {"meta": meta, "hashes": chunks.get(b"HASH", b"")}


def number(text, default=None):
    try:
        return int(text)
    except (TypeError, ValueError):
        return default


def play(program, image, path, seconds, work):
    """`--replay-check` of one replay: (held, frames, hashes, the state's
    winner, what it said)."""
    command = [sys.executable, program] if program.endswith(".py") else [program]
    command += ["--image", image, "--no-cards", "--replay-check", path]
    env = dict(os.environ)
    env.setdefault("SDL_VIDEODRIVER", "dummy")
    env.setdefault("SDL_AUDIODRIVER", "dummy")
    try:
        done = subprocess.run(command, cwd=work, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                              stderr=subprocess.PIPE, timeout=seconds)
    except subprocess.TimeoutExpired:
        return False, 0, 0, None, "not ended after %d seconds" % seconds
    except OSError as error:
        raise SystemExit("disputes: the program %s does not run: %s" % (program, error))
    text = (done.stdout + done.stderr).decode("utf-8", "replace")
    held = None
    said = None
    for line in text.splitlines():
        found = HELD.search(line)
        if found:
            held = found
        found = SAID.search(line)
        if found:
            said = found.group(2)
    if done.returncode == 0 and held is not None:
        winner = number(held.group(4))
        return True, int(held.group(2)), int(held.group(3)), winner, held.group(0)
    return False, 0, 0, None, said or "ended %s, saying nothing of the replay" % done.returncode


def check_side(upload, program, image, seconds, work, count):
    """One side's upload played back fight by fight: what it shows."""
    side = {"uploaded": upload is not None, "fights": [], "wins": [0, 0], "winner": None, "held": True}
    if upload is None:
        side["held"] = False
        return side
    folder = tempfile.mkdtemp(prefix="unpacked-", dir=work)
    try:
        files = ladder.unpack(upload, folder)
    except (SystemExit, struct.error, OSError, UnicodeError) as error:
        side["held"] = False
        side["trouble"] = "the upload cannot be unpacked: %s" % error
        return side
    fights = []
    for path in files:
        replay = read_replay(path)
        if replay is None:
            side["fights"].append({"file": os.path.basename(path), "held": False, "said": "not a replay"})
            side["held"] = False
            continue
        fights.append((replay["meta"].get("date", ""), replay["meta"].get("start_count", ""), path, replay))
    # in the order they were played
    fights.sort(key=lambda f: (f[0], number(f[1], 0)))
    for _, _, path, replay in fights:
        meta = replay["meta"]
        held, frames, hashes, winner, said = play(program, image, path, seconds, work)
        fight = {"file": os.path.basename(path), "date": meta.get("date", ""), "frames": number(meta.get("frames")),
                 "start_hash": meta.get("start_hash", ""), "recorded_winner": number(meta.get("winner")),
                 "unfinished": meta.get("unfinished", "0") not in ("", "0"), "online": meta.get("online", ""),
                 "hashes_digest": hashlib.sha256(replay["hashes"]).hexdigest(), "held": held,
                 "played_frames": frames, "hashes_held": hashes, "state_winner": winner, "said": said}
        side["fights"].append(fight)
        if not held:
            side["held"] = False
            continue
        if winner in (0, 1) and not fight["unfinished"]:
            side["wins"][winner] += 1
    for player in (0, 1):
        if side["wins"][player] >= count:
            side["winner"] = (ladder.WON_FIRST, ladder.WON_SECOND)[player]
    return side


def judge(info, sides):
    """The replays of both sides against each other and the reports: which
    report they bear out, and what the owner might settle."""
    names = [info["players"][s]["name"] for s in SIDES]
    verdict = {"match": info["match"], "count": info["count"], "names": names, "sides": sides, "reports": {},
               "same_fights": {}}
    # a fight in both uploads (the same start) has the same hashes where both
    # programs played the same game
    starts = {}
    for column in SIDES:
        for fight in sides[column]["fights"]:
            if fight.get("start_hash"):
                starts.setdefault(fight["start_hash"], {})[column] = fight
    both = [f for f in starts.values() if len(f) == 2]
    verdict["same_fights"] = {"in_both": len(both),
                              "alike": sum(1 for f in both if f["a"]["hashes_digest"] == f["b"]["hashes_digest"]),
                              "only_a": sum(1 for f in starts.values() if list(f) == ["a"]),
                              "only_b": sum(1 for f in starts.values() if list(f) == ["b"])}
    shown = {sides[c]["winner"] for c in SIDES if sides[c]["held"] and sides[c]["winner"] is not None}
    for column in SIDES:
        r = info["reports"][column]
        agrees = None
        if len(shown) == 1 and r and not r.get("unreadable"):
            agrees = r["winner_code"] == next(iter(shown))
        verdict["reports"][column] = {"name": info["players"][column]["name"], "report": r, "borne_out": agrees}
    if len(shown) == 1:
        winner = next(iter(shown))
        showing = [c for c in SIDES if sides[c]["held"] and sides[c]["winner"] == winner]
        by = [info["players"][c]["name"] for c in showing]
        wins = sides[showing[0]]["wins"]
        verdict["suggest"] = {"state": "rated", "winner": WINNER_WORDS[winner], "winner_code": winner,
                              "why": "%s's replays play back whole and show %s winning %d to %d" % (
                                  " and ".join(by), names[0 if winner == ladder.WON_FIRST else 1],
                                  max(wins), min(wins))}
    elif len(shown) == 2:
        verdict["suggest"] = {"state": None, "why": "the two sides' replays, each whole, show different winners: "
                                                    "look at them (--replay FILE) before settling"}
    else:
        verdict["suggest"] = {"state": None, "why": "no side's replays play back to a winner of the match (a fight "
                                                    "unfinished, none uploaded, or none holding): void, or settle "
                                                    "by hand"}
    return verdict


def tell_verdict(verdict):
    lines = ["match %d, first to %d: %s (the first) against %s (the second)" %
             (verdict["match"], verdict["count"], verdict["names"][0], verdict["names"][1])]
    for column, name in zip(SIDES, verdict["names"]):
        side = verdict["sides"][column]
        if not side["uploaded"]:
            lines.append("  %s's replays: none uploaded" % name)
            continue
        if side.get("trouble"):
            lines.append("  %s's replays: %s" % (name, side["trouble"]))
        for fight in side["fights"]:
            if fight.get("held"):
                lines.append("  %s's %s: every hash holds (%d frames, %d hashes); by the game's state %s%s%s" % (
                    name, fight["file"], fight["played_frames"], fight["hashes_held"],
                    {0: "the first won", 1: "the second won"}.get(fight["state_winner"], "nobody won"),
                    " (unfinished)" if fight.get("unfinished") else "",
                    "" if fight.get("recorded_winner") == fight["state_winner"] else
                    "; the file says %s" % fight.get("recorded_winner")))
            else:
                lines.append("  %s's %s: DOES NOT HOLD: %s" % (name, fight["file"], fight.get("said")))
        lines.append("  %s's replays show: %d to %d%s" % (
            name, side["wins"][0], side["wins"][1],
            "" if side["winner"] is None else ", the %s won the match" % WINNER_WORDS[side["winner"]]))
    same = verdict["same_fights"]
    lines.append("  fights in both uploads: %d, alike in every hash: %d (only the first's %d, only the second's %d)" %
                 (same["in_both"], same["alike"], same["only_a"], same["only_b"]))
    for column in SIDES:
        r = verdict["reports"][column]
        if r["report"] is None:
            lines.append("  %s reported nothing" % r["name"])
        elif r["borne_out"] is None:
            lines.append("  %s's report (%s, winner %s): the replays do not say" %
                         (r["name"], r["report"].get("reason"), r["report"].get("winner")))
        else:
            lines.append("  %s's report (%s, winner %s): %s by the replays" % (
                r["name"], r["report"]["reason"], r["report"]["winner"],
                "borne out" if r["borne_out"] else "NOT borne out"))
    suggest = verdict["suggest"]
    lines.append("  so: %s" % ("rate it, the %s the winner: %s" % (suggest["winner"], suggest["why"])
                               if suggest["state"] == "rated" else suggest["why"]))
    return lines


# --- the commands --------------------------------------------------------------


def open_store(state):
    return ladder.Store(state, server=False)


def keep_owner(state):
    """Run as root beside the service, SQLite may have made its -wal and
    -shm files as root: given back to the store's own user, whose service
    could not open them otherwise."""
    if not hasattr(os, "geteuid") or os.geteuid() != 0:
        return
    path = os.path.join(state, "ladder.sqlite")
    owner = os.stat(path)
    for suffix in ("-wal", "-shm"):
        other = path + suffix
        try:
            st = os.stat(other)
        except OSError:
            continue
        if (st.st_uid, st.st_gid) != (owner.st_uid, owner.st_gid):
            os.chown(other, owner.st_uid, owner.st_gid)


def command_list(o):
    store = open_store(o.state)
    try:
        disputed = store.disputed()
        print("%d disputed match%s open" % (len(disputed), "" if len(disputed) == 1 else "es"))
        for m in disputed:
            print("\n".join(tell(describe(store, m, o.dispute_days))))
        if o.all:
            settled = store.settled()
            print("%d settled" % len(settled))
            for s in settled:
                m = store.match(s["match"])
                if m is not None:
                    print("\n".join(tell(describe(store, m, o.dispute_days), s)))
    finally:
        store.close()
    return 0


def command_show(o):
    store = open_store(o.state)
    try:
        m = store.match(o.match)
        if m is None:
            raise SystemExit("disputes: no match %d" % o.match)
        info = describe(store, m, o.dispute_days)
        settled = store.settled(o.match)
        if o.json:
            info["settled"] = settled
            print(json.dumps(info, indent=1))
        else:
            print("\n".join(tell(info, settled)))
            if settled and settled["verdict"]:
                try:
                    print("\n".join(tell_verdict(json.loads(settled["verdict"]))))
                except (ValueError, KeyError):
                    print("  what the replays said: " + settled["verdict"][:400])
    finally:
        store.close()
    return 0


def command_export(o):
    store = open_store(o.state)
    try:
        m = store.match(o.match)
        if m is None:
            raise SystemExit("disputes: no match %d" % o.match)
        info = describe(store, m, o.dispute_days)
        kept = store.replays_of(o.match)
    finally:
        store.close()
    if os.path.exists(o.out) and os.listdir(o.out):
        raise SystemExit("disputes: %s is there and not empty" % o.out)
    os.makedirs(o.out, exist_ok=True)
    for side, (path, _, _) in kept.items():
        shutil.copyfile(path, os.path.join(o.out, "%s.rbu" % SIDES[side]))
    with open(os.path.join(o.out, "match.json"), "w", encoding="ascii") as f:
        json.dump(info, f, indent=1)
    print("match %d exported to %s: match.json%s" % (o.match, o.out, "".join(
        ", %s.rbu" % SIDES[side] for side in sorted(kept))))
    if not kept:
        print("(neither side uploaded its replays)")
    return 0


def gather(o):
    """The match's description and its two uploads' paths, from the store or
    from an export's folder."""
    if o.source:
        with open(os.path.join(o.source, "match.json"), encoding="ascii") as f:
            info = json.load(f)
        uploads = {c: os.path.join(o.source, c + ".rbu") for c in SIDES}
        return info, {c: p if os.path.isfile(p) else None for c, p in uploads.items()}
    if o.match is None:
        raise SystemExit("disputes: which match (a number, with --state), or --from an export's folder")
    store = open_store(o.state)
    try:
        m = store.match(o.match)
        if m is None:
            raise SystemExit("disputes: no match %d" % o.match)
        info = describe(store, m, o.dispute_days)
        kept = store.replays_of(o.match)
    finally:
        store.close()
    return info, {c: kept[i][0] if i in kept else None for i, c in enumerate(SIDES)}


def run_check(o, info, uploads):
    if not o.image or not o.program:
        raise SystemExit("disputes: a check plays the replays: --image (the disc's .cue) and --program (the port, "
                         "of the match's build) are needed")
    if not os.path.isfile(o.image) or not os.path.isfile(o.program):
        raise SystemExit("disputes: %s is not there" % (o.image if not os.path.isfile(o.image) else o.program))
    work = tempfile.mkdtemp(prefix="rebushido-dispute-")
    try:
        sides = {c: check_side(uploads[c], os.path.abspath(o.program), os.path.abspath(o.image), o.seconds, work,
                               info["count"]) for c in SIDES}
    finally:
        shutil.rmtree(work, ignore_errors=True)
    return judge(info, sides)


def command_check(o):
    info, uploads = gather(o)
    verdict = run_check(o, info, uploads)
    print("\n".join(tell_verdict(verdict)))
    if o.save:
        with open(o.save, "w", encoding="ascii") as f:
            json.dump(verdict, f, indent=1)
        print("the verdict kept in %s" % o.save)
    return 0


def command_resolve(o):
    if o.void == (o.winner is not None):
        raise SystemExit("disputes: --winner first|second|draw|replays, or --void")
    if not o.reason or not o.reason.strip():
        raise SystemExit("disputes: --reason says why, for the store")
    verdict = None
    if o.verdict:
        with open(o.verdict, encoding="ascii") as f:
            verdict = json.load(f)
        if verdict.get("match") != o.match:
            raise SystemExit("disputes: %s is the verdict of match %s, not %d" % (o.verdict, verdict.get("match"),
                                                                                o.match))
    elif o.image or o.program:
        o.source = None
        info, uploads = gather(o)
        verdict = run_check(o, info, uploads)
        print("\n".join(tell_verdict(verdict)))
    if o.winner == "replays":
        if verdict is None:
            raise SystemExit("disputes: --winner replays needs what the replays said: --verdict FILE, or --image "
                             "and --program to check them now")
        if verdict["suggest"]["state"] != "rated":
            raise SystemExit("disputes: the replays name no winner (%s): --winner first|second|draw or --void" %
                             verdict["suggest"]["why"])
        winner = verdict["suggest"]["winner_code"]
    elif o.winner is not None:
        winner = {"first": ladder.WON_FIRST, "second": ladder.WON_SECOND, "draw": ladder.WON_DRAW}[o.winner]
    else:
        winner = ladder.WON_NONE
    state = ladder.M_VOID if o.void else ladder.M_RATED
    store = open_store(o.state)
    try:
        m = store.match(o.match)
        if m is None:
            raise SystemExit("disputes: no match %d" % o.match)
        info = describe(store, m, o.dispute_days)
        print("\n".join(tell(info, store.settled(o.match))))
        if m["state"] != ladder.M_DISPUTED:
            raise SystemExit("disputes: match %d is %s, not disputed: nothing to settle" %
                             (o.match, ladder.STATE_NAMES.get(m["state"], "?")))
        names = (info["players"]["a"]["name"], info["players"]["b"]["name"])
        if state == ladder.M_RATED:
            now = int(time.time())
            ratings = store.rate(m["a"], m["b"], winner, now, o.tau)
            plan = "rated, %s: %s %d to %d, %s %d to %d" % (
                {ladder.WON_FIRST: names[0] + " won", ladder.WON_SECOND: names[1] + " won",
                 ladder.WON_DRAW: "a draw"}[winner],
                names[0], round(store.player(m["a"])["rating"]), round(ratings[m["a"]][0]),
                names[1], round(store.player(m["b"])["rating"]), round(ratings[m["b"]][0]))
        else:
            plan = "void: unrated, as if never played"
        print("to be settled: %s; because: %s" % (plan, o.reason))
        if not o.yes:
            if not sys.stdin.isatty():
                raise SystemExit("disputes: not settled: say --yes, or run it where it can ask")
            if input("settle match %d so? (yes/no) " % o.match).strip().lower() != "yes":
                print("not settled")
                return 1
        done = store.settle(o.match, state, winner, o.reason.strip(),
                            json.dumps(verdict, separators=(",", ":")) if verdict else "", int(time.time()), o.tau)
        if done is None:
            raise SystemExit("disputes: match %d was settled or changed meanwhile: nothing written" % o.match)
        print("match %d settled: %s" % (o.match, ladder.STATE_NAMES[done["state"]]) + (
            " (%s %d to %d, %s %d to %d)" % (names[0], round(done["a_before"]), round(done["a_after"]), names[1],
                                             round(done["b_before"]), round(done["b_after"]))
            if done["state"] == ladder.M_RATED else ""))
    finally:
        store.close()
        keep_owner(o.state)
    return 0


def main(argv):
    p = argparse.ArgumentParser(description="The ranked ladder's disputes: list, show, export, check, resolve.",
                                epilog=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--state", default=STATE, help="the ladder's folder (%(default)s)")
    p.add_argument("--dispute-days", type=int, default=60, help="the server's --dispute-days (%(default)s)")
    sub = p.add_subparsers(dest="command")
    s = sub.add_parser("list", help="the disputes open (--all: and those settled)")
    s.add_argument("--all", action="store_true")
    s = sub.add_parser("show", help="one match, and how it was settled")
    s.add_argument("match", type=int)
    s.add_argument("--json", action="store_true")
    s = sub.add_parser("export", help="a dispute's two uploads and what the store says, into a folder")
    s.add_argument("match", type=int)
    s.add_argument("--out", required=True)
    for name, about in (("check", "both sides' replays played back by the port: what they show"),
                        ("resolve", "a dispute settled: rated for a winner, or void, with a reason")):
        s = sub.add_parser(name, help=about)
        s.add_argument("match", type=int, nargs="?" if name == "check" else None)
        s.add_argument("--image", help="the disc image's .cue (for the port)")
        s.add_argument("--program", help="the port that plays the replays (rebushido-headless, or rebushido)")
        s.add_argument("--seconds", type=int, default=CHECK_SECONDS, help="the most a replay is played for")
        if name == "check":
            s.add_argument("--from", dest="source", help="an export's folder, in place of the store")
            s.add_argument("--save", help="the verdict as JSON, for resolve --verdict")
        else:
            s.add_argument("--winner", choices=("first", "second", "draw", "replays"))
            s.add_argument("--void", action="store_true")
            s.add_argument("--reason", required=True)
            s.add_argument("--verdict", help="check --save's file")
            s.add_argument("--tau", type=float, default=ladder.TAU, help="the server's --tau (%(default)s)")
            s.add_argument("--yes", action="store_true", help="settle without asking")
    o = p.parse_args(argv)
    if o.command is None:
        p.print_help()
        return 2
    return {"list": command_list, "show": command_show, "export": command_export, "check": command_check,
            "resolve": command_resolve}[o.command](o)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
