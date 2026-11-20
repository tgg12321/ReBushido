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

RELAY_VERSION = 1
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


def hello(detail, code=b"", name=b"", player=b"", setup=b"", salt=b"", secret=b"", candidates=(), version=RELAY_VERSION):
    """A 208-byte HELLO."""
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
            struct.pack("<B3x", len(candidates)),
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
                "locked": r[69],
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
