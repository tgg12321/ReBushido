#!/usr/bin/env python3
"""The ReBushido lobby, rendezvous and relay server (protocol version 2).

One process, UDP, the Python 3 standard library alone (3.8 or newer). It
keeps a list of open rooms, introduces a room's host and guest to each other
so they can punch through their routers, and passes their game traffic on
when they cannot. It reads no disc and writes nothing to disk; all it keeps,
in memory, is each room's code, names, setup code, the salt and verifier of
its password, and the addresses.

Version 2 adds rooms of several (docs/NETPLAY.md, section 16): up to eight
members, a rotation of matches the server picks (a fixed turn, or the
winner staying), matches beside it, and spectators. Each match is met as a
two-player room is, admitted by a ticket the server gives its two players.
The match's host sends the server the match's feed once (its settings, the
fight's start state as a replay keeps it, then every input and hash); the
server keeps it in memory while the match lasts and a little after, and
gives it to every spectator who asks. A client of version 1 is refused with
VERSION.

The wire format is docs/NETPLAY.md, sections 14 and 16; tools/relay/README.md
says how to run and deploy it.

    python3 rebushido_relay.py                      0.0.0.0:7845 and [::]:7845
    python3 rebushido_relay.py --listen 127.0.0.1:7845 --verbose
"""

import argparse
import collections
import hashlib
import hmac
import secrets
import select
import signal
import socket
import struct
import sys
import time

RELAY_VERSION = 2
DEFAULT_PORT = 7845
MAGIC = b"RB"

# Kinds.
K_HELLO = 48
K_STATE = 49
K_PUNCH = 50
K_PUNCH_ACK = 51
K_LIST = 52
K_ROOMS = 53
K_WHOAMI = 55
K_YOUARE = 56
K_ROOM = 57  # a member of a room of several: open, enter, stay, ...
K_ROSTER = 58  # the room as the server has it, to one member
K_FEED = 59  # a match's host: its feed, for the spectators
K_FEED_ACK = 60
K_WATCH = 61  # a spectator: what it has of a match's feed
K_SCENE = 62  # the feed, to a spectator
K_CONTROL = 48  # kinds 48..63 are the server's; below is game traffic
K_CONTROL_END = 64

# HELLO detail bits.
F_HOST = 0x01
F_GUEST = 0x02
F_RESET = 0x04
F_PUNCHED4 = 0x08
F_PUNCHED6 = 0x10
F_TICKET = 0x20  # a match of a room of several: the secret is its ticket
F_ALL = F_HOST | F_GUEST | F_RESET | F_PUNCHED4 | F_PUNCHED6 | F_TICKET

# STATE details.
S_WAITING = 1
S_PASSWORD = 2
S_PUNCH = 3
S_DIRECT4 = 4
S_DIRECT6 = 5
S_RELAYED = 6
E_NO_ROOM = 0x81
E_ROOM_FULL = 0x82
E_SERVER_FULL = 0x83
E_VERSION = 0x84
E_WRONG_PASSWORD = 0x85
E_RATE_LIMITED = 0x86
E_BAD_REQUEST = 0x87
E_INCOMPATIBLE = 0x88

MODE_NAMES = {
    S_WAITING: "waiting",
    S_PUNCH: "punch",
    S_DIRECT4: "direct4",
    S_DIRECT6: "direct6",
    S_RELAYED: "relayed",
}

# Candidate types.
C_PUBLIC = 0
C_LAN4 = 1
C_MAPPED4 = 2
C_GLOBAL6 = 3

HEADER = struct.Struct("<2sBBI")
HELLO_SIZE = 208
STATE_SIZE = 208
LIST_SIZE = 16
WHOAMI_SIZE = 16
ROOM_SIZE = 92
ROOMS_PER_PAGE = 12
ADDRESS_SIZE = 20
MAX_CANDIDATES = 4

CODE_ALPHABET = "ACDEFGHJKMNPQRTUVWXY3479"
CODE_LENGTH = 6

ZERO16 = bytes(16)
ZERO32 = bytes(32)
V4_MAPPED = bytes(10) + b"\xff\xff"

CHALLENGES_KEPT = 4  # per guest address: proofs of a recent one are taken
CHALLENGE_ADDRESSES = 64  # per room

# A room of several (section 16). ROOM's actions (its detail).
A_OPEN = 1
A_ENTER = 2
A_STAY = 3
A_LEAVE = 4
A_RULE = 5  # the host's rotation: RULE_...
A_RESULT = 6  # a match's end: its number and who won
A_SIDE_OPEN = 7  # a match beside the room's
A_SIDE_JOIN = 8  # ... joined (its place among the sides)
A_SIDE_DONE = 9
# ROOM's flags.
R_PLAYS = 0x01  # in the rotation (else a spectator)
# The rotation.
RULE_OFF = 0
RULE_FIXED = 1  # A+B, B+C, C+A, ...
RULE_WINNER = 2  # the winner stays, the next waiting comes in
# A match's winner (RESULT's arg).
W_FIRST = 0
W_SECOND = 1
W_DRAW = 2
# ROSTER's member flags.
M_PLAYS = 0x01
M_BUSY = 0x02  # in a match now
M_HOST = 0x04
ROOM_SIZE_HELLO = 160
ROSTER_SIZE = 472
MEMBERS_MAX = 8
SIDES_MAX = 4
TOKEN_SIZE = 16
TICKET_SIZE = 16
NOBODY = 0xFF
# ROOMS' locked byte: the room is a room of several.
LIST_GROUP = 0x02
# FEED's and SCENE's parts (their detail).
P_SETTINGS = 1
P_START = 2
P_INPUTS = 3
P_END = 4
P_NONE = 5  # SCENE: nothing to give yet, or no such match
SETTINGS_SIZE = 64
CHUNK = 1024
CHUNKS_MAX = 1024  # a start of at most 1 MB
CHUNK_MAP = CHUNKS_MAX // 8
INPUT_SIZE = 4  # a frame: both pads' buttons
INPUTS_A_DATAGRAM = 240  # eight hashes' frames: every hash among them fits
HASHES_A_DATAGRAM = 8
FRAMES_MAX = 0x40000
FEED_ACK_SIZE = 160
WATCH_SIZE = 192
NO_START = 0xFFFFFFFF
NONE_NOTHING_YET = 1
NONE_NO_MATCH = 2
NONE_NOT_MEMBER = 3
CHUNKS_A_WATCH = 16
# matches beside a room are numbered from here, the rotation's from 1
SIDE_NUMBERS = 1000


def log(text):
    print(text, flush=True)


# --- encoding ------------------------------------------------------------


def header(kind, detail=0, number=0):
    return HEADER.pack(MAGIC, kind, detail, number)


def pad(data, size):
    return data[:size].ljust(size, b"\0")


def read_string(raw):
    """A zero-padded printable ASCII field: its text, or None if malformed."""
    end = raw.find(b"\0")
    if end < 0:
        end = len(raw)
    elif any(raw[end:]):
        return None  # something after the padding
    text = raw[:end]
    for c in text:
        if c < 0x20 or c > 0x7E:
            return None
    return text


class Endpoint:
    """Where a datagram came from: its identity and how to answer it."""

    __slots__ = ("sock", "raw", "family", "ip", "port", "key", "ip_key")

    def __init__(self, sock, raw):
        self.sock = sock
        self.raw = raw
        if sock.family == socket.AF_INET:
            ip = socket.inet_aton(raw[0])
            family = 4
        else:
            ip = socket.inet_pton(socket.AF_INET6, raw[0].split("%")[0])
            family = 6
            if ip[:12] == V4_MAPPED:
                ip = ip[12:]
                family = 4
        self.family = family
        self.ip = ip
        self.port = raw[1]
        self.key = (family, ip, self.port)
        # Rate limits are per IPv4 address and per IPv6 /64.
        self.ip_key = (family, ip if family == 4 else ip[:8])

    def address(self):
        """The 20-byte ADDRESS field."""
        return struct.pack("<BBH", self.family, 0, self.port) + pad(self.ip, 16)

    def candidate(self):
        """The PUBLIC candidate: what the server saw."""
        return struct.pack("<BBH", C_PUBLIC, 0, self.port) + pad(self.ip, 16)

    def text(self):
        if self.family == 4:
            return "%s:%d" % (socket.inet_ntoa(self.ip), self.port)
        return "[%s]:%d" % (socket.inet_ntop(socket.AF_INET6, self.ip), self.port)


