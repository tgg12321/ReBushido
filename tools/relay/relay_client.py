"""The client side of the ReBushido relay protocol, for tests and tools.

Builds HELLO, LIST and WHOAMI datagrams and reads STATE, ROOMS and YOUARE
(docs/NETPLAY.md, section 14). Standard library only.

    import relay_client as rc
    v = rc.verifier(salt, "password")
    sock.sendto(rc.hello(rc.F_HOST, name="Dojo", player="Mikado", setup="P5-7Q2K-M9XD-0000",
                         salt=salt, secret=v), server)
    state = rc.parse_state(sock.recv(2048))
"""

import hashlib
import hmac
import socket
import struct

RELAY_VERSION = 2
HEADER = struct.Struct("<2sBBI")

K_HELLO = 48
K_STATE = 49
K_PUNCH = 50
K_PUNCH_ACK = 51
K_LIST = 52
K_ROOMS = 53
K_WHOAMI = 55
K_YOUARE = 56

F_HOST = 0x01
F_GUEST = 0x02
F_RESET = 0x04
F_PUNCHED4 = 0x08
F_PUNCHED6 = 0x10
F_TICKET = 0x20

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

STATE_NAMES = {
    S_WAITING: "WAITING",
    S_PASSWORD: "PASSWORD",
    S_PUNCH: "PUNCH",
    S_DIRECT4: "DIRECT4",
    S_DIRECT6: "DIRECT6",
    S_RELAYED: "RELAYED",
    E_NO_ROOM: "NO_ROOM",
    E_ROOM_FULL: "ROOM_FULL",
    E_SERVER_FULL: "SERVER_FULL",
    E_VERSION: "VERSION",
    E_WRONG_PASSWORD: "WRONG_PASSWORD",
    E_RATE_LIMITED: "RATE_LIMITED",
    E_BAD_REQUEST: "BAD_REQUEST",
    E_INCOMPATIBLE: "INCOMPATIBLE",
}

C_PUBLIC = 0
C_LAN4 = 1
C_MAPPED4 = 2
C_GLOBAL6 = 3


def header(kind, detail=0, number=0):
    return HEADER.pack(b"RB", kind, detail, number)


def field(text, size):
    """A zero-padded string field (str or bytes)."""
    if isinstance(text, str):
        text = text.encode("ascii")
    return text[:size].ljust(size, b"\0")


def ip_bytes(ip):
    """16 address bytes: IPv4 then 12 zeros, or the IPv6 address."""
    if ":" in ip:
        return socket.inet_pton(socket.AF_INET6, ip)
    return socket.inet_aton(ip) + bytes(12)


def candidate(ctype, ip, port):
    """A 20-byte CANDIDATE a client sends (types 1, 2, 3)."""
    return struct.pack("<BBH", ctype, 0, port) + ip_bytes(ip)


def verifier(salt, password):
    """SHA-256(salt || password): what a host sends as its secret."""
    if isinstance(password, str):
        password = password.encode("utf-8")
    return hashlib.sha256(bytes(salt) + password).digest()


def proof(verifier_bytes, challenge):
    """HMAC-SHA256(key = verifier, message = challenge): a guest's secret."""
    return hmac.new(verifier_bytes, challenge, hashlib.sha256).digest()


def hello(detail, code=b"", name=b"", player=b"", setup=b"", salt=b"", secret=b"", candidates=(), version=RELAY_VERSION,
          rtt=0):
    """A 208-byte HELLO; `rtt` the sender's round trip to the server."""
    candidates = list(candidates)
    data = b"".join(
        (
            header(K_HELLO, detail, version),
            field(code, 8),
            field(name, 24),
            field(player, 16),
            field(setup, 20),
            field(salt, 16),
            field(secret, 32),
            struct.pack("<BxH", len(candidates), rtt),
            b"".join(candidates).ljust(80, b"\0"),
        )
    )
    assert len(data) == 208
    return data


def list_rooms(first=0):
    return header(K_LIST, 0, first) + bytes(8)


def whoami():
    return header(K_WHOAMI) + bytes(8)


def parse_header(data):
    magic, kind, detail, number = HEADER.unpack_from(data)
    if magic != b"RB":
        raise ValueError("not a ReBushido datagram")
    return kind, detail, number


def text(raw):
    return raw.split(b"\0", 1)[0].decode("ascii")


def parse_address(raw):
    """A 20-byte ADDRESS -> (family, ip text or None, port)."""
    family, _, port = struct.unpack_from("<BBH", raw)
    if family == 4:
        return 4, socket.inet_ntoa(raw[4:8]), port
    if family == 6:
        return 6, socket.inet_ntop(socket.AF_INET6, raw[4:20]), port
    return 0, None, port


