"""The ranked ladder of the ReBushido lobby (docs/NETPLAY.md, section 17).

Imported by rebushido_relay.py; the Python 3 standard library alone (3.8 or
newer), as the server is. What it is:

- identities: a player is the Ed25519 public key its program made on first
  use; every request that changes what the server keeps of it is signed with
  that key (RFC 8032, checked here: Python has no Ed25519 of its own);
- names: what a player is shown as, held to the rules of `name_problem`, no
  two alike once folded (`fold`), so that no one takes another's;
- the queue: players paired by rating and by their round trips to the
  server, never two of one address, the same pair at most so often a day;
- matches: each a session of two met through the lobby by a ticket, as a
  room of several's match is; both programs report the result, signed, with
  the match's digest (the hash of its every input and state hash): reports
  that agree are rated, others are held as disputed, unrated, and the
  replays they name may be uploaded for the owner to check;
- the rating: Glicko-2 (Glickman, "Example of the Glicko-2 system", 2013),
  each match a rating period of its own, the time since a player's last one
  counted in days (`glicko2`);
- the store: SQLite in write-ahead mode, written in transactions, backed up
  once a day with a few kept, migrated from an older schema as the server
  starts (`Store`, `MIGRATIONS`); a dispute settled by the owner's
  disputes.py (`Store.settle`).

    python3 tools/relay/ladder.py --self-test      Ed25519's and Glicko-2's
                                                   published cases
    python3 tools/relay/ladder.py --unpack FILE DIR   a dispute's upload
                                                   into its .rbr files
    python3 tools/relay/ladder.py --show STATE_DIR   the ladder, as JSON
"""

import hashlib
import hmac
import json
import math
import os
import secrets
import sqlite3
import struct
import sys
import time

# --- Ed25519 (RFC 8032) ------------------------------------------------------
#
# The arithmetic of the RFC in extended coordinates. Verifying takes a few
# milliseconds; the server limits how many it is asked to make. Signing is
# here for the tests and tools alone: its secrets are not kept from a clock.