class Hello:
    """A HELLO that passed validation."""

    __slots__ = (
        "flags",
        "code",
        "name",
        "player",
        "setup",
        "salt",
        "secret",
        "candidates",
        "has_v6",
        "rtt",
    )


def parse_hello(data):
    """The fields of a 208-byte version-2 HELLO, or None if malformed."""
    flags = data[3]
    if flags & ~F_ALL:
        return None
    role = flags & (F_HOST | F_GUEST)
    if role not in (F_HOST, F_GUEST):
        return None
    if flags & F_RESET and role != F_HOST:
        return None
    hello = Hello()
    hello.flags = flags
    hello.code = read_string(data[8:16])
    hello.name = read_string(data[16:40])
    hello.player = read_string(data[40:56])
    hello.setup = read_string(data[56:76])
    if None in (hello.code, hello.name, hello.player, hello.setup):
        return None
    hello.salt = data[76:92]
    hello.secret = data[92:124]
    count = data[124]
    if count > MAX_CANDIDATES or data[125]:
        return None
    # the sender's round trip to this server, for the list's ping
    hello.rtt = struct.unpack_from("<H", data, 126)[0]
    candidates = []
    has_v6 = False
    for i in range(count):
        raw = data[128 + i * 20 : 148 + i * 20]
        ctype, pad_byte, port = struct.unpack_from("<BBH", raw)
        if ctype not in (C_LAN4, C_MAPPED4, C_GLOBAL6) or pad_byte or port == 0:
            return None
        if ctype == C_GLOBAL6:
            if not any(raw[4:]):
                return None
            has_v6 = True
        elif any(raw[8:]) or not any(raw[4:8]):
            return None
        candidates.append(raw)
    hello.candidates = candidates
    hello.has_v6 = has_v6
    if role == F_HOST and not hello.name and not flags & F_TICKET:
        return None
    return hello


class RoomHello:
    """A ROOM (a member of a room of several) that passed validation."""

    __slots__ = ("action", "code", "name", "player", "setup", "salt", "secret", "token", "flags", "rule", "arg",
                 "stamp", "rtt", "number")


def parse_room_hello(data):
    """The fields of a 160-byte ROOM, or None if malformed."""
    hello = RoomHello()
    hello.action = data[3]
    if not A_OPEN <= hello.action <= A_SIDE_DONE:
        return None
    hello.code = read_string(data[8:16])
    hello.name = read_string(data[16:40])
    hello.player = read_string(data[40:56])
    hello.setup = read_string(data[56:76])
    if None in (hello.code, hello.name, hello.player, hello.setup):
        return None
    hello.salt = data[76:92]
    hello.secret = data[92:124]
    hello.token = data[124:140]
    hello.flags, hello.rule, hello.arg = data[140], data[141], data[142]
    if hello.flags & ~R_PLAYS or hello.rule > RULE_WINNER or data[143]:
        return None
    hello.stamp, hello.rtt = struct.unpack_from("<IH", data, 144)
    hello.number = struct.unpack_from("<I", data, 152)[0]
    if hello.action == A_OPEN and not hello.name:
        return None
    return hello


# --- rate limits -----------------------------------------------------------


class Bucket:
    """A token bucket: `rate` a second, at most `burst` saved."""

    __slots__ = ("rate", "burst", "tokens", "stamp")

    def __init__(self, rate, burst, now):
        self.rate = rate
        self.burst = burst
        self.tokens = burst
        self.stamp = now

    def take(self, now, amount=1.0):
        self.tokens = min(self.burst, self.tokens + (now - self.stamp) * self.rate)
        self.stamp = now
        if self.tokens >= amount:
            self.tokens -= amount
            return True
        return False


class Source:
    """What is kept per source IP (an IPv6 /64), for the limits only."""

    __slots__ = ("hello", "query", "create", "watch", "wrong", "blocked_until", "limited_at", "seen")

    def __init__(self, limits, now):
        self.hello = Bucket(limits.hello_rate, limits.hello_rate, now)
        # a spectator's asks and a match host's feed: many more a second
        self.watch = Bucket(limits.watch_rate, limits.watch_rate, now)
        self.query = Bucket(limits.query_rate, limits.query_rate, now)
        self.create = Bucket(limits.creations_per_min / 60.0, limits.creations_per_min, now)
        self.wrong = collections.deque()
        self.blocked_until = 0.0
        self.limited_at = -1e9
        self.seen = now


# --- rooms -----------------------------------------------------------------


class Room:
    __slots__ = (
        "code",
        "seq",
        "created",
        "host",
        "name",
        "host_player",
        "setup",
        "salt",
        "verifier",
        "host_candidates",
        "host_v6",
        "host_flags",
        "host_hello",
        "guest",
        "guest_player",
        "guest_setup",
        "guest_candidates",
        "guest_v6",
        "guest_flags",
        "mode",
        "token",
        "punch_start",
        "last_hello",
        "last_datagram",
        "challenges",
        "relay_count",
        "relay_bytes",
        "ticket",
        "counted",
        "host_rtt",
    )

    def __init__(self, code, seq, host, now):
        self.code = code
        self.seq = seq
        self.created = now
        self.host = host
        self.host_hello = now
        # a match of a room of several: unlisted, admitted by its ticket
        self.ticket = None
        self.counted = True
        self.host_rtt = 0
        self.guest = None
        self.mode = S_WAITING
        self.token = 0
        self.punch_start = 0.0
        self.last_hello = now
        self.last_datagram = now
        self.challenges = {}
        self.relay_count = None
        self.relay_bytes = None
        self.clear_guest()

    def clear_guest(self):
        self.guest = None
        self.guest_player = b""
        self.guest_setup = b""
        self.guest_candidates = []
        self.guest_v6 = False
        self.guest_flags = 0
        self.host_flags = 0
        self.mode = S_WAITING
        self.token = 0
        self.relay_count = None
        self.relay_bytes = None

    def locked(self):
        return self.salt != ZERO16 or self.verifier != ZERO32

    def take_host(self, hello, now):
        self.name = hello.name
        self.host_player = hello.player
        self.setup = hello.setup
        self.salt = hello.salt
        self.verifier = hello.secret
        self.host_candidates = hello.candidates
        self.host_v6 = hello.has_v6
        self.host_flags = hello.flags
        self.host_hello = now
        self.last_hello = now
        self.last_datagram = now
        self.host_rtt = hello.rtt
        if self.ticket is not None:
            self.salt = ZERO16
            self.verifier = ZERO32

    def take_guest(self, hello, now):
        self.guest_player = hello.player
        self.guest_setup = hello.setup
        self.guest_candidates = hello.candidates
        self.guest_v6 = hello.has_v6
        self.guest_flags = hello.flags
        self.last_hello = now
        self.last_datagram = now


# --- rooms of several (section 16) -------------------------------------------


class Member:
    __slots__ = ("id", "token", "player", "plays", "endpoint", "seen", "rtt", "wins")

    def __init__(self, member_id, player, plays, endpoint, now):
        self.id = member_id
        self.token = secrets.token_bytes(TOKEN_SIZE)
        self.player = player
        self.plays = plays
        self.endpoint = endpoint
        self.seen = now
        self.rtt = 0
        self.wins = 0