def parse_candidate(raw, public_family=4):
    """A 20-byte CANDIDATE -> (type, ip text, port). A PUBLIC one's family is the peer ADDRESS's."""
    ctype, _, port = struct.unpack_from("<BBH", raw)
    v6 = ctype == C_GLOBAL6 or (ctype == C_PUBLIC and public_family == 6)
    if v6:
        return ctype, socket.inet_ntop(socket.AF_INET6, raw[4:20]), port
    return ctype, socket.inet_ntoa(raw[4:8]), port


def parse_state(data):
    if len(data) != 208:
        raise ValueError("STATE of %d bytes" % len(data))
    kind, detail, number = parse_header(data)
    if kind != K_STATE:
        raise ValueError("kind %d, not STATE" % kind)
    peer = parse_address(data[36:56])
    count = data[88]
    return {
        "detail": detail,
        "name": STATE_NAMES.get(detail, str(detail)),
        "token": number,
        "code": text(data[8:16]),
        "you": parse_address(data[16:36]),
        "peer": peer,
        "salt": data[56:72],
        "challenge": data[72:88],
        "candidates": [parse_candidate(data[92 + i * 20 : 112 + i * 20], peer[0]) for i in range(min(count, 4))],
        "player": text(data[172:188]),
        "setup": text(data[188:208]),
    }


def parse_rooms(data):
    kind, count, total = parse_header(data)
    if kind != K_ROOMS or len(data) != 8 + count * 92:
        raise ValueError("not a ROOMS datagram")
    rooms = []
    for i in range(count):
        r = data[8 + i * 92 : 100 + i * 92]
        rooms.append(
            {
                "code": text(r[0:8]),
                "name": text(r[8:32]),
                "player": text(r[32:48]),
                "setup": text(r[48:68]),
                "players": r[68],
                "locked": r[69] & 1,
                "group": bool(r[69] & 2),
                "ping": struct.unpack_from("<H", r, 70)[0],
                "salt": r[72:88],
                "age": struct.unpack_from("<I", r, 88)[0],
            }
        )
    return {"total": total, "rooms": rooms}


def parse_youare(data):
    kind, _, _ = parse_header(data)
    if kind != K_YOUARE or len(data) != 28:
        raise ValueError("not a YOUARE datagram")
    return parse_address(data[8:28])


# --- rooms of several (docs/NETPLAY.md, section 16) ---------------------------

K_ROOM = 57
K_ROSTER = 58
K_FEED = 59
K_FEED_ACK = 60
K_WATCH = 61
K_SCENE = 62

A_OPEN = 1
A_ENTER = 2
A_STAY = 3
A_LEAVE = 4
A_RULE = 5
A_RESULT = 6
A_SIDE_OPEN = 7
A_SIDE_JOIN = 8
A_SIDE_DONE = 9
R_PLAYS = 0x01
RULE_OFF = 0
RULE_FIXED = 1
RULE_WINNER = 2
W_FIRST = 0
W_SECOND = 1
W_DRAW = 2
M_PLAYS = 0x01
M_BUSY = 0x02
M_HOST = 0x04
NOBODY = 0xFF
P_SETTINGS = 1
P_START = 2
P_INPUTS = 3
P_END = 4
P_NONE = 5
NO_START = 0xFFFFFFFF
CHUNK = 1024


def room_hello(action, code=b"", name=b"", player=b"", setup=b"", salt=b"", secret=b"", token=b"", plays=True,
               rule=0, arg=0, stamp=0, rtt=0, number=0, version=RELAY_VERSION):
    """A 160-byte ROOM."""
    data = b"".join(
        (
            header(K_ROOM, action, version),
            field(code, 8),
            field(name, 24),
            field(player, 16),
            field(setup, 20),
            field(salt, 16),
            field(secret, 32),
            field(token, 16),
            struct.pack("<BBBxIHxxI4x", R_PLAYS if plays else 0, rule, arg, stamp, rtt, number),
        )
    )
    assert len(data) == 160
    return data