P = 2**255 - 19
L = 2**252 + 27742317777372353535851937790883648493
D = (-121665 * pow(121666, P - 2, P)) % P
SQRT_M1 = pow(2, (P - 1) // 4, P)


def _recover_x(y, sign):
    if y >= P:
        return None
    x2 = (y * y - 1) * pow(D * y * y + 1, P - 2, P) % P
    if x2 == 0:
        return None if sign else 0
    x = pow(x2, (P + 3) // 8, P)
    if (x * x - x2) % P != 0:
        x = x * SQRT_M1 % P
    if (x * x - x2) % P != 0:
        return None
    if x & 1 != sign:
        x = P - x
    return x


_BY = 4 * pow(5, P - 2, P) % P
_BX = _recover_x(_BY, 0)
BASE = (_BX, _BY, 1, _BX * _BY % P)
IDENTITY = (0, 1, 1, 0)
D2 = 2 * D % P


def _add(p, q):
    a = (p[1] - p[0]) * (q[1] - q[0]) % P
    b = (p[1] + p[0]) * (q[1] + q[0]) % P
    c = p[3] * D2 * q[3] % P
    d = 2 * p[2] * q[2] % P
    e, f, g, h = b - a, d - c, d + c, b + a
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _double(p):
    a = p[0] * p[0] % P
    b = p[1] * p[1] % P
    c = 2 * p[2] * p[2] % P
    h = a + b
    e = h - (p[0] + p[1]) ** 2 % P
    g = a - b
    f = c + g
    return (e * f % P, g * h % P, f * g % P, e * h % P)


def _multiply(scalar, point):
    result = IDENTITY
    for bit in reversed(range(scalar.bit_length())):
        result = _double(result)
        if (scalar >> bit) & 1:
            result = _add(result, point)
    return result


def _equal(p, q):
    return (p[0] * q[2] - q[0] * p[2]) % P == 0 and (p[1] * q[2] - q[1] * p[2]) % P == 0


def _compress(point):
    z = pow(point[2], P - 2, P)
    x, y = point[0] * z % P, point[1] * z % P
    return int.to_bytes(y | ((x & 1) << 255), 32, "little")


def _decompress(data):
    if len(data) != 32:
        return None
    y = int.from_bytes(data, "little")
    sign = y >> 255
    y &= (1 << 255) - 1
    x = _recover_x(y, sign)
    if x is None:
        return None
    return (x, y, 1, x * y % P)


def _hash_int(*parts):
    return int.from_bytes(hashlib.sha512(b"".join(parts)).digest(), "little")


def _expand(secret):
    h = hashlib.sha512(secret).digest()
    a = int.from_bytes(h[:32], "little")
    a &= (1 << 254) - 8
    a |= 1 << 254
    return a, h[32:]


def public_key(secret):
    """The public half of a 32-byte secret (a seed, as Monocypher's)."""
    a, _ = _expand(secret)
    return _compress(_multiply(a, BASE))


def sign(secret, message):
    a, prefix = _expand(secret)
    public = _compress(_multiply(a, BASE))
    r = _hash_int(prefix, message) % L
    big_r = _compress(_multiply(r, BASE))
    k = _hash_int(big_r, public, message) % L
    return big_r + int.to_bytes((r + k * a) % L, 32, "little")


def verify(public, message, signature):
    """True if `signature` is `public`'s of `message` (RFC 8032, 5.1.7)."""
    if len(public) != 32 or len(signature) != 64:
        return False
    a = _decompress(public)
    big_r = _decompress(signature[:32])
    if a is None or big_r is None:
        return False
    s = int.from_bytes(signature[32:], "little")
    if s >= L:
        return False
    k = _hash_int(signature[:32], public, message) % L
    return _equal(_multiply(s, BASE), _add(big_r, _multiply(k, a)))


# RFC 8032, 7.1: tests 1, 2 and 3
RFC_CASES = [
    ("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60",
     "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
     "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"),
    ("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb",
     "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
     "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"),
    ("c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7",
     "fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
     "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"),
]

# --- Glicko-2 ------------------------------------------------------------------
#
# Glickman's paper, steps 1 to 8, with its constants: a new player is 1500,
# RD 350, volatility 0.06; the system constant tau 0.5. Each match is a rating
# period of its own for both players, and the step that widens a rating for
# the time without play (step 6) counts that time in days: `periods`, the days
# since the player's last rated match (fractions of one too), so that ten
# matches in an evening widen it no more than one. RD is kept between 30 and
# 350; a rating whose RD is above PROVISIONAL_RD is provisional.

SCALE = 173.7178
START_RATING = 1500.0
START_RD = 350.0
START_VOLATILITY = 0.06
TAU = 0.5
RD_MOST = 350.0
RD_LEAST = 30.0
PROVISIONAL_RD = 110.0
EPSILON = 0.000001
SECONDS_A_DAY = 86400


def _g(phi):
    return 1.0 / math.sqrt(1.0 + 3.0 * phi * phi / (math.pi * math.pi))


def _e(mu, mu_j, phi_j):
    return 1.0 / (1.0 + math.exp(-_g(phi_j) * (mu - mu_j)))


def idle(rd, volatility, periods):
    """RD after `periods` rating periods with no game (step 6 alone)."""
    phi = rd / SCALE
    return min(RD_MOST, math.sqrt(phi * phi + max(periods, 0.0) * volatility * volatility) * SCALE)


def glicko2(rating, rd, volatility, games, tau=TAU, periods=1.0):
    """A player's (rating, RD, volatility) after a period of `games`, each
    (opponent's rating, opponent's RD, score: 1, 0.5 or 0)."""
    if not games:
        return rating, idle(rd, volatility, periods), volatility
    mu = (rating - START_RATING) / SCALE
    phi = rd / SCALE
    v_inverse = 0.0
    improvement = 0.0
    for r_j, rd_j, score in games:
        mu_j = (r_j - START_RATING) / SCALE
        phi_j = rd_j / SCALE
        expected = _e(mu, mu_j, phi_j)
        v_inverse += _g(phi_j) ** 2 * expected * (1.0 - expected)
        improvement += _g(phi_j) * (score - expected)
    v = 1.0 / v_inverse
    delta = v * improvement
    # step 5: the new volatility, by the Illinois algorithm
    a = math.log(volatility * volatility)

    def f(x):
        ex = math.exp(x)
        return ex * (delta * delta - phi * phi - v - ex) / (2.0 * (phi * phi + v + ex) ** 2) - (x - a) / (tau * tau)

    big_a = a
    if delta * delta > phi * phi + v:
        big_b = math.log(delta * delta - phi * phi - v)
    else:
        k = 1
        while f(a - k * tau) < 0:
            k += 1
        big_b = a - k * tau
    f_a, f_b = f(big_a), f(big_b)
    while abs(big_b - big_a) > EPSILON:
        big_c = big_a + (big_a - big_b) * f_a / (f_b - f_a)
        f_c = f(big_c)
        if f_c * f_b <= 0:
            big_a, f_a = big_b, f_b
        else:
            f_a /= 2.0
        big_b, f_b = big_c, f_c
    new_volatility = math.exp(big_a / 2.0)
    # steps 6 to 8
    phi_star = math.sqrt(phi * phi + max(periods, 0.0) * new_volatility * new_volatility)
    new_phi = 1.0 / math.sqrt(1.0 / (phi_star * phi_star) + 1.0 / v)
    new_mu = mu + new_phi * new_phi * improvement
    new_rd = min(RD_MOST, max(RD_LEAST, new_phi * SCALE))
    return new_mu * SCALE + START_RATING, new_rd, new_volatility


# Glickman's worked example: 1500, RD 200, volatility 0.06, against 1400/30
# (won), 1550/100 (lost) and 1700/300 (lost), tau 0.5: 1464.06, 151.52,
# 0.05999.
GLICKO_EXAMPLE = ((1500.0, 200.0, 0.06, [(1400.0, 30.0, 1.0), (1550.0, 100.0, 0.0), (1700.0, 300.0, 0.0)]),
                  (1464.06, 151.52, 0.05999))

# --- names -----------------------------------------------------------------------
#
# 3 to 15 of the letters, digits, space, '.', '_' and '-' (the port's
# typeface has them, and so does every field of the lobby's), beginning with
# a letter or a digit, no two spaces together, none at the end. Two names
# are alike when their folds are: case, the separators, and the letters and
# digits that look alike, all taken away.

NAME_LEAST = 3
NAME_MOST = 15
NAME_CHARACTERS = set("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ._-")
LOOK_ALIKE = str.maketrans({"0": "o", "1": "l", "i": "l", "|": "l", "5": "s", "2": "z", "8": "b", "3": "e",
                            "4": "a", "7": "t", "9": "g", "6": "g"})
KEPT_NAMES = ("ReBushido", "admin", "administrator", "owner", "server", "moderator", "mod", "system", "lobby",
              "official", "SquareSoft", "Square Enix", "Lightweight", "nobody", "unknown", "you", "player", "host",
              "guest", "CPU", "staff", "support")


def fold(name):
    """What two names that look alike have in common."""
    text = name.lower().translate(LOOK_ALIKE)
    for sep in " ._-":
        text = text.replace(sep, "")
    while True:
        folded = text.replace("rn", "m").replace("vv", "w").replace("cl", "d")
        if folded == text:
            return text
        text = folded


def name_problem(name):
    """None if `name` may be a player's, else why not, in a player's words."""
    if len(name) < NAME_LEAST or len(name) > NAME_MOST:
        return "A name is %d to %d letters long." % (NAME_LEAST, NAME_MOST)
    if any(c not in NAME_CHARACTERS for c in name):
        return "A name has letters, digits, spaces, dots, dashes and underscores alone."
    if not name[0].isalnum():
        return "A name begins with a letter or a digit."
    if "  " in name or name.endswith(" "):
        return "A name has no two spaces together and none at its end."
    if len(fold(name)) < 2:
        return "A name needs more letters."
    if fold(name) in {fold(kept) for kept in KEPT_NAMES}:
        return "That name is kept: choose another."
    return None


# --- the wire ------------------------------------------------------------------
#
# RANK (kind 63, client to server), 240 bytes; an UPLOAD is its first 176
# bytes and a chunk of 1024. RANKED (kind 54, server to client), 168 bytes and
# up to eight entries of 48. Little-endian, text fields zero-padded ASCII.

K_RANKED = 54
K_RANK = 63
RANK_SIZE = 240
SIGNED_SIZE = 176  # what the signature covers, after the domain
UPLOAD_CHUNK = 1024
UPLOAD_SIZE = SIGNED_SIZE + UPLOAD_CHUNK
RANKED_HEAD = 168
ENTRY_SIZE = 48
ENTRIES_MOST = 8
DOMAIN = b"ReBushido ranked 1\0"

A_HELLO = 1  # who am I: the nonce, the profile
A_NAME = 2  # signed: the name
A_QUEUE = 3  # signed: into the queue
A_STAY = 4  # the token: still here, in the queue or a match
A_LEAVE = 5  # the token: out of the queue
A_REPORT = 6  # signed: a match's result and digest
A_UPLOAD = 7  # the token: a chunk of a disputed match's replays
A_LADDER = 8  # a page of the ladder, or of recent matches

SIGNED_ACTIONS = (A_NAME, A_QUEUE, A_REPORT)

ST_OK = 1
E_VERSION = 0x84
E_RATE_LIMITED = 0x86
E_BAD_REQUEST = 0x87
E_BAD_SIGNATURE = 0x89
E_STALE = 0x8A  # the nonce is old: sign again with the one sent
E_NAME_TAKEN = 0x8B
E_NAME_BAD = 0x8C
E_NOT_REGISTERED = 0x8D
E_NO_MATCH = 0x8E
E_INCOMPATIBLE = 0x88
E_DISABLED = 0x90
E_TOO_MANY = 0x91  # new identities from one address today
E_FULL = 0x92  # the dispute store, or the queue

ERROR_NAMES = {E_VERSION: "VERSION", E_RATE_LIMITED: "RATE_LIMITED", E_BAD_REQUEST: "BAD_REQUEST",
               E_BAD_SIGNATURE: "BAD_SIGNATURE", E_STALE: "STALE", E_NAME_TAKEN: "NAME_TAKEN",
               E_NAME_BAD: "NAME_BAD", E_NOT_REGISTERED: "NOT_REGISTERED", E_NO_MATCH: "NO_MATCH",
               E_INCOMPATIBLE: "INCOMPATIBLE", E_DISABLED: "DISABLED", E_TOO_MANY: "TOO_MANY", E_FULL: "FULL"}

# RANKED's `what`
W_PROFILE = 1
W_QUEUED = 2
W_MATCHED = 3
W_REPORTED = 4
W_LADDER = 5
W_UPLOADED = 6

# RANKED's flags
F_REGISTERED = 0x01
F_PROVISIONAL = 0x02
F_QUEUED = 0x04
F_IN_MATCH = 0x08

# a report's reason
R_DECIDED = 1  # the match played to its end
R_OTHER_LEFT = 2  # the other left, or went quiet
R_I_LEFT = 3  # this player left
R_DESYNC = 4
R_NOT_MET = 5  # the two never met

# a match's winner: its first player (its host), its second, nobody
WON_FIRST = 0
WON_SECOND = 1
WON_DRAW = 2
WON_NONE = 0xFF

# a match's state, in the store
M_PLAYING = 0
M_RATED = 1
M_DISPUTED = 2
M_VOID = 3
STATE_NAMES = {M_PLAYING: "playing", M_RATED: "rated", M_DISPUTED: "disputed", M_VOID: "void"}

# LADDER's kinds (its arg2)
L_TOP = 0  # from a rank on
L_AROUND = 1  # around the asker's own rank
L_MINE = 2  # the asker's recent matches
L_RECENT = 3  # everyone's recent matches

# the match's rule: First to N (docs/PORT.md, "Match rules"), a stage of
# the stage pages' 18 places drawn for it
RULE_FIRST_TO = 2
STAGE_PLACES = 18

UPLOAD_MAGIC = b"RBRANKUP"

RANK = struct.Struct("<2sBBI32s16s16s20sIHBBI16sBBBBI32sII8s")
assert RANK.size == SIGNED_SIZE


def _text(raw):
    end = raw.find(b"\0")
    text = raw if end < 0 else raw[:end]
    if end >= 0 and any(raw[end:]):
        return None
    if any(c < 0x20 or c > 0x7E for c in text):
        return None
    return text.decode("ascii")


class Request:
    """A RANK that passed validation."""

    __slots__ = ("action", "version", "key", "nonce", "name", "setup", "stamp", "rtt", "flags", "reason",
                 "match", "token", "winner", "rule", "count", "fights", "frames", "digest", "arg", "arg2",
                 "signature", "signed", "chunk", "raw")


def parse_rank(data):
    """A Request, or None where it is not one."""
    if len(data) not in (RANK_SIZE, UPLOAD_SIZE):
        return None
    fields = RANK.unpack_from(data)
    r = Request()
    (_, _, r.action, r.version, r.key, r.nonce, name, setup, r.stamp, r.rtt, r.flags, r.reason, r.match,
     r.token, r.winner, r.rule, r.count, r.fights, r.frames, r.digest, r.arg, r.arg2, _) = fields
    r.name = _text(name)
    r.setup = _text(setup)
    if r.name is None or r.setup is None:
        return None
    if r.action == A_UPLOAD:
        if len(data) != UPLOAD_SIZE:
            return None
        r.chunk = bytes(data[SIGNED_SIZE:])
        r.signature = b""
    else:
        if len(data) != RANK_SIZE:
            return None
        r.chunk = b""
        r.signature = bytes(data[SIGNED_SIZE:RANK_SIZE])
    r.signed = bytes(data[:SIGNED_SIZE])
    r.raw = bytes(data)
    return r


def rank_bytes(action, version, key=bytes(32), nonce=bytes(16), name="", setup="", stamp=0, rtt=0, flags=0,
               reason=0, match=0, token=bytes(16), winner=WON_NONE, rule=0, count=0, fights=0, frames=0,
               digest=bytes(32), arg=0, arg2=0):
    """The 176 bytes a RANK's signature covers (relay_client.py, tests)."""

    def field(text, size):
        return (text.encode("ascii") if isinstance(text, str) else text)[:size].ljust(size, b"\0")

    return RANK.pack(b"RB", K_RANK, action, version, key, nonce, field(name, 16), field(setup, 20), stamp, rtt,
                     flags, reason, match, token, winner, rule, count, fights, frames, digest, arg, arg2,
                     bytes(8))


def periods(player, now):
    """The rating periods (days) from a player's last rated match to `now`."""
    if not player["rated_at"]:
        return 0.0
    return max(0.0, (now - player["rated_at"]) / SECONDS_A_DAY)


# --- the store -----------------------------------------------------------------

# The store's schema: TABLES are those of its first, MIGRATIONS what each
# later one adds (a new store is made of the first and every migration). A
# store of an older schema is copied whole into backups/ (a name the daily
# copies' rotation leaves alone) and then migrated in one transaction, as
# the server starts; a store of a newer schema than this file's is refused.
SCHEMA = 2
MIGRATIONS = {
    # 2: a dispute settled by the owner (tools/relay/disputes.py): the
    # decision, why, what the dispute was, and what the replays said
    2: """
CREATE TABLE IF NOT EXISTS settled (
    match INTEGER PRIMARY KEY,
    decided INTEGER NOT NULL,
    state INTEGER NOT NULL, winner INTEGER NOT NULL,
    reason TEXT NOT NULL, was TEXT NOT NULL, verdict TEXT NOT NULL DEFAULT '');
""",
}
TABLES = """
CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
CREATE TABLE IF NOT EXISTS players (
    key BLOB PRIMARY KEY,
    name TEXT NOT NULL,
    folded TEXT NOT NULL,
    rating REAL NOT NULL, rd REAL NOT NULL, volatility REAL NOT NULL,
    wins INTEGER NOT NULL DEFAULT 0, losses INTEGER NOT NULL DEFAULT 0, draws INTEGER NOT NULL DEFAULT 0,
    created INTEGER NOT NULL, seen INTEGER NOT NULL, rated_at INTEGER NOT NULL DEFAULT 0);
CREATE INDEX IF NOT EXISTS players_folded ON players (folded);
CREATE INDEX IF NOT EXISTS players_rating ON players (rating);
CREATE TABLE IF NOT EXISTS matches (
    id INTEGER PRIMARY KEY,
    a BLOB NOT NULL, b BLOB NOT NULL,
    created INTEGER NOT NULL, decided INTEGER NOT NULL DEFAULT 0,
    state INTEGER NOT NULL, winner INTEGER NOT NULL DEFAULT 255, why TEXT NOT NULL DEFAULT '',
    rule INTEGER NOT NULL, count INTEGER NOT NULL, stage INTEGER NOT NULL,
    a_report BLOB, b_report BLOB,
    a_before REAL, a_after REAL, b_before REAL, b_after REAL);
CREATE INDEX IF NOT EXISTS matches_a ON matches (a, created);
CREATE INDEX IF NOT EXISTS matches_b ON matches (b, created);
CREATE INDEX IF NOT EXISTS matches_created ON matches (created);
CREATE TABLE IF NOT EXISTS replays (
    match INTEGER NOT NULL, side INTEGER NOT NULL, bytes INTEGER NOT NULL, file TEXT NOT NULL,
    received INTEGER NOT NULL, PRIMARY KEY (match, side));
"""


class Store:
    """The ladder's file: `ladder.sqlite` in the state folder, its backups in
    `backups/`, a dispute's replays in `disputes/`.

    Written in write-ahead mode with full synchronisation, each change one
    transaction: a crash or a power cut leaves the last change whole or not
    at all. Holds no address of anyone's: keys, names, ratings, results, the
    signed reports and the disputed replays alone."""

    def __init__(self, folder, keep_backups=7, server=True):
        """The server's store (`server`): made where there is none, migrated
        where it is of an older schema. A tool's (`server` False): the
        store that is there, of this schema, or SystemExit saying why."""
        self.folder = folder
        self.keep_backups = keep_backups
        self.path = os.path.join(folder, "ladder.sqlite")
        if server:
            os.makedirs(folder, exist_ok=True)
            os.makedirs(os.path.join(folder, "backups"), exist_ok=True)
            os.makedirs(os.path.join(folder, "disputes"), exist_ok=True)
        elif not os.path.isfile(self.path):
            raise SystemExit("no ladder at %s" % self.path)
        self.db = sqlite3.connect(self.path, isolation_level=None, timeout=30)
        try:
            self.open(server)
        except BaseException:
            self.db.close()
            raise

    def open(self, server):
        self.db.execute("PRAGMA journal_mode=WAL")
        self.db.execute("PRAGMA synchronous=FULL")
        self.db.execute("PRAGMA foreign_keys=ON")
        if server:
            self.db.executescript(TABLES)
        with self.transaction():
            have = self.db.execute("SELECT value FROM meta WHERE key = 'schema'").fetchone()
            fresh = have is None
            if fresh:
                self.db.execute("INSERT INTO meta VALUES ('schema', '1')")
        have = 1 if fresh else int(have[0])
        if have > SCHEMA:
            raise SystemExit("ladder: %s is of a newer schema (%d) than this program's (%d)" %
                             (self.path, have, SCHEMA))
        if have < SCHEMA:
            if not server:
                raise SystemExit("ladder: %s is of schema %d, this program's %d: the server of this version "
                                 "migrates it as it starts (tools/relay/README.md)" % (self.path, have, SCHEMA))
            if not fresh:
                self.copy_before(have)
            with self.transaction():
                for version in range(have + 1, SCHEMA + 1):
                    for statement in MIGRATIONS[version].split(";"):
                        if statement.strip():
                            self.db.execute(statement)
                self.db.execute("UPDATE meta SET value = ? WHERE key = 'schema'", (str(SCHEMA),))
            self.migrated = (have, SCHEMA, fresh)
        else:
            self.migrated = None
        with self.transaction():
            secret = self.db.execute("SELECT value FROM meta WHERE key = 'nonce_secret'").fetchone()
            if secret is None:
                if not server:
                    raise SystemExit("ladder: %s has no nonce secret: not a store the server made" % self.path)
                secret = (secrets.token_hex(32),)
                self.db.execute("INSERT INTO meta VALUES ('nonce_secret', ?)", secret)
        self.nonce_secret = bytes.fromhex(secret[0])

    def copy_before(self, have):
        """The whole store copied before a migration, into backups/ under a
        name the daily copies' rotation never removes: kept until the owner
        does."""
        stamp = time.strftime("%Y-%m-%d-%H%M%S", time.gmtime())
        path = os.path.join(self.folder, "backups", "before-schema-%d-from-%d-%s.sqlite" % (SCHEMA, have, stamp))
        part = path + ".part"
        copy = sqlite3.connect(part)
        try:
            self.db.backup(copy)
        finally:
            copy.close()
        with open(part, "r+b") as f:
            os.fsync(f.fileno())
        os.replace(part, path)
        self.copied_before = path

    def transaction(self):
        return _Transaction(self.db)

    def close(self):
        self.db.close()

    # players

    def player(self, key):
        row = self.db.execute("SELECT key, name, folded, rating, rd, volatility, wins, losses, draws, created, seen, "
                              "rated_at FROM players WHERE key = ?", (key,)).fetchone()
        if row is None:
            return None
        return dict(zip(("key", "name", "folded", "rating", "rd", "volatility", "wins", "losses", "draws",
                         "created", "seen", "rated_at"), row))

    def name_holder(self, folded, since):
        """The key that holds a name of this fold, seen since `since`."""
        row = self.db.execute("SELECT key FROM players WHERE folded = ? AND seen >= ? LIMIT 1",
                              (folded, since)).fetchone()
        return row[0] if row else None

    def set_name(self, key, name, now):
        with self.transaction():
            if self.player(key) is None:
                self.db.execute("INSERT INTO players (key, name, folded, rating, rd, volatility, created, seen) "
                                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                                (key, name, fold(name), START_RATING, START_RD, START_VOLATILITY, now, now))
            else:
                self.db.execute("UPDATE players SET name = ?, folded = ?, seen = ? WHERE key = ?",
                                (name, fold(name), now, key))

    def seen(self, key, now):
        self.db.execute("UPDATE players SET seen = ? WHERE key = ? AND seen < ?", (now, key, now - 3600))

    def rank_of(self, player):
        """A player's place on the ladder (1 is the top), and how many there are."""
        above = self.db.execute("SELECT COUNT(*) FROM players WHERE rated_at > 0 AND (rating > ? OR "
                                "(rating = ? AND key < ?))",
                                (player["rating"], player["rating"], player["key"])).fetchone()[0]
        total = self.db.execute("SELECT COUNT(*) FROM players WHERE rated_at > 0").fetchone()[0]
        if not player["rated_at"]:
            return 0, total
        return above + 1, total

    def top(self, first, count):
        rows = self.db.execute("SELECT key, name, rating, rd, volatility, wins, losses, draws, rated_at FROM players "
                               "WHERE rated_at > 0 ORDER BY rating DESC, key ASC LIMIT ? OFFSET ?",
                               (count, max(first - 1, 0))).fetchall()
        return [dict(rank=first + i, key=r[0], name=r[1], rating=r[2], rd=r[3], volatility=r[4], wins=r[5],
                     losses=r[6], draws=r[7], rated_at=r[8]) for i, r in enumerate(rows)]

    # matches

    def new_match(self, a, b, now, count, stage):
        with self.transaction():
            cursor = self.db.execute("INSERT INTO matches (a, b, created, state, rule, count, stage) "
                                     "VALUES (?, ?, ?, ?, ?, ?, ?)", (a, b, now, M_PLAYING, RULE_FIRST_TO, count,
                                                                      stage))
            return cursor.lastrowid

    def match(self, number):
        row = self.db.execute("SELECT id, a, b, created, decided, state, winner, why, rule, count, stage, a_report, "
                              "b_report, a_before, a_after, b_before, b_after FROM matches WHERE id = ?",
                              (number,)).fetchone()
        if row is None:
            return None
        return dict(zip(("id", "a", "b", "created", "decided", "state", "winner", "why", "rule", "count", "stage",
                         "a_report", "b_report", "a_before", "a_after", "b_before", "b_after"), row))

    def keep_report(self, number, side, raw):
        column = "a_report" if side == 0 else "b_report"
        with self.transaction():
            self.db.execute("UPDATE matches SET %s = ? WHERE id = ? AND state = ?" % column, (raw, number,
                                                                                               M_PLAYING))

    def pair_count(self, a, b, since):
        """The matches of the pair a and b since `since`, void ones not counted."""
        return self.db.execute("SELECT COUNT(*) FROM matches WHERE created >= ? AND state != ? AND "
                               "((a = ? AND b = ?) OR (a = ? AND b = ?))", (since, M_VOID, a, b, b, a)).fetchone()[0]

    def playing(self):
        return [r[0] for r in self.db.execute("SELECT id FROM matches WHERE state = ?", (M_PLAYING,))]

    def decide(self, number, state, winner, why, now, ratings=None):
        """A match decided: its state, winner and why; with `ratings`
        ({key: (rating, rd, volatility, score)}), both players rated in the
        same transaction."""
        with self.transaction():
            m = self.match(number)
            if m is None or m["state"] != M_PLAYING:
                return False
            before = {}
            after = {}
            if ratings:
                for key, (rating, rd, volatility, score) in ratings.items():
                    p = self.player(key)
                    before[key] = p["rating"]
                    after[key] = rating
                    column = "wins" if score == 1.0 else "losses" if score == 0.0 else "draws"
                    self.db.execute("UPDATE players SET rating = ?, rd = ?, volatility = ?, %s = %s + 1, "
                                    "rated_at = ?, seen = ? WHERE key = ?" % (column, column),
                                    (rating, rd, volatility, now, now, key))
            self.db.execute("UPDATE matches SET state = ?, winner = ?, why = ?, decided = ?, a_before = ?, "
                            "a_after = ?, b_before = ?, b_after = ? WHERE id = ?",
                            (state, winner, why, now, before.get(m["a"]), after.get(m["a"]), before.get(m["b"]),
                             after.get(m["b"]), number))
            return True

    def rate(self, a, b, winner, now, tau=TAU):
        """Both players of a match rated by Glicko-2 for its result, as of
        `now`: {key: (rating, rd, volatility, score)} for `decide`. Each is
        rated against the other's rating with the other's RD widened for
        the days since its last match."""
        pa, pb = self.player(a), self.player(b)
        seen_a = idle(pa["rd"], pa["volatility"], periods(pa, now))
        seen_b = idle(pb["rd"], pb["volatility"], periods(pb, now))
        score_a = 1.0 if winner == WON_FIRST else 0.0 if winner == WON_SECOND else 0.5
        new_a = glicko2(pa["rating"], pa["rd"], pa["volatility"], [(pb["rating"], seen_b, score_a)], tau,
                        periods(pa, now))
        new_b = glicko2(pb["rating"], pb["rd"], pb["volatility"], [(pa["rating"], seen_a, 1.0 - score_a)], tau,
                        periods(pb, now))
        return {a: new_a + (score_a,), b: new_b + (1.0 - score_a,)}

    def disputed(self):
        """The matches held disputed, oldest first."""
        return [self.match(r[0]) for r in
                self.db.execute("SELECT id FROM matches WHERE state = ? ORDER BY created", (M_DISPUTED,))]

    def replays_of(self, number):
        """A match's kept uploads: {side: (file's path, bytes, when kept)}."""
        return {side: (os.path.join(self.folder, "disputes", name), size, received) for side, size, name, received in
                self.db.execute("SELECT side, bytes, file, received FROM replays WHERE match = ?", (number,))}

    def settled(self, number=None):
        """The disputes the owner settled (all, newest first, or one's)."""
        columns = ("match", "decided", "state", "winner", "reason", "was", "verdict")
        if number is not None:
            row = self.db.execute("SELECT %s FROM settled WHERE match = ?" % ", ".join(columns), (number,)).fetchone()
            return dict(zip(columns, row)) if row else None
        return [dict(zip(columns, row)) for row in
                self.db.execute("SELECT %s FROM settled ORDER BY decided DESC" % ", ".join(columns))]

    def settle(self, number, state, winner, reason, verdict, now, tau=TAU):
        """A disputed match settled by the owner, in one transaction: rated
        for `winner` (both players' ratings moved as of now) or void, the
        decision kept with its reason, what the dispute was and what the
        replays said (`verdict`, text). The match, or None where it is not
        held disputed (settled already, or never disputed)."""
        if state not in (M_RATED, M_VOID) or (state == M_RATED) != (winner in (WON_FIRST, WON_SECOND, WON_DRAW)):
            raise ValueError("a match is settled rated with a winner, or void with none")
        with self.transaction():
            m = self.match(number)
            if m is None or m["state"] != M_DISPUTED:
                return None
            ratings = None
            if state == M_RATED:
                if self.player(m["a"]) is None or self.player(m["b"]) is None:
                    raise ValueError("a player of match %d is no longer kept: it can only be void" % number)
                ratings = self.rate(m["a"], m["b"], winner, now, tau)
            self.db.execute("UPDATE matches SET state = ? WHERE id = ?", (M_PLAYING, number))
            self.decide(number, state, winner, ("settled by the owner: " + reason)[:200], now, ratings)
            self.db.execute("INSERT INTO settled VALUES (?, ?, ?, ?, ?, ?, ?)",
                            (number, now, state, winner, reason, m["why"], verdict or ""))
            return self.match(number)

    def recent(self, key, count):
        if key is None:
            rows = self.db.execute("SELECT id FROM matches WHERE state != ? ORDER BY created DESC LIMIT ?",
                                   (M_PLAYING, count)).fetchall()
        else:
            rows = self.db.execute("SELECT id FROM matches WHERE state != ? AND (a = ? OR b = ?) "
                                   "ORDER BY created DESC LIMIT ?", (M_PLAYING, key, key, count)).fetchall()
        return [self.match(r[0]) for r in rows]

    def name_of(self, key):
        row = self.db.execute("SELECT name FROM players WHERE key = ?", (key,)).fetchone()
        return row[0] if row else "?"

    # disputes

    def dispute_bytes(self):
        return self.db.execute("SELECT COALESCE(SUM(bytes), 0) FROM replays").fetchone()[0]

    def has_replay(self, number, side):
        return self.db.execute("SELECT 1 FROM replays WHERE match = ? AND side = ?", (number, side)).fetchone()

    def keep_replay(self, number, side, data, now):
        """A disputed match's replays from one side, written whole or not at all."""
        name = "match-%d-%s.rbu" % (number, "ab"[side])
        path = os.path.join(self.folder, "disputes", name)
        part = path + ".part"
        with open(part, "wb") as f:
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(part, path)
        with self.transaction():
            self.db.execute("INSERT OR REPLACE INTO replays VALUES (?, ?, ?, ?, ?)",
                            (number, side, len(data), name, now))

    # care

    def expire(self, now, dispute_days, keep_days, name_days):
        """What has been kept long enough taken away: disputed replays after
        `dispute_days`, matches and players unseen after `keep_days`."""
        removed = 0
        old = self.db.execute("SELECT match, side, file FROM replays WHERE received < ?",
                              (now - dispute_days * SECONDS_A_DAY,)).fetchall()
        for number, side, name in old:
            try:
                os.remove(os.path.join(self.folder, "disputes", name))
            except OSError:
                pass
            with self.transaction():
                self.db.execute("DELETE FROM replays WHERE match = ? AND side = ?", (number, side))
            removed += 1
        with self.transaction():
            gone = now - keep_days * SECONDS_A_DAY
            removed += self.db.execute("DELETE FROM matches WHERE created < ? AND state != ?",
                                       (gone, M_PLAYING)).rowcount
            removed += self.db.execute("DELETE FROM players WHERE seen < ?", (gone,)).rowcount
            # a settling is kept as long as its match
            removed += self.db.execute("DELETE FROM settled WHERE match NOT IN (SELECT id FROM matches)").rowcount
        return removed

    def backup(self, now):
        """Today's copy into backups/ (taken while the server runs: SQLite's
        own online backup), and the oldest beyond `keep_backups` removed. The
        path made, or None if today's is there already."""
        day = time.strftime("%Y-%m-%d", time.gmtime(now))
        folder = os.path.join(self.folder, "backups")
        path = os.path.join(folder, "ladder-%s.sqlite" % day)
        if os.path.exists(path):
            return None
        part = path + ".part"
        if os.path.exists(part):
            os.remove(part)
        copy = sqlite3.connect(part)
        try:
            self.db.backup(copy)
        finally:
            copy.close()
        with open(part, "r+b") as f:
            os.fsync(f.fileno())
        os.replace(part, path)
        kept = sorted(n for n in os.listdir(folder) if n.startswith("ladder-") and n.endswith(".sqlite"))
        for name in kept[: max(len(kept) - self.keep_backups, 0)]:
            os.remove(os.path.join(folder, name))
        return path


class _Transaction:
    def __init__(self, db):
        self.db = db
        self.outer = False

    def __enter__(self):
        self.outer = not self.db.in_transaction
        if self.outer:
            self.db.execute("BEGIN IMMEDIATE")
        return self

    def __exit__(self, kind, value, trace):
        if self.outer:
            self.db.execute("COMMIT" if kind is None else "ROLLBACK")
        return False


# --- the ladder: the queue, the matches, the reports -------------------------


class Entry:
    """A player in the queue."""

    __slots__ = ("key", "name", "rating", "rd", "setup", "rtt", "endpoint", "token", "since", "seen")


class Live:
    """A match being played, as the server remembers it while it runs."""

    __slots__ = ("number", "keys", "tokens", "code", "ticket", "ended", "seen", "first_report", "count", "stage",
                 "names", "ratings", "endpoints", "uploads", "reported")

    def __init__(self):
        self.ended = None
        self.first_report = None
        self.uploads = {}
        self.reported = [False, False]


class Ladder:
    """The ranked half of the server: given the RANK datagrams, the server's
    socket to answer by and its codes and tickets for the matches."""

    def __init__(self, server, options, log):
        self.server = server
        self.o = options
        self.log = log
        self.store = Store(options.state, options.backups_keep) if options.state else None
        self.queue = {}  # key -> Entry
        self.tokens = {}  # token -> key
        self.live = {}  # match number -> Live
        self.live_of = {}  # key -> match number
        self.codes = {}  # a match's code -> Live
        self.decided = {}  # match number -> (state, winner, ratings) for the late
        self.identities = {}  # ip_key -> (day, count): new identities today
        self.signers = {}  # key -> Bucket: signatures a key may have checked
        self.backup_at = 0.0
        self.expired_at = 0.0
        self.paired_at = 0.0
        self.counts = {"matches": 0, "rated": 0, "disputed": 0, "void": 0, "refused": 0}
        self.stamp = 0  # the request's, echoed in the answer
        if self.store is not None:
            # matches being played when the server stopped: reported after
            # its start as any other is; the queue and the meetings are gone
            for number in self.store.playing():
                m = self.store.match(number)
                live = Live()
                live.number = number
                live.keys = (m["a"], m["b"])
                live.tokens = (None, None)
                live.code = b""
                live.ticket = bytes(16)
                live.seen = [time.monotonic(), time.monotonic()]
                live.count = m["count"]
                live.stage = m["stage"]
                live.names = (self.store.name_of(m["a"]), self.store.name_of(m["b"]))
                live.ratings = (None, None)
                live.endpoints = [None, None]
                self.live[number] = live
                for key in live.keys:
                    self.live_of[key] = number
                for side, column in ((0, "a_report"), (1, "b_report")):
                    if m[column] is not None:
                        live.first_report = live.first_report or time.monotonic()
                        live.reported[side] = True

    # time

    def wall(self):
        return int(time.time())

    def nonce(self, key, minute=None):
        if minute is None:
            minute = int(time.time() // 60)
        return hmac.new(self.store.nonce_secret, key + struct.pack("<I", minute), hashlib.sha256).digest()[:16]

    def nonce_good(self, key, nonce):
        minute = int(time.time() // 60)
        return any(hmac.compare_digest(self.nonce(key, m), nonce) for m in (minute, minute - 1))

    # answers

    def answer(self, endpoint, status, request, what=W_PROFILE, key=None, flags=0, token=bytes(16), waited=0,
               live=None, result=None, upload=(0, 0), entries=(), ladder=(0, 0)):
        """A RANKED: its head, then the entries of a ladder."""
        player = self.store.player(key) if (self.store is not None and key is not None) else None
        nonce = self.nonce(key) if (self.store is not None and key is not None) else bytes(16)
        name = b""
        rating = rd = 0
        rank = total = 0
        wins = losses = draws = 0
        if player is not None:
            flags |= F_REGISTERED
            shown_rd = idle(player["rd"], player["volatility"], self.periods(player))
            if shown_rd > PROVISIONAL_RD:
                flags |= F_PROVISIONAL
            name = player["name"].encode()
            rating, rd = int(round(player["rating"])), int(round(shown_rd))
            rank, total = self.store.rank_of(player)
            wins, losses, draws = player["wins"], player["losses"], player["draws"]
        elif self.store is not None:
            flags |= F_PROVISIONAL
            rating, rd = int(START_RATING), int(START_RD)
            total = self.store.rank_of({"rating": 0, "key": b"", "rated_at": 0})[1]
        if key is not None and key in self.queue:
            flags |= F_QUEUED
        if key is not None and key in self.live_of:
            flags |= F_IN_MATCH
        number = role = stage = count = 0
        code = b""
        ticket = bytes(16)
        other = b""
        other_rating = other_rd = 0
        if live is not None:
            side = live.keys.index(key) if key in live.keys else 0
            number = live.number
            role = 1 if side == 0 else 2
            stage = live.stage
            count = live.count
            code = live.code
            ticket = live.ticket
            other = live.names[1 - side].encode()
            if live.ratings[1 - side] is not None:
                other_rating, other_rd = (int(round(v)) for v in live.ratings[1 - side])
        res_state = res_upload = 0
        res_winner = WON_NONE
        before = after = other_before = other_after = 0
        if result is not None:
            res_state, res_upload, res_winner, before, after, other_before, other_after = result
        head = b"".join((
            struct.pack("<2sBBI", b"RB", K_RANKED, status, request & 0xFF),
            nonce,
            struct.pack("<IBBBB", self.stamp, flags, what, self.o.ranked_first_to, 0),
            name[:16].ljust(16, b"\0"),
            struct.pack("<HHIIHHHH", min(rating, 65535), min(rd, 65535), rank, total, min(wins, 65535),
                        min(losses, 65535), min(draws, 65535), 0),
            token,
            struct.pack("<HH", min(int(waited), 65535), min(len(self.queue), 65535)),
            struct.pack("<IBBBB", number, role, stage, count, 0),
            code[:8].ljust(8, b"\0"),
            ticket,
            other[:16].ljust(16, b"\0"),
            struct.pack("<HH", min(other_rating, 65535), min(other_rd, 65535)),
            struct.pack("<BBBBHHHH", res_state, res_upload, res_winner, 0, int(round(before)), int(round(after)),
                        int(round(other_before)), int(round(other_after))),
            struct.pack("<II", upload[0], upload[1]),
            struct.pack("<IBB2x", ladder[0], len(entries), ladder[1]),
        ))
        assert len(head) == RANKED_HEAD, len(head)
        self.server.send(endpoint, head + b"".join(entries))

    def refuse(self, endpoint, status, request, key=None):
        self.counts["refused"] += 1
        if self.o.verbose:
            self.log("ranked: %s: %s" % (endpoint.text(), ERROR_NAMES.get(status, status)))
        self.answer(endpoint, status, request, key=key if (key is not None and self.store is not None) else None)

    def periods(self, player):
        """The rating periods (days) since a player's last rated match."""
        return periods(player, self.wall())

    # the datagram

    def datagram(self, endpoint, source, data, now):
        # a dispute's upload comes in many chunks: the spectators' rate
        upload = len(data) > 3 and data[3] == A_UPLOAD
        self.stamp = struct.unpack_from("<I", data, 92)[0] if len(data) >= 96 else 0
        if source is None or not (source.watch if upload else source.ranked).take(now):
            if source is not None:
                self.server.rate_limited_ranked(endpoint, source, now)
            return
        # the version first (the header's number): another version's RANK
        # may be of another size or layout
        if len(data) >= 8 and struct.unpack_from("<I", data, 4)[0] != self.server.version:
            self.refuse(endpoint, E_VERSION, data[3])
            return
        r = parse_rank(data)
        if r is None:
            self.refuse(endpoint, E_BAD_REQUEST, data[3] if len(data) > 3 else 0)
            return
        if self.store is None:
            self.refuse(endpoint, E_DISABLED, r.action)
            return
        if r.action in SIGNED_ACTIONS:
            if not self.nonce_good(r.key, r.nonce):
                self.refuse(endpoint, E_STALE, r.action, key=r.key)
                return
            signer = self.signers.get(r.key)
            if signer is None:
                if len(self.signers) > 10000:
                    self.signers.clear()
                signer = self.signers[r.key] = self.server.bucket(self.o.signed_rate, self.o.signed_rate * 3, now)
            if not source.signed.take(now) or not signer.take(now):
                self.server.rate_limited_ranked(endpoint, source, now)
                return
            if not verify(r.key, DOMAIN + r.signed, r.signature):
                self.counts["refused"] += 1
                self.log("ranked: a request whose signature is not its key's refused (%s)" %
                         {A_NAME: "name", A_QUEUE: "queue", A_REPORT: "report"}[r.action])
                self.answer(endpoint, E_BAD_SIGNATURE, r.action)
                return
        handler = {A_HELLO: self.hello, A_NAME: self.name, A_QUEUE: self.enqueue, A_STAY: self.stay,
                   A_LEAVE: self.leave, A_REPORT: self.report, A_UPLOAD: self.upload, A_LADDER: self.ladder}.get(
            r.action)
        if handler is None:
            self.refuse(endpoint, E_BAD_REQUEST, r.action)
            return
        handler(endpoint, source, r, now)

    def hello(self, endpoint, source, r, now):
        self.answer(endpoint, ST_OK, r.action, key=r.key)

    def name_change(self, endpoint, source, r, now):
        """The request's name made the key's: None, or the refusal sent."""
        player = self.store.player(r.key)
        if player is not None and player["name"] == r.name:
            return None
        why = name_problem(r.name)
        if why is not None:
            return E_NAME_BAD
        wall = self.wall()
        holder = self.store.name_holder(fold(r.name), wall - self.o.name_days * SECONDS_A_DAY)
        if holder is not None and holder != r.key:
            return E_NAME_TAKEN
        if player is None:
            day = wall // SECONDS_A_DAY
            had = self.identities.get(endpoint.ip_key)
            count = had[1] if had and had[0] == day else 0
            if count >= self.o.identities_per_day:
                return E_TOO_MANY
            self.identities[endpoint.ip_key] = (day, count + 1)
        self.store.set_name(r.key, r.name, wall)
        if self.o.verbose:
            self.log("ranked: %s is %s" % (r.key.hex()[:16], r.name))
        return None

    def name(self, endpoint, source, r, now):
        problem = self.name_change(endpoint, source, r, now)
        if problem is not None:
            self.refuse(endpoint, problem, r.action, key=r.key)
            return
        self.answer(endpoint, ST_OK, r.action, key=r.key)

    def enqueue(self, endpoint, source, r, now):
        problem = self.name_change(endpoint, source, r, now)
        if problem is not None:
            self.refuse(endpoint, problem, r.action, key=r.key)
            return
        number = self.live_of.get(r.key)
        if number is not None:
            # in a match still: it is given again (a lost MATCHED)
            live = self.live[number]
            side = live.keys.index(r.key)
            if live.tokens[side] is None:
                tokens = list(live.tokens)
                tokens[side] = secrets.token_bytes(16)
                live.tokens = tuple(tokens)
                self.tokens[tokens[side]] = r.key
            live.endpoints[side] = endpoint
            live.seen[side] = now
            self.answer(endpoint, ST_OK, r.action, W_MATCHED, key=r.key, token=live.tokens[side], live=live)
            return
        entry = self.queue.get(r.key)
        if entry is None:
            if len(self.queue) >= self.o.queue_most:
                self.refuse(endpoint, E_FULL, r.action, key=r.key)
                return
            entry = Entry()
            entry.key = r.key
            entry.token = secrets.token_bytes(16)
            entry.since = now
            self.queue[r.key] = entry
            self.tokens[entry.token] = r.key
            if self.o.verbose:
                self.log("ranked: %s queues" % r.name)
        player = self.store.player(r.key)
        entry.name = player["name"]
        entry.rating = player["rating"]
        entry.rd = idle(player["rd"], player["volatility"], self.periods(player))
        entry.setup = r.setup
        entry.rtt = r.rtt
        entry.endpoint = endpoint
        entry.seen = now
        self.store.seen(r.key, self.wall())
        self.answer(endpoint, ST_OK, r.action, W_QUEUED, key=r.key, token=entry.token, waited=now - entry.since)

    def stay(self, endpoint, source, r, now):
        key = self.tokens.get(r.token)
        if key is None or key != r.key:
            self.refuse(endpoint, E_NO_MATCH, r.action, key=r.key)
            return
        number = self.live_of.get(key)
        if number is not None:
            live = self.live[number]
            side = live.keys.index(key)
            live.seen[side] = now
            live.endpoints[side] = endpoint
            if live.reported[side]:
                self.answer(endpoint, ST_OK, r.action, W_REPORTED, key=key, token=r.token, live=live,
                            result=(M_PLAYING, 0, WON_NONE, 0, 0, 0, 0))
            else:
                self.answer(endpoint, ST_OK, r.action, W_MATCHED, key=key, token=r.token, live=live)
            return
        if r.match and r.match in self.decided:
            self.answer_decided(endpoint, r, key, r.match)
            return
        entry = self.queue.get(key)
        if entry is not None:
            entry.seen = now
            entry.endpoint = endpoint
            entry.rtt = r.rtt
            self.answer(endpoint, ST_OK, r.action, W_QUEUED, key=key, token=r.token, waited=now - entry.since)
            return
        if r.match:
            self.answer_decided(endpoint, r, key, r.match)
            return
        self.answer(endpoint, ST_OK, r.action, key=key)

    def leave(self, endpoint, source, r, now):
        key = self.tokens.get(r.token)
        if key is not None and key == r.key and key in self.queue:
            entry = self.queue.pop(key)
            self.tokens.pop(entry.token, None)
            if self.o.verbose:
                self.log("ranked: %s leaves the queue" % entry.name)
        self.answer(endpoint, ST_OK, r.action, key=r.key)

    # pairing

    def pair_limit_reached(self, a, b):
        since = self.wall() - SECONDS_A_DAY
        return self.store.pair_count(a, b, since) >= self.o.ranked_pair_per_day

    def pair(self, now):
        """Two of the queue made a match where they are close enough in
        rating and in round trips: the longer one has waited, the wider."""
        waiting = sorted(self.queue.values(), key=lambda e: e.since)
        paired = set()
        for i, a in enumerate(waiting):
            if a.key in paired:
                continue
            waited = now - a.since
            window = min(self.o.ranked_window + self.o.ranked_widen * waited, self.o.ranked_window_most)
            ping = min(self.o.ranked_ping + self.o.ranked_ping_widen * waited, self.o.ranked_ping_most)
            best = None
            for b in waiting[i + 1:]:
                if b.key in paired or b.setup != a.setup:
                    continue
                if not self.o.ranked_same_address and b.endpoint.ip_key == a.endpoint.ip_key:
                    continue
                gap = abs(a.rating - b.rating)
                trip = a.rtt + b.rtt
                if gap > window or trip > ping:
                    continue
                if self.pair_limit_reached(a.key, b.key):
                    continue
                cost = gap / window + trip / ping
                if best is None or cost < best[0]:
                    best = (cost, b)
            if best is not None:
                b = best[1]
                paired.add(a.key)
                paired.add(b.key)
                self.make_match(a, b, now)

    def make_match(self, a, b, now):
        """A match of a (its host, player 1: the longer waiting) and b."""
        stage = secrets.choice(self.o.ranked_stages)
        number = self.store.new_match(a.key, b.key, self.wall(), self.o.ranked_first_to, stage)
        live = Live()
        live.number = number
        live.keys = (a.key, b.key)
        live.tokens = (a.token, b.token)
        live.code = self.server.new_code()
        live.ticket = secrets.token_bytes(16)
        live.seen = [now, now]
        live.count = self.o.ranked_first_to
        live.stage = stage
        live.names = (a.name, b.name)
        live.ratings = ((a.rating, a.rd), (b.rating, b.rd))
        live.endpoints = [a.endpoint, b.endpoint]
        self.live[number] = live
        self.codes[live.code] = live
        for entry in (a, b):
            del self.queue[entry.key]
            self.live_of[entry.key] = number
        self.counts["matches"] += 1
        self.log("ranked: match %d, %s (%d) and %s (%d), first to %d, stage %d" %
                 (number, a.name, a.rating, b.name, b.rating, live.count, stage))
        for side, entry in enumerate((a, b)):
            self.answer(entry.endpoint, ST_OK, A_STAY, W_MATCHED, key=entry.key, token=entry.token, live=live)

    def ticket_of(self, code, secret):
        """The ranked match a ticketed HELLO is of, or None."""
        live = self.codes.get(code)
        if live is None or live.ended is not None:
            return None
        if not hmac.compare_digest(live.ticket, secret[:16]) or any(secret[16:]):
            return None
        return live

    # results

    def report(self, endpoint, source, r, now):
        number = r.match
        live = self.live.get(number)
        if live is None:
            if number in self.decided or (self.store.match(number) or {}).get("state", M_PLAYING) != M_PLAYING:
                self.answer_decided(endpoint, r, r.key, number)
                return
            self.refuse(endpoint, E_NO_MATCH, r.action, key=r.key)
            return
        if r.key not in live.keys:
            self.refuse(endpoint, E_NO_MATCH, r.action, key=r.key)
            return
        side = live.keys.index(r.key)
        if r.reason not in (R_DECIDED, R_OTHER_LEFT, R_I_LEFT, R_DESYNC, R_NOT_MET) or \
                r.winner not in (WON_FIRST, WON_SECOND, WON_DRAW, WON_NONE):
            self.refuse(endpoint, E_BAD_REQUEST, r.action, key=r.key)
            return
        m = self.store.match(number)
        column = "a_report" if side == 0 else "b_report"
        if m[column] is None:
            self.store.keep_report(number, side, r.raw)
            if self.o.verbose:
                self.log("ranked: match %d: %s reports %s, %s" % (number, live.names[side],
                                                                  REASON_NAMES.get(r.reason, r.reason),
                                                                  WINNER_NAMES.get(r.winner, r.winner)))
        live.seen[side] = now
        live.endpoints[side] = endpoint
        live.reported[side] = True
        if live.first_report is None:
            live.first_report = now
        self.consider(live, now)
        if number in self.decided:
            self.answer_decided(endpoint, r, r.key, number)
        else:
            self.answer(endpoint, ST_OK, r.action, W_REPORTED, key=r.key, token=live.tokens[side] or bytes(16),
                        live=live, result=(M_PLAYING, 0, WON_NONE, 0, 0, 0, 0))

    def answer_decided(self, endpoint, r, key, number):
        m = self.store.match(number)
        if m is None or key not in (m["a"], m["b"]):
            self.refuse(endpoint, E_NO_MATCH, r.action, key=key)
            return
        side = 0 if key == m["a"] else 1
        mine = ("a", "b")[side]
        theirs = ("a", "b")[1 - side]
        upload = m["state"] == M_DISPUTED and not self.store.has_replay(number, side) and \
            m[mine + "_report"] is not None
        have = 0
        live = self.live.get(number)
        if live is not None and side in live.uploads:
            have = len(live.uploads[side])
        self.answer(endpoint, ST_OK, r.action, W_REPORTED, key=key, token=r.token,
                    result=(m["state"], 1 if upload else 0, m["winner"], m[mine + "_before"] or 0,
                            m[mine + "_after"] or 0, m[theirs + "_before"] or 0, m[theirs + "_after"] or 0),
                    upload=(have, self.o.dispute_bytes))

    def consider(self, live, now):
        """A match decided where its reports, or their absence, say enough."""
        m = self.store.match(live.number)
        if m is None or m["state"] != M_PLAYING:
            return
        reports = [parse_rank(m["a_report"]) if m["a_report"] else None,
                   parse_rank(m["b_report"]) if m["b_report"] else None]
        gone = [now - seen > self.o.gone_timeout for seen in live.seen]
        if reports[0] is not None and reports[1] is not None:
            state, winner, why = judge_two(reports[0], reports[1], m)
        elif reports[0] is not None or reports[1] is not None:
            side = 0 if reports[0] is not None else 1
            if now - live.first_report < self.o.report_wait:
                return
            if gone[1 - side]:
                state, winner, why = judge_one(reports[side], side, m)
            elif now - live.first_report > self.o.report_timeout:
                state, winner, why = M_DISPUTED, WON_NONE, "the other's report never came"
            else:
                return
        elif self.wall() - m["created"] > self.o.match_timeout:
            state, winner, why = M_VOID, WON_NONE, "nobody reported it"
        else:
            return
        self.finish(live, state, winner, why, now)

    def finish(self, live, state, winner, why, now):
        a, b = live.keys
        # the ratings read and written in one transaction: the owner's
        # disputes.py may rate the same players beside the server
        with self.store.transaction():
            ratings = self.store.rate(a, b, winner, self.wall(), self.o.tau) if state == M_RATED else None
            self.store.decide(live.number, state, winner, why, self.wall(), ratings)
        self.decided[live.number] = now
        live.ended = now
        for key in live.keys:
            if self.live_of.get(key) == live.number:
                del self.live_of[key]
        self.counts[STATE_NAMES[state]] += 1
        m = self.store.match(live.number)
        self.log("ranked: match %d %s: %s%s" % (
            live.number, STATE_NAMES[state],
            {WON_FIRST: live.names[0] + " won", WON_SECOND: live.names[1] + " won", WON_DRAW: "a draw"}.get(
                winner, "no winner"),
            (" (%s %d to %d, %s %d to %d)" % (live.names[0], m["a_before"], m["a_after"], live.names[1],
                                               m["b_before"], m["b_after"])) if state == M_RATED else
            " (%s)" % why))

    # disputes

    def upload(self, endpoint, source, r, now):
        key = self.tokens.get(r.token)
        live = self.live.get(r.match)
        if key is None or key != r.key or live is None or key not in live.keys:
            self.refuse(endpoint, E_NO_MATCH, r.action, key=r.key)
            return
        side = live.keys.index(key)
        m = self.store.match(r.match)
        if m["state"] != M_DISPUTED or self.store.has_replay(r.match, side):
            self.answer_decided(endpoint, r, key, r.match)
            return
        total = r.arg2
        if total > self.o.dispute_bytes or total < len(UPLOAD_MAGIC) or \
                self.store.dispute_bytes() + total > self.o.disputes_total:
            self.refuse(endpoint, E_FULL, r.action, key=key)
            return
        have = live.uploads.setdefault(side, bytearray())
        if r.arg == len(have) and r.arg < total:
            have += r.chunk[: min(UPLOAD_CHUNK, total - r.arg)]
        if len(have) >= total:
            data = bytes(have[:total])
            if not data.startswith(UPLOAD_MAGIC):
                live.uploads.pop(side, None)
                self.refuse(endpoint, E_BAD_REQUEST, r.action, key=key)
                return
            self.store.keep_replay(r.match, side, data, self.wall())
            live.uploads.pop(side, None)
            self.log("ranked: match %d: %s's replays kept for the owner (%d bytes)" %
                     (r.match, live.names[side], total))
            self.answer(endpoint, ST_OK, r.action, W_UPLOADED, key=key, upload=(total, total))
            return
        self.answer(endpoint, ST_OK, r.action, W_UPLOADED, key=key, upload=(len(have), total))

    # the ladder's pages

    def ladder(self, endpoint, source, r, now):
        if not source.query.take(now):
            self.server.rate_limited_ranked(endpoint, source, now)
            return
        kind = r.arg2
        entries = []
        if kind in (L_TOP, L_AROUND):
            first = max(r.arg, 1)
            if kind == L_AROUND:
                player = self.store.player(r.key)
                rank = self.store.rank_of(player)[0] if player else 0
                first = max(rank - ENTRIES_MOST // 2, 1) if rank else 1
            for p in self.store.top(first, ENTRIES_MOST):
                shown_rd = idle(p["rd"], p["volatility"], self.periods(p))
                entries.append(struct.pack("<IHHHHHH16s", p["rank"], int(round(p["rating"])), int(round(shown_rd)),
                                           min(p["wins"], 65535), min(p["losses"], 65535), min(p["draws"], 65535),
                                           1 if shown_rd > PROVISIONAL_RD else 0,
                                           p["name"].encode()[:16].ljust(16, b"\0")).ljust(ENTRY_SIZE, b"\0"))
            self.answer(endpoint, ST_OK, r.action, W_LADDER, key=r.key, entries=entries, ladder=(first, kind))
            return
        for m in self.store.recent(r.key if kind == L_MINE else None, ENTRIES_MOST):
            delta_a = int(round((m["a_after"] or 0) - (m["a_before"] or 0))) if m["state"] == M_RATED else 0
            delta_b = int(round((m["b_after"] or 0) - (m["b_before"] or 0))) if m["state"] == M_RATED else 0
            entries.append(struct.pack("<IBBhhH16s16s", m["created"], m["winner"], m["state"], delta_a, delta_b,
                                       int(round(m["a_after"] or m["a_before"] or 0)),
                                       self.store.name_of(m["a"]).encode()[:16].ljust(16, b"\0"),
                                       self.store.name_of(m["b"]).encode()[:16].ljust(16, b"\0")).ljust(
                ENTRY_SIZE, b"\0"))
        self.answer(endpoint, ST_OK, r.action, W_LADDER, key=r.key, entries=entries, ladder=(0, kind))

    # time

    def tick(self, now):
        if self.store is None:
            return
        for entry in list(self.queue.values()):
            if now - entry.seen > self.o.queue_timeout:
                del self.queue[entry.key]
                self.tokens.pop(entry.token, None)
                if self.o.verbose:
                    self.log("ranked: %s let go of the queue (not heard from)" % entry.name)
        if now - self.paired_at >= 0.5:
            self.paired_at = now
            self.pair(now)
        for live in list(self.live.values()):
            if live.ended is None:
                self.consider(live, now)
            elif now - live.ended > self.o.decided_linger:
                del self.live[live.number]
                self.codes.pop(live.code, None)
                for token in live.tokens:
                    if token is not None and self.tokens.get(token) in live.keys and \
                            self.tokens[token] not in self.queue:
                        del self.tokens[token]
        for number in [n for n, at in self.decided.items() if now - at > self.o.decided_linger]:
            del self.decided[number]
        if now - self.backup_at >= self.o.backup_every:
            self.backup_at = now
            made = self.store.backup(self.wall()) if self.o.backup_every >= SECONDS_A_DAY else \
                self.backup_now()
            if made:
                self.log("ranked: backed up to %s" % os.path.basename(made))
        if now - self.expired_at >= 3600:
            self.expired_at = now
            self.store.expire(self.wall(), self.o.dispute_days, self.o.keep_days, self.o.name_days)

    def backup_now(self):
        """A backup now, whatever the day (a test's --backup-every under a
        day): named by the second instead."""
        folder = os.path.join(self.store.folder, "backups")
        stamp = time.strftime("%Y-%m-%d-%H%M%S", time.gmtime())
        path = os.path.join(folder, "ladder-%s.sqlite" % stamp)
        if os.path.exists(path):
            return None
        copy = sqlite3.connect(path + ".part")
        try:
            self.store.db.backup(copy)
        finally:
            copy.close()
        os.replace(path + ".part", path)
        kept = sorted(n for n in os.listdir(folder) if n.startswith("ladder-") and n.endswith(".sqlite"))
        for name in kept[: max(len(kept) - self.store.keep_backups, 0)]:
            os.remove(os.path.join(folder, name))
        return path

    def summary(self):
        return "ranked: queue %d, playing %d, matches %d, rated %d, disputed %d, void %d, refused %d" % (
            len(self.queue), sum(1 for l in self.live.values() if l.ended is None), self.counts["matches"],
            self.counts["rated"], self.counts["disputed"], self.counts["void"], self.counts["refused"])

    def close(self):
        if self.store is not None:
            self.store.close()


REASON_NAMES = {R_DECIDED: "decided", R_OTHER_LEFT: "the other left", R_I_LEFT: "it left", R_DESYNC: "a desync",
                R_NOT_MET: "never met"}
WINNER_NAMES = {WON_FIRST: "the first won", WON_SECOND: "the second won", WON_DRAW: "a draw", WON_NONE: "nobody"}


def judge_two(a, b, m):
    """Both reports in: (state, winner, why)."""
    for report in (a, b):
        if report.reason == R_DECIDED and (report.rule != RULE_FIRST_TO or report.count != m["count"]):
            return M_DISPUTED, WON_NONE, "a report's rule is not the ladder's"
    reasons = (a.reason, b.reason)
    if reasons == (R_DECIDED, R_DECIDED):
        if a.winner == b.winner and a.digest == b.digest and a.frames == b.frames:
            return M_RATED, a.winner, "both reports agree"
        if a.winner != b.winner:
            return M_DISPUTED, WON_NONE, "the reports name different winners"
        return M_DISPUTED, WON_NONE, "the reports' digests differ"
    if reasons == (R_I_LEFT, R_OTHER_LEFT) and a.winner == b.winner == WON_SECOND:
        return M_RATED, WON_SECOND, "the first player left"
    if reasons == (R_OTHER_LEFT, R_I_LEFT) and a.winner == b.winner == WON_FIRST:
        return M_RATED, WON_FIRST, "the second player left"
    # a leave in the match's last frames: the one program saw the match
    # decided, the leaver's left first; the leaver's own signed report
    # concedes, and who leaves loses
    if reasons == (R_I_LEFT, R_DECIDED) and a.winner == WON_SECOND:
        return M_RATED, WON_SECOND, "the first player left as it was decided"
    if reasons == (R_DECIDED, R_I_LEFT) and b.winner == WON_FIRST:
        return M_RATED, WON_FIRST, "the second player left as it was decided"
    if all(reason in (R_NOT_MET, R_DESYNC) for reason in reasons):
        return M_VOID, WON_NONE, "not played to an end (%s)" % " and ".join(REASON_NAMES[x] for x in reasons)
    return M_DISPUTED, WON_NONE, "the reports differ (%s, %s)" % (REASON_NAMES[a.reason], REASON_NAMES[b.reason])


def judge_one(report, side, m):
    """One report, the other player gone: the leaver loses."""
    if report.reason in (R_NOT_MET, R_DESYNC) or report.winner == WON_NONE:
        return M_VOID, WON_NONE, "one report (%s), the other gone" % REASON_NAMES[report.reason]
    if report.reason == R_DECIDED and (report.rule != RULE_FIRST_TO or report.count != m["count"]):
        return M_DISPUTED, WON_NONE, "the report's rule is not the ladder's"
    return M_RATED, report.winner, "one report, the other player gone (%s)" % REASON_NAMES[report.reason]


# --- tools ---------------------------------------------------------------------


def unpack(path, folder):
    """A dispute's upload into its replay files: their paths."""
    with open(path, "rb") as f:
        data = f.read()
    if not data.startswith(UPLOAD_MAGIC):
        raise SystemExit("%s is not a ranked match's upload" % path)
    count = struct.unpack_from("<I", data, 8)[0]
    at = 12
    made = []
    os.makedirs(folder, exist_ok=True)
    for _ in range(count):
        size_name = struct.unpack_from("<I", data, at)[0]
        name = os.path.basename(data[at + 4:at + 4 + size_name].decode("ascii", "replace")) or "replay.rbr"
        at += 4 + size_name
        size = struct.unpack_from("<I", data, at)[0]
        at += 4
        out = os.path.join(folder, name)
        with open(out, "wb") as f:
            f.write(data[at:at + size])
        at += size
        made.append(out)
    return made


def show(folder):
    """The ladder as JSON, the store opened for reading alone: safe beside
    the running server, and making no file of its own (as root, it must
    not, or the service's user could not open it after)."""
    path = os.path.join(folder, "ladder.sqlite")
    if not os.path.isfile(path):
        raise SystemExit("no ladder at %s" % path)
    store = Store.__new__(Store)
    store.db = sqlite3.connect("file:%s?mode=ro" % path, uri=True)
    out = {"players": [], "recent": []}
    for p in store.top(1, 1000):
        out["players"].append({"rank": p["rank"], "name": p["name"], "rating": round(p["rating"]),
                               "rd": round(p["rd"]), "wins": p["wins"], "losses": p["losses"], "draws": p["draws"],
                               "key": p["key"].hex()})
    for m in store.recent(None, 50):
        out["recent"].append({"match": m["id"], "a": store.name_of(m["a"]), "b": store.name_of(m["b"]),
                              "state": STATE_NAMES[m["state"]], "winner": m["winner"], "why": m["why"],
                              "when": time.strftime("%Y-%m-%d %H:%M", time.gmtime(m["created"]))})
    out["disputed"] = [m["id"] for m in store.disputed()]
    try:
        out["settled"] = [{"match": s["match"], "state": STATE_NAMES[s["state"]], "winner": s["winner"],
                           "reason": s["reason"], "was": s["was"],
                           "when": time.strftime("%Y-%m-%d %H:%M", time.gmtime(s["decided"]))}
                          for s in store.settled()]
    except sqlite3.OperationalError:
        out["settled"] = []  # a store of schema 1, before the server of 2 migrated it
    store.close()
    return out


def self_test():
    failures = []
    for secret, public, message, signature in RFC_CASES:
        secret, public = bytes.fromhex(secret), bytes.fromhex(public)
        message, signature = bytes.fromhex(message), bytes.fromhex(signature)
        if public_key(secret) != public or sign(secret, message) != signature:
            failures.append("RFC 8032's key or signature of %s" % secret.hex()[:16])
        if not verify(public, message, signature):
            failures.append("RFC 8032's signature of %s refused" % secret.hex()[:16])
        bad = bytearray(signature)
        bad[7] ^= 0x04
        if verify(public, message, bytes(bad)) or verify(public, message + b"x", signature):
            failures.append("a changed signature or message of %s passes" % secret.hex()[:16])
    (r, rd, vol, games), want = GLICKO_EXAMPLE
    got = glicko2(r, rd, vol, games)
    if abs(got[0] - want[0]) > 0.01 or abs(got[1] - want[1]) > 0.01 or abs(got[2] - want[2]) > 0.00001:
        failures.append("Glicko-2's example: %.2f, %.2f, %.5f, not %.2f, %.2f, %.5f" % (got + want))
    return failures


def main(argv):
    if argv[:1] == ["--self-test"]:
        failed = self_test()
        for failure in failed:
            print("FAILED: " + failure)
        print("ladder self-test: %s" % ("FAILED" if failed else "OK"))
        return 1 if failed else 0
    if argv[:1] == ["--unpack"] and len(argv) == 3:
        for path in unpack(argv[1], argv[2]):
            print(path)
        return 0
    if argv[:1] == ["--show"] and len(argv) == 2:
        print(json.dumps(show(argv[1]), indent=1))
        return 0
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