class Feed:
    """A match's feed as its host sent it: what spectators are given."""

    __slots__ = ("settings", "start_frame", "start_size", "start", "chunks", "inputs_from", "inputs", "hashes",
                 "end", "winner")

    def __init__(self):
        self.settings = None
        self.start_frame = NO_START
        self.start_size = 0
        self.start = None
        self.chunks = set()
        self.inputs_from = None  # the frame of the first input kept
        self.inputs = bytearray()
        self.hashes = collections.OrderedDict()  # frame -> 8 bytes
        self.end = None  # the frame the session ended at
        self.winner = NOBODY

    def start_whole(self):
        return self.start is not None and len(self.chunks) * CHUNK >= self.start_size

    def next_frame(self):
        return NO_START if self.inputs_from is None else self.inputs_from + len(self.inputs) // INPUT_SIZE


class Match:
    __slots__ = ("code", "ticket", "number", "a", "b", "side", "feed", "begun", "ended")

    def __init__(self, code, number, a, b, side, now):
        self.code = code
        self.ticket = secrets.token_bytes(TICKET_SIZE)
        self.number = number
        self.a = a  # member ids: the first is the match's host, player 1
        self.b = b
        self.side = side
        self.feed = Feed()
        self.begun = now
        self.ended = None


class Group:
    """A room of several: its members, its rotation and its matches."""

    __slots__ = ("code", "seq", "created", "name", "setup", "salt", "verifier", "members", "queue", "rule", "match",
                 "sides", "ended", "number", "side_number", "next_id", "last", "challenges", "host_id")

    def __init__(self, code, seq, hello, now):
        self.code = code
        self.seq = seq
        self.created = now
        self.name = hello.name
        self.setup = hello.setup
        self.salt = hello.salt
        self.verifier = hello.secret
        self.members = []  # in the order they came
        self.queue = []  # member ids, the rotation's order
        self.rule = hello.rule
        self.match = None  # the rotation's match now
        self.sides = []  # matches beside it
        self.ended = []  # matches over, their feeds kept a while
        self.number = 0  # the rotation's matches made
        self.side_number = SIDE_NUMBERS  # and those beside it
        self.next_id = 1
        self.last = (0, NOBODY, NOBODY, NOBODY)  # the last result: number, a, b, winner
        self.challenges = {}
        self.host_id = 0

    def locked(self):
        return self.salt != ZERO16 or self.verifier != ZERO32

    def member(self, member_id):
        for m in self.members:
            if m.id == member_id:
                return m
        return None

    def by_token(self, token):
        for m in self.members:
            if hmac.compare_digest(m.token, token):
                return m
        return None

    def matches(self):
        return ([self.match] if self.match is not None else []) + self.sides

    def busy(self, member_id):
        return any(member_id in (m.a, m.b) for m in self.matches())

    def host(self):
        return self.member(self.host_id)


