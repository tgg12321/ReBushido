#!/usr/bin/env python3
"""The ReBushido lobby, rendezvous and relay server (protocol version 1).

One process, UDP, the Python 3 standard library alone (3.8 or newer). It
keeps a list of open rooms, introduces a room's host and guest to each other
so they can punch through their routers, and passes their game traffic on
when they cannot. It holds no game data, reads no disc and writes nothing to
disk; all it keeps, in memory, is each room's code, names, setup code, the
salt and verifier of its password, and the two addresses.

The wire format is docs/NETPLAY.md, section 14; tools/relay/README.md says
how to run and deploy it.

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

RELAY_VERSION = 1
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
K_CONTROL = 48  # kinds 48..63 are the server's; below is game traffic
K_CONTROL_END = 64

# HELLO detail bits.
F_HOST = 0x01
F_GUEST = 0x02
F_RESET = 0x04
F_PUNCHED4 = 0x08
F_PUNCHED6 = 0x10
F_ALL = F_HOST | F_GUEST | F_RESET | F_PUNCHED4 | F_PUNCHED6

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
    )


def parse_hello(data):
    """The fields of a 208-byte version-1 HELLO, or None if malformed."""
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
    if count > MAX_CANDIDATES or any(data[125:128]):
        return None
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
    if role == F_HOST and not hello.name:
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

    __slots__ = ("hello", "query", "create", "wrong", "blocked_until", "limited_at", "seen")

    def __init__(self, limits, now):
        self.hello = Bucket(limits.hello_rate, limits.hello_rate, now)
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
    )

    def __init__(self, code, seq, host, now):
        self.code = code
        self.seq = seq
        self.created = now
        self.host = host
        self.host_hello = now
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

    def take_guest(self, hello, now):
        self.guest_player = hello.player
        self.guest_setup = hello.setup
        self.guest_candidates = hello.candidates
        self.guest_v6 = hello.has_v6
        self.guest_flags = hello.flags
        self.last_hello = now
        self.last_datagram = now


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
        rooms = list(self.rooms.values())
        total = len(rooms)
        page = []
        # Newest first: the dict keeps creation order.
        for index in range(total - 1 - first, max(total - 1 - first - ROOMS_PER_PAGE, -1), -1):
            room = rooms[index]
            locked = room.locked()
            page.append(
                b"".join(
                    (
                        pad(room.code, 8),
                        pad(room.name, 24),
                        pad(room.host_player, 16),
                        pad(room.setup, 20),
                        struct.pack("<BB2x", 1 if room.guest is None else 2, 1 if locked else 0),
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

    def host_hello(self, endpoint, source, hello, now):
        room = self.by_host.get(endpoint.key)
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
        if room is None:
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
        if room.locked() and not self.proved(room, endpoint, source, hello, now):
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

    def create(self, host, now):
        while True:
            code = "".join(secrets.choice(CODE_ALPHABET) for _ in range(CODE_LENGTH)).encode()
            if code not in self.rooms:
                break
        self.seq += 1
        room = Room(code, self.seq, host, now)
        self.rooms[code] = room
        self.by_host[host.key] = room
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

    # --- time ---

    def tick(self, now):
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
                "rooms %d, relayed %d, in %.1f/s, out %.1f/s, forwarded %.1f/s, dropped %.1f/s"
                % (
                    len(self.rooms),
                    sum(1 for r in self.rooms.values() if r.mode == S_RELAYED),
                    self.received / span,
                    self.sent / span,
                    self.forwarded / span,
                    self.dropped / span,
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
    p.add_argument("--max-sources", type=int, default=100000, help="source IPs tracked for the limits")
    p.add_argument("--stats-interval", type=float, default=60.0, metavar="S", help="seconds between count lines")
    options = p.parse_args(argv)
    for name in ("max_rooms", "rooms_per_ip", "wrong_passwords_per_min", "max_sources"):
        if getattr(options, name) < 1:
            p.error("--%s must be at least 1" % name.replace("_", "-"))
    for name in ("creations_per_min", "hello_rate", "query_rate", "relay_datagram_rate", "relay_byte_rate", "stats_interval"):
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
    log("stopped: %d rooms dropped" % len(server.rooms))
    return 0


if __name__ == "__main__":
    sys.exit(main())