def parse_roster(data):
    if len(data) != 472:
        raise ValueError("ROSTER of %d bytes" % len(data))
    kind, status, stamp = parse_header(data)
    if kind != K_ROSTER:
        raise ValueError("kind %d, not ROSTER" % kind)
    you, count, rule, host = data[88:92]
    number, a, b = struct.unpack_from("<IBB", data, 112)
    last = struct.unpack_from("<IBBB", data, 144)
    members = []
    for i in range(min(count, 8)):
        r = data[152 + i * 24:176 + i * 24]
        members.append({"id": r[0], "flags": r[1], "wins": r[2], "rtt": struct.unpack_from("<H", r, 4)[0],
                        "player": text(r[8:24])})
    sides = []
    for i in range(4):
        r = data[344 + i * 32:376 + i * 32]
        if r[0] != NOBODY:
            sides.append({"a": r[0], "b": r[1], "number": struct.unpack_from("<I", r, 4)[0], "code": text(r[8:16]),
                          "ticket": r[16:32]})
    return {
        "status": status,
        "name": STATE_NAMES.get(status, "OK" if status == 1 else str(status)),
        "stamp": stamp,
        "code": text(data[8:16]),
        "room": text(data[16:40]),
        "token": data[40:56],
        "salt": data[56:72],
        "challenge": data[72:88],
        "you": you,
        "rule": rule,
        "host": host,
        "setup": text(data[92:112]),
        "match": {"number": number, "a": a, "b": b, "code": text(data[120:128]), "ticket": data[128:144]},
        "last": {"number": last[0], "a": last[1], "b": last[2], "winner": last[3]},
        "members": members,
        "sides": sides,
    }


def feed_settings(code, ticket, settings):
    return header(K_FEED, P_SETTINGS) + field(code, 8) + field(ticket, 16) + field(settings, 64)


def feed_start(code, ticket, start_frame, start, index):
    count = (len(start) + CHUNK - 1) // CHUNK
    return header(K_FEED, P_START) + field(code, 8) + field(ticket, 16) + struct.pack(
        "<IIHH", start_frame, len(start), index, count) + start[index * CHUNK:(index + 1) * CHUNK]


def feed_inputs(code, ticket, first, inputs, hashes=()):
    """`inputs`: 4 bytes a frame; `hashes`: (frame, 8 bytes) pairs."""
    hashes = list(hashes)
    return header(K_FEED, P_INPUTS) + field(code, 8) + field(ticket, 16) + struct.pack(
        "<IHBx", first, len(inputs) // 4, len(hashes)) + b"".join(struct.pack("<I", f) + h for f, h in hashes) + inputs


def feed_end(code, ticket, frame, winner):
    return header(K_FEED, P_END) + field(code, 8) + field(ticket, 16) + struct.pack("<IB3x", frame, winner)


def parse_feed_ack(data):
    kind, _, _ = parse_header(data)
    if kind != K_FEED_ACK or len(data) != 160:
        raise ValueError("not a FEED_ACK")
    start_frame, settings, end, next_frame = struct.unpack_from("<IBB2xI", data, 16)
    bitmap = data[28:156]
    return {"code": text(data[8:16]), "start_frame": start_frame, "settings": bool(settings), "end": bool(end),
            "next": next_frame, "chunks": {i for i in range(1024) if bitmap[i // 8] >> (i % 8) & 1}}


def watch(code, token, match, start_frame=NO_START, wants=NO_START, has_settings=False, chunks=(), stamp=0):
    bitmap = bytearray(128)
    for i in chunks:
        bitmap[i // 8] |= 1 << (i % 8)
    data = header(K_WATCH) + field(code, 8) + field(token, 16) + field(match, 8) + struct.pack(
        "<IIB3xI", start_frame, wants, 1 if has_settings else 0, stamp) + bytes(bitmap) + bytes(8)
    assert len(data) == 192
    return data


def parse_scene(data):
    kind, part, _ = parse_header(data)
    if kind != K_SCENE:
        raise ValueError("not a SCENE")
    out = {"part": part, "code": text(data[8:16])}
    if part == P_SETTINGS:
        out.update(settings=data[16:80], first=text(data[80:96]), second=text(data[96:112]))
    elif part == P_START:
        out.update(zip(("start_frame", "size", "index", "count"), struct.unpack_from("<IIHH", data, 16)))
        out["data"] = data[28:]
    elif part == P_INPUTS:
        first, count, nh = struct.unpack_from("<IHB", data, 16)
        out.update(first=first, count=count,
                   hashes=[(struct.unpack_from("<I", data, 24 + i * 12)[0], data[28 + i * 12:36 + i * 12])
                           for i in range(nh)],
                   inputs=data[24 + nh * 12:])
    elif part == P_END:
        out.update(zip(("frame", "winner"), struct.unpack_from("<IB", data, 16)))
    elif part == P_NONE:
        out["why"] = data[16]
    return out