class Server:
    def __init__(self, options, sockets):
        self.o = options
        self.sockets = sockets
        self.rooms = {}  # code -> Room, in creation order
        self.by_host = {}  # host Endpoint.key -> Room
        self.guest_of = {}  # guest Endpoint.key -> Room
        self.relayed = {}  # Endpoint.key of either side of a RELAYED room -> Room
        self.per_ip = collections.Counter()  # host ip_key -> rooms
        self.sources = {}  # ip_key -> Source
        self.groups = {}  # code -> Group: the rooms of several
        self.group_of = {}  # an opener's Endpoint.key -> its Group (a lost ROSTER)
        self.match_codes = {}  # a match's code -> (Group, Match)
        self.seq = 0
        self.received = 0
        self.sent = 0
        self.forwarded = 0
        self.dropped = 0
        self.stats_at = time.monotonic()
        self.verbose = options.verbose

    # --- sending ---

    def send(self, endpoint, data):
        try:
            endpoint.sock.sendto(data, endpoint.raw)
            self.sent += 1
        except OSError:
            pass

    def state(self, endpoint, detail, room=None, as_host=False, salt=ZERO16, challenge=ZERO16, setup=None):
        """A STATE for `endpoint`; with a room, as its host or its guest."""
        number = 0
        code = b""
        peer = bytes(ADDRESS_SIZE)
        candidates = []
        player = b""
        other_setup = b""
        if room is not None:
            code = room.code
            if as_host:
                other = room.guest
                if other is not None:
                    player = room.guest_player
                    other_setup = room.guest_setup
                    own = room.guest_candidates
            else:
                other = room.host
                player = room.host_player
                other_setup = room.setup
                own = room.host_candidates
            if other is not None and detail in (S_PUNCH, S_DIRECT4, S_DIRECT6, S_RELAYED):
                number = room.token
                peer = other.address()
                candidates = [other.candidate()] + own[: MAX_CANDIDATES - 1]
        if setup is not None:
            other_setup = setup
        data = b"".join(
            (
                header(K_STATE, detail, number),
                pad(code, 8),
                endpoint.address(),
                peer,
                salt,
                challenge,
                struct.pack("<B3x", len(candidates)),
                pad(b"".join(candidates), 80),
                pad(player, 16),
                pad(other_setup, 20),
            )
        )
        assert len(data) == STATE_SIZE
        self.send(endpoint, data)

    def refuse(self, endpoint, detail, room=None, as_host=False, **fields):
        self.state(endpoint, detail, room, as_host, **fields)
        if self.verbose:
            log("%s: %s" % (endpoint.text(), ERROR_NAMES.get(detail, detail)))

    def rate_limited(self, endpoint, source, now):
        """RATE_LIMITED, once a second at most per source; otherwise silence."""
        if now - source.limited_at >= 1.0:
            source.limited_at = now
            self.refuse(endpoint, E_RATE_LIMITED)
        else:
            self.dropped += 1

    # --- receiving ---

    def source(self, endpoint, now):
        source = self.sources.get(endpoint.ip_key)
        if source is None:
            if len(self.sources) >= self.o.max_sources:
                self.forget_sources(now)
                if len(self.sources) >= self.o.max_sources:
                    return None
            source = Source(self.o, now)
            self.sources[endpoint.ip_key] = source
        source.seen = now
        return source

    def datagram(self, sock, data, raw, now):
        self.received += 1
        if len(data) < HEADER.size or data[:2] != MAGIC:
            self.dropped += 1
            return
        try:
            endpoint = Endpoint(sock, raw)
        except (OSError, ValueError):
            self.dropped += 1
            return
        kind = data[2]
        if kind < K_CONTROL:
            self.relay(endpoint, data, now)
        elif kind == K_HELLO:
            self.hello(endpoint, data, now)
        elif kind == K_LIST:
            self.list_rooms(endpoint, data, now)
        elif kind == K_WHOAMI:
            self.whoami(endpoint, data, now)
        elif kind == K_ROOM:
            self.room_hello(endpoint, data, now)
        elif kind == K_FEED:
            self.feed(endpoint, data, now)
        elif kind == K_WATCH:
            self.watch(endpoint, data, now)
        else:
            self.dropped += 1  # PUNCH, PUNCH_ACK, a server's kind, unknown

    def relay(self, endpoint, data, now):
        room = self.relayed.get(endpoint.key)
        if room is None or room.mode != S_RELAYED or len(data) > self.o.relay_max_size:
            self.dropped += 1
            return
        if endpoint.key == room.host.key:
            target = room.guest
        elif endpoint.key == room.guest.key:
            target = room.host
        else:
            self.dropped += 1
            return
        room.last_datagram = now
        if not room.relay_count.take(now) or not room.relay_bytes.take(now, len(data)):
            self.dropped += 1
            return
        # Out through the socket the target itself talks to: its family, and
        # the local address its router expects an answer from.
        try:
            target.sock.sendto(data, target.raw)
            self.forwarded += 1
        except OSError:
            pass

    def whoami(self, endpoint, data, now):
        source = self.source(endpoint, now)
        if source is None or len(data) != WHOAMI_SIZE or not source.query.take(now):
            self.dropped += 1
            return
        self.send(endpoint, header(K_YOUARE) + endpoint.address())

    def list_rooms(self, endpoint, data, now):
        source = self.source(endpoint, now)
        if source is None or len(data) != LIST_SIZE or any(data[8:16]) or not source.query.take(now):
            self.dropped += 1
            return
        first = HEADER.unpack_from(data)[3]
        # the two-player rooms and the rooms of several, a match's meeting
        # not among them; in the order they were made
        rooms = [r for r in self.rooms.values() if r.ticket is None] + list(self.groups.values())
        rooms.sort(key=lambda r: r.seq)
        total = len(rooms)
        page = []
        # Newest first.
        for index in range(total - 1 - first, max(total - 1 - first - ROOMS_PER_PAGE, -1), -1):
            room = rooms[index]
            locked = room.locked()
            if isinstance(room, Group):
                host = room.host()
                players = len(room.members)
                flags = LIST_GROUP
                player = host.player if host is not None else b""
                rtt = host.rtt if host is not None else 0
            else:
                players = 1 if room.guest is None else 2
                flags = 0
                player = room.host_player
                rtt = room.host_rtt
            page.append(
                b"".join(
                    (
                        pad(room.code, 8),
                        pad(room.name, 24),
                        pad(player, 16),
                        pad(room.setup, 20),
                        # the host's round trip to the server, for the ping
                        struct.pack("<BBH", players, flags | (1 if locked else 0), min(rtt, 0xFFFF)),
                        room.salt if locked else ZERO16,
                        struct.pack("<I", min(int(now - room.created), 0xFFFFFFFF)),
                    )
                )
            )
        self.send(endpoint, header(K_ROOMS, len(page), total) + b"".join(page))

    def hello(self, endpoint, data, now):
        source = self.source(endpoint, now)
        if source is None:
            self.dropped += 1
            return
        if not source.hello.take(now):
            self.rate_limited(endpoint, source, now)
            return
        if len(data) != HELLO_SIZE:
            self.refuse(endpoint, E_BAD_REQUEST)
            return
        if HEADER.unpack_from(data)[3] != RELAY_VERSION:
            self.refuse(endpoint, E_VERSION)
            return
        hello = parse_hello(data)
        if hello is None:
            self.refuse(endpoint, E_BAD_REQUEST)
            return
        if hello.flags & F_HOST:
            self.host_hello(endpoint, source, hello, now)
        else:
            self.guest_hello(endpoint, source, hello, now)

    def ticket_of(self, hello):
        """The match a ticketed HELLO is of, if its ticket is that match's."""
        found = self.match_codes.get(hello.code)
        if found is None or found[1].ended is not None:
            return None
        if not hmac.compare_digest(found[1].ticket, hello.secret[:TICKET_SIZE]) or any(hello.secret[TICKET_SIZE:]):
            return None
        return found[1]

    def host_hello(self, endpoint, source, hello, now):
        room = self.by_host.get(endpoint.key)
        if hello.flags & F_TICKET:
            # a match's host: the meeting is opened under the match's code
            match = self.ticket_of(hello)
            if match is None:
                self.refuse(endpoint, E_NO_ROOM)
                return
            if room is not None and room.code != match.code:
                self.remove(room, "its host meets a room's match")
                room = None
            if room is None:
                if hello.code in self.rooms:
                    self.remove(self.rooms[hello.code], "its match's host came again from elsewhere")
                room = self.create(endpoint, now, code=match.code, counted=False)
                room.ticket = match.ticket
        elif room is not None and room.ticket is not None:
            self.refuse(endpoint, E_BAD_REQUEST)
            return
        if room is None:
            # A new room, whatever code was sent (a lost STATE must not make
            # two rooms: an address that hosts one is given that one).
            if len(self.rooms) >= self.o.max_rooms:
                self.refuse(endpoint, E_SERVER_FULL)
                return
            if self.per_ip[endpoint.ip_key] >= self.o.rooms_per_ip or not source.create.take(now):
                self.rate_limited(endpoint, source, now)
                return
            room = self.create(endpoint, now)
        else:
            room.host = endpoint  # the same address; keep the socket it uses now
        room.take_host(hello, now)
        if hello.flags & F_RESET and room.guest is not None:
            if self.verbose:
                log("room %s: reset by its host" % room.code.decode())
            self.drop_guest(room)
        self.decide(room, now)
        self.state(endpoint, room.mode, room, as_host=True)

    def guest_hello(self, endpoint, source, hello, now):
        if source.blocked_until > now:
            self.rate_limited(endpoint, source, now)
            return
        room = self.rooms.get(hello.code)
        ticketed = hello.flags & F_TICKET
        if ticketed:
            match = self.ticket_of(hello)
            if match is None:
                self.refuse(endpoint, E_NO_ROOM)
                return
            if room is None:
                # the match's host has not opened it yet: it will
                self.state(endpoint, S_WAITING, setup=b"")
                return
        if room is None or (room.ticket is not None and not ticketed):
            self.refuse(endpoint, E_NO_ROOM)
            return
        if endpoint.key == room.host.key:
            self.refuse(endpoint, E_BAD_REQUEST, room)
            return
        if room.guest is not None and room.guest.key == endpoint.key:
            room.guest = endpoint
            room.take_guest(hello, now)
            self.decide(room, now)
            self.state(endpoint, room.mode, room)
            return
        if hello.setup != room.setup:
            self.refuse(endpoint, E_INCOMPATIBLE, room, setup=room.setup)
            return
        if room.guest is not None:
            self.refuse(endpoint, E_ROOM_FULL, room)
            return
        if not ticketed and room.locked() and not self.proved(room, endpoint, source, hello, now):
            return
        self.admit(room, endpoint, hello, now)

    def proved(self, room, endpoint, source, hello, now):
        """A locked room: True if the proof is good, else answered."""
        sent = room.challenges.get(endpoint.key)
        if sent:
            while sent and now - sent[0][1] > self.o.challenge_timeout:
                sent.popleft()
        if sent and hello.secret != ZERO32:
            good = False
            for challenge, _ in sent:
                expected = hmac.new(room.verifier, challenge, hashlib.sha256).digest()
                if hmac.compare_digest(expected, hello.secret):
                    good = True
            if good:
                del room.challenges[endpoint.key]
                return True
            self.wrong_password(source, now)
            self.refuse(endpoint, E_WRONG_PASSWORD, room, salt=room.salt, challenge=sent[-1][0])
            return False
        # No proof, or none to a challenge still current: a fresh challenge.
        challenge = secrets.token_bytes(16)
        if sent is None:
            if len(room.challenges) >= CHALLENGE_ADDRESSES:
                oldest = min(room.challenges, key=lambda k: room.challenges[k][-1][1] if room.challenges[k] else 0)
                del room.challenges[oldest]
            sent = room.challenges[endpoint.key] = collections.deque(maxlen=CHALLENGES_KEPT)
        sent.append((challenge, now))
        self.state(endpoint, S_PASSWORD, room, salt=room.salt, challenge=challenge)
        return False

    def wrong_password(self, source, now):
        wrong = source.wrong
        wrong.append(now)
        while wrong and now - wrong[0] > 60.0:
            wrong.popleft()
        if len(wrong) >= self.o.wrong_passwords_per_min:
            source.blocked_until = now + self.o.wrong_password_block
            wrong.clear()

    # --- room changes ---

    def new_code(self):
        """A code no room, room of several or match has."""
        while True:
            code = "".join(secrets.choice(CODE_ALPHABET) for _ in range(CODE_LENGTH)).encode()
            if code not in self.rooms and code not in self.groups and code not in self.match_codes:
                return code

    def create(self, host, now, code=None, counted=True):
        if code is None:
            code = self.new_code()
        self.seq += 1
        room = Room(code, self.seq, host, now)
        room.counted = counted
        self.rooms[code] = room
        self.by_host[host.key] = room
        if counted:
            # a match of a room of several is the room's, not its host's
            self.per_ip[host.ip_key] += 1
        if self.verbose:
            log("room %s: opened by %s" % (code.decode(), host.text()))
        return room

    def remove(self, room, why):
        self.drop_guest(room)
        del self.rooms[room.code]
        if self.by_host.get(room.host.key) is room:
            del self.by_host[room.host.key]
        key = room.host.ip_key
        if room.counted:
            self.per_ip[key] -= 1
            if self.per_ip[key] <= 0:
                del self.per_ip[key]
        if self.verbose:
            log("room %s: closed (%s)" % (room.code.decode(), why))

    def drop_guest(self, room):
        if room.guest is not None:
            for key in (room.host.key, room.guest.key):
                if self.relayed.get(key) is room:
                    del self.relayed[key]
            if self.guest_of.get(room.guest.key) is room:
                del self.guest_of[room.guest.key]
        room.clear_guest()

    def admit(self, room, guest, hello, now):
        before = self.guest_of.get(guest.key)
        if before is not None and before is not room:
            self.drop_guest(before)  # an address is the guest of one room
        room.guest = guest
        room.take_guest(hello, now)
        room.guest_flags = 0
        room.host_flags = 0
        room.token = 0
        while room.token == 0:
            room.token = secrets.randbits(32)
        room.punch_start = now
        room.mode = S_PUNCH
        self.guest_of[guest.key] = room
        if self.verbose:
            log("room %s: %s joined" % (room.code.decode(), guest.text()))
        self.decide(room, now)
        self.state(guest, room.mode, room)
        # Tell the host now rather than at its next heartbeat: the punch
        # window has begun.
        self.state(room.host, room.mode, room, as_host=True)

    def decide(self, room, now):
        if room.mode != S_PUNCH:
            return
        both = room.host_flags & room.guest_flags
        elapsed = now - room.punch_start
        if self.o.relay_only:
            mode = S_RELAYED
        elif both & F_PUNCHED6:
            mode = S_DIRECT6
        elif both & F_PUNCHED4 and (not (room.host_v6 or room.guest_v6) or elapsed >= self.o.v6_grace):
            mode = S_DIRECT4
        elif elapsed >= self.o.punch_window:
            mode = S_RELAYED
        else:
            return
        room.mode = mode
        room.last_datagram = now
        if mode == S_RELAYED:
            room.relay_count = Bucket(self.o.relay_datagram_rate, self.o.relay_datagram_rate, now)
            room.relay_bytes = Bucket(self.o.relay_byte_rate, self.o.relay_byte_rate, now)
            self.relayed[room.host.key] = room
            self.relayed[room.guest.key] = room
        if self.verbose:
            log("room %s: %s" % (room.code.decode(), MODE_NAMES[mode]))

    # --- rooms of several ---

    def roster(self, endpoint, status, group=None, member=None, stamp=0):
        """A ROSTER: the room as `member` is to see it (a match's ticket is
        given to its two players alone), or a refusal."""
        parts = [header(K_ROSTER, status, stamp)]
        if group is None:
            data = parts[0] + bytes(ROSTER_SIZE - HEADER.size)
            self.send(endpoint, data)
            return
        me = member.id if member is not None else NOBODY

        def ticket_for(match):
            return match.ticket if member is not None and me in (match.a, match.b) else bytes(TICKET_SIZE)

        match = group.match
        parts += [
            pad(group.code, 8),
            pad(group.name, 24),
            member.token if member is not None else bytes(TOKEN_SIZE),
            ZERO16,
            ZERO16,
            struct.pack("<BBBB", me, len(group.members), group.rule, group.host_id),
            pad(group.setup, 20),
        ]
        if match is not None:
            parts += [struct.pack("<IBB2x", match.number, match.a, match.b), pad(match.code, 8), ticket_for(match)]
        else:
            parts += [struct.pack("<IBB2x", 0, NOBODY, NOBODY), bytes(8), bytes(TICKET_SIZE)]
        parts.append(struct.pack("<IBBBx", *group.last))
        for i in range(MEMBERS_MAX):
            if i < len(group.members):
                m = group.members[i]
                flags = (M_PLAYS if m.plays else 0) | (M_BUSY if group.busy(m.id) else 0) | \
                    (M_HOST if m.id == group.host_id else 0)
                parts.append(struct.pack("<BBBxH2x", m.id, flags, min(m.wins, 255), min(m.rtt, 0xFFFF)) +
                             pad(m.player, 16))
            else:
                parts.append(bytes(24))
        for i in range(SIDES_MAX):
            if i < len(group.sides):
                side = group.sides[i]
                parts.append(struct.pack("<BB2xI", side.a, side.b, side.number) + pad(side.code, 8) +
                             ticket_for(side))
            else:
                parts.append(struct.pack("<BB2xI", NOBODY, NOBODY, 0) + bytes(8 + TICKET_SIZE))
        data = b"".join(parts)
        assert len(data) == ROSTER_SIZE
        self.send(endpoint, data)

    def room_hello(self, endpoint, data, now):
        source = self.source(endpoint, now)
        if source is None:
            self.dropped += 1
            return
        if not source.hello.take(now):
            if now - source.limited_at >= 1.0:
                source.limited_at = now
                self.roster(endpoint, E_RATE_LIMITED)
            return
        if len(data) != ROOM_SIZE_HELLO:
            self.roster(endpoint, E_BAD_REQUEST)
            return
        if HEADER.unpack_from(data)[3] != RELAY_VERSION:
            self.roster(endpoint, E_VERSION)
            return
        hello = parse_room_hello(data)
        if hello is None:
            self.roster(endpoint, E_BAD_REQUEST)
            return
        if hello.action == A_OPEN:
            self.group_open(endpoint, source, hello, now)
            return
        group = self.groups.get(hello.code)
        if group is None:
            self.roster(endpoint, E_NO_ROOM, stamp=hello.stamp)
            return
        if hello.action == A_ENTER and hello.token == bytes(TOKEN_SIZE):
            self.group_enter(endpoint, source, group, hello, now)
            return
        member = group.by_token(hello.token)
        if member is None:
            self.roster(endpoint, E_NO_ROOM, stamp=hello.stamp)
            return
        member.endpoint = endpoint
        member.seen = now
        member.rtt = hello.rtt
        member.plays = bool(hello.flags & R_PLAYS)
        action = hello.action
        if action == A_LEAVE:
            self.group_leave(group, member, now, "left")
            self.roster(endpoint, E_NO_ROOM, stamp=hello.stamp)
            return
        if action == A_RULE and member.id == group.host_id:
            group.rule = hello.rule
            if self.verbose:
                log("room %s: the rotation is %s" % (group.code.decode(), ("off", "a fixed turn",
                                                                           "the winner staying")[hello.rule]))
        elif action == A_RESULT:
            self.group_result(group, member, hello.number, hello.arg, now)
        elif action == A_SIDE_OPEN and not group.busy(member.id) and len(group.sides) < SIDES_MAX:
            group.side_number += 1
            side = Match(self.new_code(), group.side_number, member.id, NOBODY, True, now)
            group.sides.append(side)
            self.match_codes[side.code] = (group, side)
            if self.verbose:
                log("room %s: match %d beside it, %s waiting" % (group.code.decode(), side.number,
                                                                 member.player.decode()))
        elif action == A_SIDE_JOIN and not group.busy(member.id) and hello.arg < len(group.sides):
            side = group.sides[hello.arg]
            if side.b == NOBODY:
                side.b = member.id
                side.begun = now
        elif action == A_SIDE_DONE:
            for side in list(group.sides):
                if side.number == hello.number and member.id in (side.a, side.b):
                    self.match_over(group, side, NOBODY, now)
        self.pick(group, now)
        self.roster(endpoint, 1, group, member, hello.stamp)

    def group_open(self, endpoint, source, hello, now):
        group = self.group_of.get(endpoint.key)
        if group is not None and group.code in self.groups and hello.token == bytes(TOKEN_SIZE):
            # the ROSTER that answered the opening was lost: the same room
            host = group.host()
            if host is not None and host.endpoint.key == endpoint.key:
                self.roster(endpoint, 1, group, host, hello.stamp)
                return
        if len(self.rooms) + len(self.groups) >= self.o.max_rooms:
            self.roster(endpoint, E_SERVER_FULL, stamp=hello.stamp)
            return
        if self.per_ip[endpoint.ip_key] >= self.o.rooms_per_ip or not source.create.take(now):
            self.roster(endpoint, E_RATE_LIMITED, stamp=hello.stamp)
            return
        self.seq += 1
        group = Group(self.new_code(), self.seq, hello, now)
        host = Member(group.next_id, hello.player, bool(hello.flags & R_PLAYS), endpoint, now)
        host.rtt = hello.rtt
        group.next_id += 1
        group.members.append(host)
        group.queue.append(host.id)
        group.host_id = host.id
        self.groups[group.code] = group
        self.group_of[endpoint.key] = group
        self.per_ip[endpoint.ip_key] += 1
        if self.verbose:
            log("room %s: a room of several opened by %s" % (group.code.decode(), endpoint.text()))
        self.roster(endpoint, 1, group, host, hello.stamp)

    def group_enter(self, endpoint, source, group, hello, now):
        if source.blocked_until > now:
            self.roster(endpoint, E_RATE_LIMITED, stamp=hello.stamp)
            return
        for m in group.members:
            if m.endpoint.key == endpoint.key:
                # the ROSTER that let this one in was lost
                self.roster(endpoint, 1, group, m, hello.stamp)
                return
        if hello.setup != group.setup:
            self.send(endpoint, header(K_ROSTER, E_INCOMPATIBLE, hello.stamp) + pad(group.code, 8) +
                      pad(group.name, 24) + bytes(TOKEN_SIZE + 32 + 4) + pad(group.setup, 20) +
                      bytes(ROSTER_SIZE - 112))
            return
        if len(group.members) >= MEMBERS_MAX:
            self.roster(endpoint, E_ROOM_FULL, stamp=hello.stamp)
            return
        if group.locked() and not self.group_proved(group, endpoint, source, hello, now):
            return
        member = Member(group.next_id, hello.player, bool(hello.flags & R_PLAYS), endpoint, now)
        member.rtt = hello.rtt
        group.next_id += 1
        group.members.append(member)
        group.queue.append(member.id)
        group.challenges.pop(endpoint.key, None)
        if self.verbose:
            log("room %s: %s came in (%d members)" % (group.code.decode(), endpoint.text(), len(group.members)))
        self.pick(group, now)
        self.roster(endpoint, 1, group, member, hello.stamp)

    def group_proved(self, group, endpoint, source, hello, now):
        """A locked room of several: True if the proof is good, else answered
        (as a two-player room's, proved)."""
        sent = group.challenges.get(endpoint.key)
        if sent:
            while sent and now - sent[0][1] > self.o.challenge_timeout:
                sent.popleft()
        if sent and hello.secret != ZERO32:
            for challenge, _ in sent:
                expected = hmac.new(group.verifier, challenge, hashlib.sha256).digest()
                if hmac.compare_digest(expected, hello.secret):
                    return True
            self.wrong_password(source, now)
            self.roster_password(endpoint, E_WRONG_PASSWORD, group, hello.stamp, sent[-1][0])
            return False
        challenge = secrets.token_bytes(16)
        if sent is None:
            if len(group.challenges) >= CHALLENGE_ADDRESSES:
                group.challenges.pop(next(iter(group.challenges)))
            sent = group.challenges[endpoint.key] = collections.deque(maxlen=CHALLENGES_KEPT)
        sent.append((challenge, now))
        self.roster_password(endpoint, S_PASSWORD, group, hello.stamp, challenge)
        return False

    def roster_password(self, endpoint, status, group, stamp, challenge):
        """PASSWORD or WRONG_PASSWORD: the room's salt and a challenge, and
        nothing of its members."""
        self.send(endpoint, header(K_ROSTER, status, stamp) + pad(group.code, 8) + pad(group.name, 24) +
                  bytes(TOKEN_SIZE) + group.salt + challenge + bytes(ROSTER_SIZE - 88))

    def group_leave(self, group, member, now, why):
        # out of the rotation first, so that the next match is not its
        group.members.remove(member)
        if member.id in group.queue:
            group.queue.remove(member.id)
        for match in group.matches():
            if member.id in (match.a, match.b):
                # the one who stays wins a match left in the middle
                if match.side:
                    self.match_over(group, match, NOBODY, now)
                else:
                    self.match_over(group, match, W_SECOND if member.id == match.a else W_FIRST, now)
        if self.verbose:
            log("room %s: a member %s (%d left)" % (group.code.decode(), why, len(group.members)))
        if not group.members:
            self.group_remove(group, "nobody left")
            return
        if group.host_id == member.id:
            group.host_id = group.members[0].id
        self.pick(group, now)

    def group_remove(self, group, why):
        for match in group.matches() + group.ended:
            self.match_codes.pop(match.code, None)
            if match.code in self.rooms:
                self.remove(self.rooms[match.code], "its room of several closed")
        del self.groups[group.code]
        for key in [k for k, g in self.group_of.items() if g is group]:
            del self.group_of[key]
            ip_key = (key[0], key[1] if key[0] == 4 else key[1][:8])
            self.per_ip[ip_key] -= 1
            if self.per_ip[ip_key] <= 0:
                del self.per_ip[ip_key]
        if self.verbose:
            log("room %s: closed (%s)" % (group.code.decode(), why))

    def pick(self, group, now):
        """The rotation's next match, where it is on and two players are free:
        the first two free in the rotation's order."""
        if group.rule == RULE_OFF or group.match is not None:
            return
        free = [i for i in group.queue if group.member(i) is not None and group.member(i).plays and
                not group.busy(i)]
        if len(free) < 2:
            return
        group.number += 1
        group.match = Match(self.new_code(), group.number, free[0], free[1], False, now)
        self.match_codes[group.match.code] = (group, group.match)
        if self.verbose:
            log("room %s: match %d, members %d and %d" % (group.code.decode(), group.number, free[0], free[1]))

    def group_result(self, group, member, number, winner, now):
        for match in group.matches():
            if match.number == number and member.id in (match.a, match.b) and winner <= W_DRAW:
                self.match_over(group, match, winner, now)
                return

    def match_over(self, group, match, winner, now):
        """A match is over: its score, the rotation's order, its feed kept a
        while for those still watching it."""
        match.ended = now
        if match.side:
            group.sides.remove(match)
        else:
            group.match = None
            group.last = (match.number, match.a, match.b, winner)
            a, b = match.a, match.b
            queue = group.queue
            if winner in (W_FIRST, W_SECOND):
                won, lost = (a, b) if winner == W_FIRST else (b, a)
                if group.member(won) is not None:
                    group.member(won).wins += 1
            if group.rule == RULE_WINNER and winner in (W_FIRST, W_SECOND):
                # the winner stays, first; the loser to the end
                rest = [i for i in queue if i not in (a, b)]
                group.queue = ([won] if won in queue else []) + rest + ([lost] if lost in queue else [])
            elif group.rule == RULE_WINNER:
                rest = [i for i in queue if i not in (a, b)]
                group.queue = rest + [i for i in (a, b) if i in queue]
            elif a in queue:
                # a fixed turn: the first of the match to the end
                queue.remove(a)
                queue.append(a)
        group.ended.append(match)
        if self.verbose:
            log("room %s: match %d over (%s)" % (group.code.decode(), match.number,
                                                 {W_FIRST: "the first won", W_SECOND: "the second won",
                                                  W_DRAW: "a draw"}.get(winner, "left")))
        self.pick(group, now)

    def match_of(self, data):
        """The match a FEED is of, by its code and ticket."""
        found = self.match_codes.get(bytes(data[8:16]).rstrip(b"\0"))
        if found is None or not hmac.compare_digest(found[1].ticket, bytes(data[16:32])):
            return None
        return found[1]

    def feed(self, endpoint, data, now):
        source = self.source(endpoint, now)
        if source is None or len(data) < 40 or not source.watch.take(now):
            self.dropped += 1
            return
        match = self.match_of(data)
        if match is None:
            self.dropped += 1
            return
        feed = match.feed
        part = data[3]
        if part == P_SETTINGS and len(data) == 32 + SETTINGS_SIZE:
            feed.settings = bytes(data[32:])
        elif part == P_START and len(data) > 44:
            start_frame, size, index, count = struct.unpack_from("<IIHH", data, 32)
            if size == 0 or size > CHUNKS_MAX * CHUNK or count != (size + CHUNK - 1) // CHUNK or index >= count:
                self.dropped += 1
                return
            if start_frame != feed.start_frame or size != feed.start_size:
                feed.start_frame = start_frame
                feed.start_size = size
                feed.start = bytearray(size)
                feed.chunks = set()
                if feed.inputs_from is None:
                    feed.inputs_from = start_frame
            piece = data[44:44 + CHUNK]
            if len(piece) != min(CHUNK, size - index * CHUNK):
                self.dropped += 1
                return
            feed.start[index * CHUNK:index * CHUNK + len(piece)] = piece
            feed.chunks.add(index)
        elif part == P_INPUTS and len(data) >= 40:
            first, count, hashes = struct.unpack_from("<IHB", data, 32)
            at = 40 + hashes * 12
            if hashes > HASHES_A_DATAGRAM or count > INPUTS_A_DATAGRAM or len(data) != at + count * INPUT_SIZE:
                self.dropped += 1
                return
            for i in range(hashes):
                frame = struct.unpack_from("<I", data, 40 + i * 12)[0]
                feed.hashes[frame] = bytes(data[44 + i * 12:52 + i * 12])
            while len(feed.hashes) > FRAMES_MAX // 30:
                feed.hashes.popitem(last=False)
            if feed.inputs_from is not None:
                known = feed.next_frame()
                if first <= known < first + count and known - feed.inputs_from < FRAMES_MAX:
                    feed.inputs += data[at + (known - first) * INPUT_SIZE:]
        elif part == P_END and len(data) >= 37:
            feed.end, feed.winner = struct.unpack_from("<IB", data, 32)
        else:
            self.dropped += 1
            return
        bitmap = bytearray(CHUNK_MAP)
        for index in feed.chunks:
            bitmap[index // 8] |= 1 << (index % 8)
        self.send(endpoint, header(K_FEED_ACK) + pad(match.code, 8) +
                  struct.pack("<IBB2xI", feed.start_frame, feed.settings is not None, feed.end is not None,
                              feed.next_frame()) + bytes(bitmap) + bytes(4))

    def watch(self, endpoint, data, now):
        """A spectator's ask: what it lacks of the match's feed."""
        source = self.source(endpoint, now)
        if source is None or len(data) != WATCH_SIZE or not source.watch.take(now):
            self.dropped += 1
            return
        code = bytes(data[32:40]).rstrip(b"\0")
        group = self.groups.get(bytes(data[8:16]).rstrip(b"\0"))
        member = group.by_token(bytes(data[16:32])) if group is not None else None
        if member is None:
            self.send(endpoint, header(K_SCENE, P_NONE) + pad(code, 8) + struct.pack("<B7x", NONE_NOT_MEMBER))
            return
        member.seen = now
        match = None
        for m in group.matches() + group.ended:
            if m.code == code:
                match = m
        if match is None:
            self.send(endpoint, header(K_SCENE, P_NONE) + pad(code, 8) + struct.pack("<B7x", NONE_NO_MATCH))
            return
        feed = match.feed
        has_start, wants, flags = struct.unpack_from("<IIB", data, 40)
        if feed.settings is not None and not flags & 1:
            names = [group.member(i) for i in (match.a, match.b)]
            self.send(endpoint, header(K_SCENE, P_SETTINGS) + pad(code, 8) + feed.settings +
                      b"".join(pad(m.player if m else b"", 16) for m in names))
        if not feed.start_whole():
            if feed.end is None:
                self.send(endpoint, header(K_SCENE, P_NONE) + pad(code, 8) + struct.pack("<B7x", NONE_NOTHING_YET))
        elif wants == NO_START:
            # the start, the chunks it does not have (all of them, where it
            # has another's: the newest fight's start is the way in)
            bitmap = data[56:56 + CHUNK_MAP] if has_start == feed.start_frame else bytes(CHUNK_MAP)
            count = (feed.start_size + CHUNK - 1) // CHUNK
            sent = 0
            for index in range(count):
                if bitmap[index // 8] >> (index % 8) & 1:
                    continue
                self.send(endpoint, header(K_SCENE, P_START) + pad(code, 8) +
                          struct.pack("<IIHH", feed.start_frame, feed.start_size, index, count) +
                          bytes(feed.start[index * CHUNK:(index + 1) * CHUNK]))
                sent += 1
                if sent >= CHUNKS_A_WATCH:
                    break
        elif wants != NO_START and feed.inputs_from is not None:
            # the inputs from what it wants, with the hashes among them
            for _ in range(2):
                known = feed.next_frame()
                if wants < feed.inputs_from or wants >= known:
                    break
                count = min(INPUTS_A_DATAGRAM, known - wants)
                at = (wants - feed.inputs_from) * INPUT_SIZE
                hashes = [(f, h) for f, h in feed.hashes.items() if wants <= f < wants + count][:HASHES_A_DATAGRAM]
                self.send(endpoint, header(K_SCENE, P_INPUTS) + pad(code, 8) +
                          struct.pack("<IHBx", wants, count, len(hashes)) +
                          b"".join(struct.pack("<I", f) + h for f, h in hashes) +
                          bytes(feed.inputs[at:at + count * INPUT_SIZE]))
                wants += count
        if feed.end is not None:
            self.send(endpoint, header(K_SCENE, P_END) + pad(code, 8) + struct.pack("<IB3x", feed.end, feed.winner))

    def group_tick(self, now):
        for group in list(self.groups.values()):
            for m in list(group.members):
                if now - m.seen > self.o.member_timeout:
                    self.group_leave(group, m, now, "was not heard from")
                    if group.code not in self.groups:
                        break
            if group.code not in self.groups:
                continue
            for match in group.matches():
                if match.side and match.b == NOBODY and now - match.begun > self.o.side_timeout:
                    self.match_over(group, match, NOBODY, now)
            for match in list(group.ended):
                if now - match.ended > self.o.feed_linger:
                    group.ended.remove(match)
                    self.match_codes.pop(match.code, None)

    # --- time ---

    def tick(self, now):
        self.group_tick(now)
        for room in list(self.rooms.values()):
            self.decide(room, now)
            if room.mode in (S_WAITING, S_PUNCH):
                if now - room.host_hello > self.o.waiting_timeout:
                    self.remove(room, "host gone")
            elif room.mode == S_RELAYED:
                if now - room.last_datagram > self.o.relayed_timeout:
                    self.remove(room, "relay idle")
            elif now - room.last_hello > self.o.direct_timeout:
                self.remove(room, "direct, done")
        for room in self.rooms.values():
            for key in [k for k, sent in room.challenges.items() if not sent or now - sent[-1][1] > self.o.challenge_timeout]:
                del room.challenges[key]
        if now - self.stats_at >= self.o.stats_interval:
            self.forget_sources(now)
            span = now - self.stats_at
            log(
                "rooms %d, relayed %d, in %.1f/s, out %.1f/s, forwarded %.1f/s, dropped %.1f/s; rooms of several %d, "
                "members %d, matches %d"
                % (
                    len(self.rooms),
                    sum(1 for r in self.rooms.values() if r.mode == S_RELAYED),
                    self.received / span,
                    self.sent / span,
                    self.forwarded / span,
                    self.dropped / span,
                    len(self.groups),
                    sum(len(g.members) for g in self.groups.values()),
                    sum(len(g.matches()) for g in self.groups.values()),
                )
            )
            self.received = self.sent = self.forwarded = self.dropped = 0
            self.stats_at = now

    def forget_sources(self, now):
        idle = max(120.0, self.o.wrong_password_block + 60.0)
        for key in [k for k, s in self.sources.items() if now - s.seen > idle and s.blocked_until <= now]:
            del self.sources[key]


ERROR_NAMES = {
    E_NO_ROOM: "NO_ROOM",
    E_ROOM_FULL: "ROOM_FULL",
    E_SERVER_FULL: "SERVER_FULL",
    E_VERSION: "VERSION",
    E_WRONG_PASSWORD: "WRONG_PASSWORD",
    E_RATE_LIMITED: "RATE_LIMITED",
    E_BAD_REQUEST: "BAD_REQUEST",
    E_INCOMPATIBLE: "INCOMPATIBLE",
}


# --- sockets and the loop --------------------------------------------------


def parse_listen(text):
    """ADDR:PORT or [V6ADDR]:PORT -> (family, address, port)."""
    if text.startswith("["):
        address, sep, port = text[1:].partition("]:")
        family = socket.AF_INET6
    else:
        address, sep, port = text.rpartition(":")
        family = socket.AF_INET
    if not sep:
        raise argparse.ArgumentTypeError("%r is not ADDR:PORT or [ADDR]:PORT" % text)
    try:
        socket.inet_pton(family, address)
        port = int(port)
    except (OSError, ValueError):
        raise argparse.ArgumentTypeError("%r is not ADDR:PORT or [ADDR]:PORT" % text)
    if not 0 <= port <= 65535:
        raise argparse.ArgumentTypeError("port out of range in %r" % text)
    return family, address, port


def open_socket(family, address, port, v6_only):
    sock = socket.socket(family, socket.SOCK_DGRAM)
    try:
        if family == socket.AF_INET6:
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 1 if v6_only else 0)
        if hasattr(socket, "SIO_UDP_CONNRESET"):
            # Windows: an ICMP port unreachable must not fail the next recvfrom.
            sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        sock.bind((address, port))
        sock.setblocking(False)
    except OSError:
        sock.close()
        raise
    return sock


def open_sockets(listens):
    explicit = bool(listens)
    if not explicit:
        listens = [(socket.AF_INET, "0.0.0.0", DEFAULT_PORT)]
        if socket.has_ipv6:
            listens.append((socket.AF_INET6, "::", DEFAULT_PORT))
    v6_only = any(f == socket.AF_INET for f, _, _ in listens)
    sockets = []
    for family, address, port in listens:
        try:
            sockets.append(open_socket(family, address, port, v6_only))
        except OSError as error:
            if not explicit and family == socket.AF_INET6:
                log("no IPv6 (%s): IPv4 only" % error)
                continue
            for sock in sockets:
                sock.close()
            raise SystemExit("cannot listen on %s port %d: %s" % (address, port, error))
    return sockets


def bound_text(sock):
    name = sock.getsockname()
    if sock.family == socket.AF_INET6:
        return "[%s]:%d" % (name[0], name[1])
    return "%s:%d" % (name[0], name[1])


def arguments(argv):
    p = argparse.ArgumentParser(description="ReBushido lobby, rendezvous and relay server (UDP).")
    p.add_argument(
        "--listen",
        action="append",
        type=parse_listen,
        default=[],
        metavar="ADDR:PORT",
        help="where to listen, repeatable; IPv6 as [::]:7845 (default 0.0.0.0:7845 and [::]:7845)",
    )
    p.add_argument("--punch-window", type=float, default=3.0, metavar="S", help="seconds of punching before a room is relayed")
    p.add_argument("--v6-grace", type=float, default=1.0, metavar="S", help="seconds DIRECT4 waits for IPv6 when a side sent an IPv6 candidate")
    p.add_argument("--relay-only", action="store_true", help="relay every room at once, no punching")
    p.add_argument("--verbose", action="store_true", help="log rooms, addresses and refusals (otherwise counts only)")
    p.add_argument("--max-rooms", type=int, default=500, help="rooms in all (then SERVER_FULL)")
    p.add_argument("--rooms-per-ip", type=int, default=3, help="rooms hosted from one IP")
    p.add_argument("--creations-per-min", type=float, default=6, help="rooms created per IP a minute")
    p.add_argument("--hello-rate", type=float, default=30, help="HELLOs a second per IP")
    p.add_argument("--query-rate", type=float, default=10, help="LISTs and WHOAMIs a second per IP")
    p.add_argument("--wrong-passwords-per-min", type=int, default=5, help="wrong passwords per IP a minute before it is blocked")
    p.add_argument("--wrong-password-block", type=float, default=60.0, metavar="S", help="seconds an IP's guest HELLOs are refused after that")
    p.add_argument("--relay-datagram-rate", type=float, default=240, help="relayed datagrams a second per room")
    p.add_argument("--relay-byte-rate", type=float, default=300000, help="relayed bytes a second per room")
    p.add_argument("--relay-max-size", type=int, default=1200, help="largest datagram relayed, bytes")
    p.add_argument("--waiting-timeout", type=float, default=20.0, metavar="S", help="a waiting or punching room's life after its host's last HELLO")
    p.add_argument("--direct-timeout", type=float, default=30.0, metavar="S", help="a direct room's life after its last HELLO")
    p.add_argument("--relayed-timeout", type=float, default=60.0, metavar="S", help="a relayed room's life after its last datagram")
    p.add_argument("--challenge-timeout", type=float, default=30.0, metavar="S", help="how long a password challenge holds")
    p.add_argument("--member-timeout", type=float, default=20.0, metavar="S",
                   help="a room of several's member's life after it was last heard from")
    p.add_argument("--side-timeout", type=float, default=120.0, metavar="S",
                   help="how long a match beside a room waits for its second player")
    p.add_argument("--feed-linger", type=float, default=30.0, metavar="S",
                   help="how long a match's feed is kept for its spectators after it ends")
    p.add_argument("--watch-rate", type=float, default=400, help="WATCHes and FEEDs a second per IP")
    p.add_argument("--max-sources", type=int, default=100000, help="source IPs tracked for the limits")
    p.add_argument("--stats-interval", type=float, default=60.0, metavar="S", help="seconds between count lines")
    options = p.parse_args(argv)
    for name in ("max_rooms", "rooms_per_ip", "wrong_passwords_per_min", "max_sources"):
        if getattr(options, name) < 1:
            p.error("--%s must be at least 1" % name.replace("_", "-"))
    for name in ("creations_per_min", "hello_rate", "query_rate", "relay_datagram_rate", "relay_byte_rate", "stats_interval",
                 "watch_rate"):
        if getattr(options, name) <= 0:
            p.error("--%s must be above 0" % name.replace("_", "-"))
    return options


def main(argv=None):
    options = arguments(sys.argv[1:] if argv is None else argv)
    sockets = open_sockets(options.listen)
    server = Server(options, sockets)
    stopping = []

    def stop(signum, frame):
        stopping.append(signum)

    for name in ("SIGINT", "SIGTERM", "SIGBREAK"):
        if hasattr(signal, name):
            signal.signal(getattr(signal, name), stop)
    log("rebushido-relay %d listening on %s" % (RELAY_VERSION, ", ".join(bound_text(s) for s in sockets)))
    tick_every = 0.1
    next_tick = time.monotonic() + tick_every
    try:
        while not stopping:
            try:
                ready, _, _ = select.select(sockets, [], [], tick_every)
            except InterruptedError:
                continue
            except OSError:
                if stopping:
                    break
                raise
            now = time.monotonic()
            for sock in ready:
                for _ in range(256):
                    try:
                        data, raw = sock.recvfrom(65536)
                    except (BlockingIOError, InterruptedError):
                        break
                    except OSError:
                        continue  # an ICMP error reported on a UDP socket
                    server.datagram(sock, data, raw, now)
            if now >= next_tick:
                server.tick(now)
                next_tick = now + tick_every
    except KeyboardInterrupt:
        pass
    for sock in sockets:
        sock.close()
    log("stopped: %d rooms dropped, %d rooms of several" % (len(server.rooms), len(server.groups)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
