#!/usr/bin/env python3
"""Test of the relay server: python tools/relay/test_relay.py (or python3 in WSL).

Starts rebushido_relay.py on 127.0.0.1 and [::1] (the IPv6 part is skipped
where there is no IPv6) on ports the system picks, with short timeouts, and
drives fake clients through every state and refusal of the protocol. A few
more servers, each with one limit set low, test the limits. Prints OK and
exits 0, or names the first check that failed and exits 1.
"""

import os
import socket
import subprocess
import sys
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import relay_client as rc  # noqa: E402

SERVER = os.path.join(HERE, "rebushido_relay.py")
SETUP = "P5-7Q2K-M9XD-0000"
ALPHABET = "ACDEFGHJKMNPQRTUVWXY3479"
WINDOWS = sys.platform == "win32"


def has_ipv6():
    if not socket.has_ipv6:
        return False
    try:
        s = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
        s.bind(("::1", 0))
        s.close()
        return True
    except OSError:
        return False


IPV6 = has_ipv6()


class Failure(Exception):
    pass


def check(condition, what):
    if not condition:
        raise Failure(what)


class Server:
    """The server in a process of its own, on ports the system picks."""

    def __init__(self, *options, v6=True):
        listens = ["--listen", "127.0.0.1:0"]
        if v6 and IPV6:
            listens += ["--listen", "[::1]:0"]
        flags = subprocess.CREATE_NEW_PROCESS_GROUP if WINDOWS else 0
        self.proc = subprocess.Popen(
            [sys.executable, "-u", SERVER] + listens + list(options),
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            creationflags=flags,
        )
        first = self.proc.stdout.readline()
        if "listening on" not in first:
            self.proc.kill()
            raise Failure("the server did not start: %r" % (first + self.proc.stdout.read()))
        self.lines = [first.strip()]
        self.v4 = self.v6 = None
        for part in first.split("listening on", 1)[1].split(","):
            part = part.strip()
            if part.startswith("["):
                host, port = part[1:].split("]:")
                self.v6 = (host, int(port))
            else:
                host, port = part.rsplit(":", 1)
                self.v4 = (host, int(port))
        self.reader = threading.Thread(target=self.read, daemon=True)
        self.reader.start()

    def read(self):
        for line in self.proc.stdout:
            self.lines.append(line.rstrip("\n"))

    def stop(self):
        """Ctrl+Break on Windows, SIGTERM elsewhere: it must end cleanly."""
        import signal

        self.proc.send_signal(signal.CTRL_BREAK_EVENT if WINDOWS else signal.SIGTERM)
        try:
            code = self.proc.wait(10)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            raise Failure("the server did not stop on a signal")
        self.reader.join(5)
        check(code == 0, "the server exited %r on a signal" % code)
        check(any(line.startswith("stopped") for line in self.lines), "no 'stopped' line")

    def kill(self):
        if self.proc.poll() is None:
            self.proc.kill()
            self.proc.wait()


class Client:
    def __init__(self, server, v6=False):
        family = socket.AF_INET6 if v6 else socket.AF_INET
        self.sock = socket.socket(family, socket.SOCK_DGRAM)
        if hasattr(socket, "SIO_UDP_CONNRESET"):
            self.sock.ioctl(socket.SIO_UDP_CONNRESET, False)
        self.sock.bind(("::1" if v6 else "127.0.0.1", 0))
        self.ip = "::1" if v6 else "127.0.0.1"
        self.port = self.sock.getsockname()[1]
        self.family = 6 if v6 else 4
        self.server = server.v6 if v6 else server.v4

    def address(self):
        return (self.family, self.ip, self.port)

    def send(self, data):
        self.sock.sendto(data, self.server)

    def recv(self, kind=None, timeout=1.0):
        """The next datagram (of `kind`), or None."""
        end = time.monotonic() + timeout
        while True:
            left = end - time.monotonic()
            if left <= 0:
                return None
            self.sock.settimeout(left)
            try:
                data = self.sock.recv(65536)
            except socket.timeout:
                return None
            except OSError:
                continue
            if kind is None or (len(data) >= 8 and data[2] == kind):
                return data

    def drain(self):
        self.sock.setblocking(False)
        try:
            while True:
                self.sock.recv(65536)
        except OSError:
            pass
        self.sock.setblocking(True)

    def ask(self, data, timeout=1.0):
        """Send once; the STATE that answers it (parsed), or None."""
        self.drain()
        self.send(data)
        reply = self.recv(rc.K_STATE, timeout)
        return rc.parse_state(reply) if reply else None

    def close(self):
        self.sock.close()


def host_hello(code=b"", name="Dojo", player="Mikado", setup=SETUP, salt=b"", secret=b"", cands=(), flags=0):
    return rc.hello(rc.F_HOST | flags, code, name, player, setup, salt, secret, cands)


def guest_hello(code, player="Utsusemi", setup=SETUP, secret=b"", cands=(), flags=0):
    return rc.hello(rc.F_GUEST | flags, code, b"", player, setup, b"", secret, cands)


def list_all(client):
    """Every room the server lists, by paging."""
    rooms = []
    first = 0
    while True:
        client.drain()
        client.send(rc.list_rooms(first))
        reply = client.recv(rc.K_ROOMS)
        check(reply is not None, "no ROOMS")
        page = rc.parse_rooms(reply)
        rooms += page["rooms"]
        first += 12
        if first >= page["total"]:
            return rooms


def codes(client):
    return [r["code"] for r in list_all(client)]


def until_both(host, hdata, guest, gdata, want, timeout=3.0):
    """Both send HELLO every 50 ms until both are told `want`; the seconds it took."""
    start = time.monotonic()
    got = {}
    while time.monotonic() - start < timeout:
        for name, client, data in (("host", host, hdata), ("guest", guest, gdata)):
            if got.get(name) == want:
                continue
            state = client.ask(data, 0.05)
            if state:
                got[name] = state["detail"]
        if got.get("host") == want and got.get("guest") == want:
            return time.monotonic() - start
        time.sleep(0.05)
    raise Failure("not %s in %.1f s: %r" % (rc.STATE_NAMES[want], timeout, got))


def pair(server, host_cands=(), guest_cands=(), guest_v6=False, host_v6=False, want=rc.S_PUNCH):
    """A host opens a room and a guest joins; both are checked."""
    host = Client(server, host_v6)
    guest = Client(server, guest_v6)
    state = host.ask(host_hello(cands=host_cands))
    check(state and state["detail"] == rc.S_WAITING, "host not WAITING: %r" % state)
    code = state["code"]
    g = guest.ask(guest_hello(code, cands=guest_cands))
    check(g and g["detail"] == want, "guest not %s: %r" % (rc.STATE_NAMES[want], g))
    pushed = host.recv(rc.K_STATE)
    check(pushed is not None, "the host was not told the guest joined")
    h = rc.parse_state(pushed)
    check(h["detail"] == want, "host pushed %r" % h["name"])
    check(g["token"] != 0 and g["token"] == h["token"], "tokens %r %r" % (g["token"], h["token"]))
    check(g["code"] == code and h["code"] == code, "codes in PUNCH")
    check(g["you"] == guest.address() and h["you"] == host.address(), "you addresses")
    check(g["peer"] == host.address() and h["peer"] == guest.address(), "peer addresses")
    check(g["player"] == "Mikado" and h["player"] == "Utsusemi", "players")
    check(g["setup"] == SETUP and h["setup"] == SETUP, "setups")
    host_public = (0, host.ip, host.port)
    guest_public = (0, guest.ip, guest.port)
    check(g["candidates"][0] == host_public, "guest's first candidate is not the host's PUBLIC")
    check(h["candidates"][0] == guest_public, "host's first candidate is not the guest's PUBLIC")
    expect_h = [rc.parse_candidate(c) for c in host_cands][:3]
    expect_g = [rc.parse_candidate(c) for c in guest_cands][:3]
    check(g["candidates"][1:] == expect_h, "host's own candidates: %r" % g["candidates"])
    check(h["candidates"][1:] == expect_g, "guest's own candidates: %r" % h["candidates"])
    return host, guest, code, g["token"]


LAN = rc.candidate(rc.C_LAN4, "192.168.1.20", 7845)
MAPPED = rc.candidate(rc.C_MAPPED4, "203.0.113.9", 40000)
GLOBAL6 = rc.candidate(rc.C_GLOBAL6, "2001:db8::20", 7845)


# --- the main server ---------------------------------------------------------


def test_whoami(server):
    c = Client(server)
    c.send(rc.whoami())
    reply = c.recv(rc.K_YOUARE)
    check(reply is not None, "no YOUARE")
    check(rc.parse_youare(reply) == c.address(), "YOUARE %r" % (rc.parse_youare(reply),))
    c.send(rc.whoami() + b"x")
    check(c.recv(rc.K_YOUARE, 0.3) is None, "a WHOAMI of 17 bytes was answered")
    if IPV6:
        c6 = Client(server, v6=True)
        c6.send(rc.whoami())
        reply = c6.recv(rc.K_YOUARE)
        check(reply is not None and rc.parse_youare(reply) == c6.address(), "YOUARE over IPv6")


def test_create(server):
    h = Client(server)
    s = h.ask(host_hello(name="Alpha"))
    check(s and s["detail"] == rc.S_WAITING, "not WAITING: %r" % s)
    code = s["code"]
    check(len(code) == 6 and all(ch in ALPHABET for ch in code), "code %r" % code)
    check(s["you"] == h.address() and s["token"] == 0 and s["peer"][0] == 0, "WAITING fields")
    again = h.ask(host_hello(name="Alpha"))
    check(again["code"] == code, "a second HELLO with no code made another room")
    again = h.ask(host_hello(code=code, name="Alpha"))
    check(again["code"] == code, "a HELLO with the code made another room")
    rooms = [r for r in list_all(h) if r["code"] == code]
    check(len(rooms) == 1, "the room is not listed once")
    r = rooms[0]
    check(r["name"] == "Alpha" and r["player"] == "Mikado" and r["setup"] == SETUP, "listed fields %r" % r)
    check(r["players"] == 1 and r["locked"] == 0 and r["salt"] == bytes(16), "listed players/lock %r" % r)
    return h, code


def test_list_paging(server):
    hosts = [Client(server) for _ in range(14)]
    made = []
    for i, h in enumerate(hosts):
        s = h.ask(host_hello(name="Room %d" % i))
        check(s and s["detail"] == rc.S_WAITING, "room %d not made" % i)
        made.append(s["code"])
    c = Client(server)
    c.send(rc.list_rooms(0))
    page0 = rc.parse_rooms(c.recv(rc.K_ROOMS))
    check(len(page0["rooms"]) == 12 and page0["total"] >= 14, "page 0: %d of %d" % (len(page0["rooms"]), page0["total"]))
    newest = list(reversed(made))
    check([r["code"] for r in page0["rooms"]] == newest[:12], "page 0 is not newest first")
    check(page0["rooms"][0]["name"] == "Room 13", "page 0's first name")
    c.send(rc.list_rooms(12))
    page1 = rc.parse_rooms(c.recv(rc.K_ROOMS))
    check([r["code"] for r in page1["rooms"]][:2] == newest[12:], "page 1 does not follow page 0")
    c.send(rc.list_rooms(page0["total"] + 5))
    beyond = rc.parse_rooms(c.recv(rc.K_ROOMS))
    check(beyond["rooms"] == [] and beyond["total"] == page0["total"], "a page past the end")
    c.send(rc.list_rooms(0)[:15])
    check(c.recv(rc.K_ROOMS, 0.3) is None, "a LIST of 15 bytes was answered")
    c.send(rc.list_rooms(0)[:8] + b"\x01" + bytes(7))
    check(c.recv(rc.K_ROOMS, 0.3) is None, "a LIST with bytes 8..15 not zero was answered")
    return hosts, made


def test_direct4(server):
    host, guest, code, _ = pair(server, host_cands=[LAN, MAPPED], guest_cands=[LAN])
    hd = host_hello(code=code, cands=[LAN, MAPPED], flags=rc.F_PUNCHED4)
    # One side alone is not enough.
    s = host.ask(hd)
    check(s["detail"] == rc.S_PUNCH, "DIRECT4 with one side punched")
    until_both(host, hd, guest, guest_hello(code, cands=[LAN], flags=rc.F_PUNCHED4), rc.S_DIRECT4, 1.0)
    s = guest.ask(guest_hello(code))
    check(s["detail"] == rc.S_DIRECT4 and s["peer"] == host.address(), "DIRECT4 does not stay decided")
    rooms = [r for r in list_all(host) if r["code"] == code]
    check(rooms and rooms[0]["players"] == 2, "a room with a guest is not listed with 2 players")
    return host, guest, code


def test_direct6(server):
    host, guest, code, _ = pair(server, host_cands=[GLOBAL6], guest_cands=[GLOBAL6, LAN])
    flags = rc.F_PUNCHED4 | rc.F_PUNCHED6
    until_both(host, host_hello(code=code, flags=flags), guest, guest_hello(code, flags=flags), rc.S_DIRECT6, 1.0)


def test_direct4_waits_for_v6(server, grace):
    host, guest, code, _ = pair(server, host_cands=[GLOBAL6], guest_cands=[LAN])
    start = time.monotonic()
    hd = host_hello(code=code, cands=[GLOBAL6], flags=rc.F_PUNCHED4)
    gd = guest_hello(code, cands=[LAN], flags=rc.F_PUNCHED4)
    host.ask(hd)
    s = guest.ask(gd)
    check(s["detail"] == rc.S_PUNCH, "DIRECT4 at once though the host sent an IPv6 candidate")
    until_both(host, hd, guest, gd, rc.S_DIRECT4, 2.0)
    check(time.monotonic() - start >= grace * 0.8, "DIRECT4 before the IPv6 grace")


def test_relayed(server, window, relayed_timeout):
    host, guest, code, _ = pair(server)
    start = time.monotonic()
    until_both(host, host_hello(code=code), guest, guest_hello(code), rc.S_RELAYED, window + 2.0)
    check(time.monotonic() - start >= window * 0.8, "RELAYED before the punch window")
    forwarding(host, guest)
    third = Client(server)
    third.send(rc.header(1, 0, 1) + b"from a third")
    check(host.recv(None, 0.3) is None and guest.recv(None, 0.1) is None, "a third address's datagram was relayed")
    host.send(rc.header(2) + bytes(1300))
    check(guest.recv(None, 0.3) is None, "a datagram over 1200 bytes was relayed")
    host.send(rc.header(rc.K_PUNCH) + bytes(8))
    check(guest.recv(None, 0.3) is None, "a PUNCH was relayed")
    s = host.ask(host_hello(code=code))
    check(s["detail"] == rc.S_RELAYED, "RELAYED does not stay decided")
    # Traffic alone keeps a relayed room, past its timeout.
    end = time.monotonic() + relayed_timeout * 1.6
    while time.monotonic() < end:
        host.send(rc.header(3) + b"keep")
        check(guest.recv(None, 0.5) is not None, "relaying stopped while traffic flowed")
        time.sleep(0.15)
    check(code in codes(third), "a relayed room with traffic expired")
    return host, guest, code


def forwarding(a, b):
    for sender, receiver in ((a, b), (b, a)):
        for size in (8, 200, 1200):
            data = rc.header(7, 9, size) + bytes(range(256)) * 5
            data = data[:size]
            sender.send(data)
            got = receiver.recv(None)
            check(got == data, "a datagram of %d bytes was not relayed as it was" % size)


def test_cross_family(server):
    host, guest, code, _ = pair(server, guest_v6=True)
    until_both(host, host_hello(code=code), guest, guest_hello(code), rc.S_RELAYED, 4.0)
    forwarding(host, guest)


def test_locked(server):
    salt = os.urandom(16)
    verifier = rc.verifier(salt, "kabuto")
    host = Client(server)
    s = host.ask(host_hello(name="Locked", salt=salt, secret=verifier))
    code = s["code"]
    listed = [r for r in list_all(host) if r["code"] == code][0]
    check(listed["locked"] == 1 and listed["salt"] == salt, "a locked room is not listed locked with its salt")
    guest = Client(server)
    p = guest.ask(guest_hello(code))
    check(p["detail"] == rc.S_PASSWORD and p["salt"] == salt and p["challenge"] != bytes(16), "PASSWORD: %r" % p)
    check(p["token"] == 0 and p["peer"][0] == 0 and p["player"] == "Mikado", "PASSWORD gives the peer away")
    wrong = rc.proof(rc.verifier(salt, "kabuta"), p["challenge"])
    w = guest.ask(guest_hello(code, secret=wrong))
    check(w["detail"] == rc.E_WRONG_PASSWORD, "a wrong proof: %r" % w["name"])
    # A proof to a challenge never sent to this address is not counted wrong.
    other = Client(server)
    o = other.ask(guest_hello(code, secret=rc.proof(verifier, p["challenge"])))
    check(o["detail"] == rc.S_PASSWORD, "a proof to another's challenge: %r" % o["name"])
    right = rc.proof(verifier, p["challenge"])
    ok = guest.ask(guest_hello(code, secret=right))
    check(ok["detail"] == rc.S_PUNCH and ok["peer"] == host.address(), "the right proof: %r" % ok["name"])
    pushed = host.recv(rc.K_STATE)
    check(pushed and rc.parse_state(pushed)["detail"] == rc.S_PUNCH, "the host of a locked room was not told")
    # A salt alone, or a verifier alone, locks a room too.
    h2 = Client(server)
    c2 = h2.ask(host_hello(name="Salt only", salt=salt))["code"]
    check(Client(server).ask(guest_hello(c2))["detail"] == rc.S_PASSWORD, "a room with only a salt is not locked")


def test_refusals(server):
    host, guest, code, _ = pair(server)
    third = Client(server)
    full = third.ask(guest_hello(code))
    check(full["detail"] == rc.E_ROOM_FULL, "ROOM_FULL: %r" % full["name"])
    check(full["code"] == code, "ROOM_FULL names the room")
    other = Client(server)
    inc = other.ask(guest_hello(code, setup="P5-0000-0000-0001"))
    check(inc["detail"] == rc.E_INCOMPATIBLE and inc["setup"] == SETUP, "INCOMPATIBLE: %r" % inc)
    check(other.ask(guest_hello("ZZZZZZ"))["detail"] == rc.E_NO_ROOM, "NO_ROOM for an unknown code")
    check(other.ask(guest_hello(""))["detail"] == rc.E_NO_ROOM, "NO_ROOM for no code")
    check(host.ask(guest_hello(code))["detail"] == rc.E_BAD_REQUEST, "a host joining its own room")
    # RESET: the guest goes, the room waits again under the same code.
    s = host.ask(host_hello(code=code, flags=rc.F_RESET))
    check(s["detail"] == rc.S_WAITING and s["code"] == code and s["token"] == 0, "RESET: %r" % s["name"])
    s = host.ask(host_hello(code=code))
    check(s["detail"] == rc.S_WAITING, "after RESET")
    newcomer = third.ask(guest_hello(code))
    check(newcomer["detail"] == rc.S_PUNCH, "a new guest after RESET: %r" % newcomer["name"])
    check(guest.ask(guest_hello(code))["detail"] == rc.E_ROOM_FULL, "the old guest after RESET")


def test_bad_requests(server):
    c = Client(server)

    def answer(data):
        s = c.ask(data)
        check(s is not None, "no answer to a malformed HELLO")
        return s["detail"]

    good = host_hello(name="Bad")
    check(answer(good[:100]) == rc.E_BAD_REQUEST, "a short HELLO")
    check(answer(good + b"\0") == rc.E_BAD_REQUEST, "a long HELLO")
    check(answer(rc.hello(rc.F_HOST | rc.F_GUEST, name="x")) == rc.E_BAD_REQUEST, "HOST and GUEST")
    check(answer(rc.hello(0, name="x")) == rc.E_BAD_REQUEST, "neither HOST nor GUEST")
    check(answer(rc.hello(rc.F_GUEST | rc.F_RESET, code="AAAAAA")) == rc.E_BAD_REQUEST, "a guest's RESET")
    check(answer(rc.hello(rc.F_HOST | 0x40, name="x")) == rc.E_BAD_REQUEST, "an unknown detail bit")
    check(answer(rc.hello(rc.F_HOST | rc.F_TICKET, code="AAAAAA", name="x")) == rc.E_NO_ROOM, "a ticket of no match")
    check(answer(host_hello(name="")) == rc.E_BAD_REQUEST, "a room with no name")
    check(answer(host_hello(player="Mi\x01kado")) == rc.E_BAD_REQUEST, "a control byte in a name")
    check(answer(host_hello(name=b"Dojo\0x")) == rc.E_BAD_REQUEST, "text after a name's padding")
    check(answer(host_hello(name=b"Doj\xe9")) == rc.E_BAD_REQUEST, "a byte over 0x7E")
    public = rc.candidate(rc.C_LAN4, "10.0.0.1", 5)
    public = bytes([rc.C_PUBLIC]) + public[1:]
    check(answer(host_hello(cands=[public])) == rc.E_BAD_REQUEST, "a PUBLIC candidate from a client")
    check(answer(host_hello(cands=[rc.candidate(9, "10.0.0.1", 5)])) == rc.E_BAD_REQUEST, "a candidate of type 9")
    check(answer(host_hello(cands=[rc.candidate(rc.C_LAN4, "10.0.0.1", 0)])) == rc.E_BAD_REQUEST, "a candidate of port 0")
    five = host_hello(cands=[LAN] * 4)
    five = five[:124] + b"\x05" + five[125:]
    check(answer(five) == rc.E_BAD_REQUEST, "five candidates")
    # the clients of version 1 are refused, cleanly
    check(answer(rc.hello(rc.F_HOST, name="x", version=1)) == rc.E_VERSION, "version 1")
    check(answer(rc.hello(rc.F_HOST, name="x", version=3)) == rc.E_VERSION, "version 3")
    # None of these made a room.
    check(answer(host_hello(name="Good")) == rc.S_WAITING, "a good HELLO after bad ones")


def test_vanish(client, codes_gone, room_timeout):
    end = time.monotonic() + room_timeout + 2.0
    while time.monotonic() < end:
        listed = set(codes(client))
        if not listed & set(codes_gone):
            break
        time.sleep(0.2)
    check(not set(codes(client)) & set(codes_gone), "rooms of silent hosts are still listed")
    check(client.ask(guest_hello(codes_gone[0]))["detail"] == rc.E_NO_ROOM, "an expired room is joinable")


def main_server():
    window, grace, waiting, direct, relayed = 0.8, 0.4, 1.5, 1.0, 1.0
    server = Server(
        "--verbose",
        "--punch-window", str(window),
        "--v6-grace", str(grace),
        "--waiting-timeout", str(waiting),
        "--direct-timeout", str(direct),
        "--relayed-timeout", str(relayed),
        "--rooms-per-ip", "1000",
        "--creations-per-min", "100000",
        "--hello-rate", "100000",
        "--query-rate", "100000",
        "--wrong-passwords-per-min", "1000",
        "--stats-interval", "0.5",
    )
    try:
        test_whoami(server)
        _, alpha = test_create(server)
        _, paged = test_list_paging(server)
        _, _, direct_code = test_direct4(server)
        test_direct6(server)
        test_direct4_waits_for_v6(server, grace)
        test_locked(server)
        test_refusals(server)
        test_bad_requests(server)
        if IPV6:
            test_cross_family(server)
        host, guest, relayed_code = test_relayed(server, window, relayed)
        # Every host above has gone quiet: their rooms vanish.
        watcher = Client(server)
        test_vanish(watcher, [alpha] + paged + [direct_code, relayed_code], max(waiting, direct, relayed))
        host.send(rc.header(4) + b"late")
        check(guest.recv(None, 0.3) is None, "an expired relayed room still relays")
        server.stop()
        check(any(line.startswith("rooms ") for line in server.lines), "no count line")
        check(any("joined" in line for line in server.lines), "--verbose logs no rooms")
    finally:
        server.kill()


# --- rooms of several (docs/NETPLAY.md, section 16) --------------------------


class Member:
    """A member of a room of several: its client, token and last roster."""

    def __init__(self, server, player, plays=True):
        self.client = Client(server)
        self.player = player
        self.plays = plays
        self.token = b""
        self.code = ""
        self.roster = None

    def say(self, action, **fields):
        fields.setdefault("plays", self.plays)
        fields.setdefault("code", self.code)
        fields.setdefault("token", self.token)
        self.client.drain()
        self.client.send(rc.room_hello(action, player=self.player, setup=SETUP, **fields))
        reply = self.client.recv(rc.K_ROSTER)
        check(reply is not None, "%s: no ROSTER to action %d" % (self.player, action))
        self.roster = rc.parse_roster(reply)
        if self.roster["status"] == 1:
            self.token = self.roster["token"]
            self.code = self.roster["code"]
        return self.roster

    def stay(self):
        return self.say(rc.A_STAY)

    def names(self, *ids):
        by_id = {m["id"]: m["player"] for m in self.roster["members"]}
        return tuple(by_id.get(i) for i in ids)


def open_group(server, rule, players=("Ayame", "Bunta", "Chie"), **fields):
    members = [Member(server, p) for p in players]
    r = members[0].say(rc.A_OPEN, name="Many", rule=rule, rtt=37, **fields)
    check(r["status"] == 1 and len(r["code"]) == 6 and r["you"] == r["host"], "OPEN: %r" % r["name"])
    for m in members[1:]:
        e = m.say(rc.A_ENTER, code=r["code"])
        check(e["status"] == 1 and e["code"] == r["code"] and e["token"] != members[0].token, "ENTER: %r" % e["name"])
    return members


def pair_now(member):
    """The rotation's match as `member` sees it: the two players' names."""
    match = member.stay()["match"]
    return member.names(match["a"], match["b"])


def test_group_rotation(server):
    a, b, c = open_group(server, rc.RULE_FIXED)
    # each stay says the member's round trip to the server
    r = a.say(rc.A_STAY, rtt=37)
    check(len(r["members"]) == 3 and r["rule"] == rc.RULE_FIXED, "three members: %r" % r["members"])
    listed = [x for x in list_all(a.client) if x["code"] == a.code]
    check(listed and listed[0]["group"] and listed[0]["players"] == 3 and listed[0]["ping"] == 37,
          "a room of several is listed with its members and its host's round trip: %r" % listed)
    turns = []
    for number in range(1, 5):
        match = a.stay()["match"]
        turns.append(a.names(match["a"], match["b"]))
        check(match["number"] == number, "match %d numbered %d" % (number, match["number"]))
        # the ticket only to the two who play
        seen = {m.player: m.stay()["match"]["ticket"] for m in (a, b, c)}
        players = a.names(match["a"], match["b"])
        for who, ticket in seen.items():
            check((ticket != bytes(16)) == (who in players), "%s's ticket of match %d" % (who, number))
        first = [m for m in (a, b, c) if m.player == players[0]][0]
        first.say(rc.A_RESULT, number=number, arg=rc.W_FIRST)
    check(turns == [("Ayame", "Bunta"), ("Bunta", "Chie"), ("Chie", "Ayame"), ("Ayame", "Bunta")],
          "a fixed turn: %r" % turns)
    wins = {m["player"]: m["wins"] for m in a.stay()["members"]}
    check(wins == {"Ayame": 2, "Bunta": 1, "Chie": 1}, "the wins: %r" % wins)
    last = a.roster["last"]
    check(last["number"] == 4 and a.names(last["a"])[0] == "Ayame" and last["winner"] == rc.W_FIRST,
          "the last result: %r" % last)
    return a, b, c


def test_group_winner_stays(server):
    a, b, c = open_group(server, rc.RULE_WINNER, ("Dai", "Eiko", "Fumi"))
    turns = []
    # the second wins, then the first, then a draw
    for winner in (rc.W_SECOND, rc.W_FIRST, rc.W_DRAW, rc.W_FIRST):
        match = a.stay()["match"]
        turns.append(a.names(match["a"], match["b"]))
        a.say(rc.A_RESULT, number=match["number"], arg=winner) if a.player in turns[-1] else \
            b.say(rc.A_RESULT, number=match["number"], arg=winner)
    # Dai+Eiko, Eiko wins and stays: Eiko+Fumi; Eiko wins: Eiko+Dai; a
    # draw: both to the end, Fumi first: Fumi+Eiko
    check(turns == [("Dai", "Eiko"), ("Eiko", "Fumi"), ("Eiko", "Dai"), ("Fumi", "Eiko")],
          "the winner staying: %r" % turns)
    # Fumi+Dai now: a result by one not in the match counts for nothing,
    # and one told twice once
    match = a.stay()["match"]
    check(a.names(match["a"], match["b"]) == ("Fumi", "Dai"), "the fifth match: %r" % (match,))
    b.say(rc.A_RESULT, number=match["number"], arg=rc.W_FIRST)
    check(a.stay()["match"]["number"] == match["number"], "a result by one not in the match")
    c.say(rc.A_RESULT, number=match["number"], arg=rc.W_FIRST)
    c.say(rc.A_RESULT, number=match["number"], arg=rc.W_FIRST)
    check(a.stay()["match"]["number"] == match["number"] + 1, "a result told twice")
    # the host turns the rotation off: no match after this one
    a.say(rc.A_RULE, rule=rc.RULE_OFF)
    match = a.stay()["match"]
    first = [m for m in (a, b, c) if m.player == a.names(match["a"])[0]][0]
    first.say(rc.A_RESULT, number=match["number"], arg=rc.W_FIRST)
    check(a.stay()["match"]["a"] == rc.NOBODY, "a match made with the rotation off")
    check(b.say(rc.A_RULE, rule=rc.RULE_FIXED)["rule"] == rc.RULE_OFF, "a member who is not the host set the rule")


def test_group_meeting(server):
    """A match is met as a two-player room is, by its ticket alone."""
    a, b, c = open_group(server, rc.RULE_FIXED, ("Goro", "Hana", "Isao"))
    match = a.stay()["match"]
    b.stay()
    ticket = match["ticket"]
    check(match["code"] not in codes(a.client), "a match's meeting is listed before it opens")
    guest = Client(server)
    early = guest.ask(rc.hello(rc.F_GUEST | rc.F_TICKET, code=match["code"], player="Hana", setup=SETUP, secret=ticket))
    check(early["detail"] == rc.S_WAITING, "a guest before the match's host: %r" % early["name"])
    host = Client(server)
    s = host.ask(rc.hello(rc.F_HOST | rc.F_TICKET, code=match["code"], player="Goro", setup=SETUP, secret=ticket))
    check(s["detail"] == rc.S_WAITING and s["code"] == match["code"], "the match's host: %r" % s)
    check(match["code"] not in codes(a.client), "a match's meeting is listed")
    wrong = Client(server).ask(rc.hello(rc.F_GUEST | rc.F_TICKET, code=match["code"], setup=SETUP,
                                        secret=bytes(16)))
    check(wrong["detail"] == rc.E_NO_ROOM, "a wrong ticket: %r" % wrong["name"])
    plain = Client(server).ask(guest_hello(match["code"]))
    check(plain["detail"] == rc.E_NO_ROOM, "a guest without a ticket: %r" % plain["name"])
    g = guest.ask(rc.hello(rc.F_GUEST | rc.F_TICKET, code=match["code"], player="Hana", setup=SETUP, secret=ticket))
    check(g["detail"] == rc.S_PUNCH and g["peer"] == host.address(), "the match's guest: %r" % g["name"])
    # the room of several's opener can open a two-player room still
    check(a.client.ask(host_hello(name="Two"))["detail"] == rc.S_WAITING, "a room of the same address")
    return a, b, c, match, host, guest


def test_group_sides(server):
    a, b, c, d, e = open_group(server, rc.RULE_FIXED, ("Jun", "Kei", "Mai", "Nao", "Oto"))
    rotation = pair_now(a)
    check(rotation == ("Jun", "Kei"), "the rotation's first: %r" % (rotation,))
    s = d.say(rc.A_SIDE_OPEN)
    check(len(s["sides"]) == 1 and d.names(s["sides"][0]["a"]) == ("Nao",) and s["sides"][0]["b"] == rc.NOBODY,
          "a match beside: %r" % s["sides"])
    check(s["sides"][0]["ticket"] != bytes(16), "the side's opener has no ticket")
    check(c.stay()["sides"][0]["ticket"] == bytes(16), "a member not in the side has its ticket")
    s = e.say(rc.A_SIDE_JOIN, arg=0)
    side = s["sides"][0]
    check(e.names(side["b"]) == ("Oto",) and side["ticket"] != bytes(16), "the side joined: %r" % side)
    check(side["code"] != a.stay()["match"]["code"], "the side's code is the rotation's")
    busy = {m["player"]: bool(m["flags"] & rc.M_BUSY) for m in a.roster["members"]}
    check(busy == {"Jun": True, "Kei": True, "Mai": False, "Nao": True, "Oto": True}, "busy: %r" % busy)
    # the rotation goes on without the two beside it
    a.say(rc.A_RESULT, number=a.roster["match"]["number"], arg=rc.W_FIRST)
    check(pair_now(a) == ("Kei", "Mai"), "the rotation took a member beside it: %r" % (pair_now(a),))
    d.say(rc.A_SIDE_DONE, number=side["number"])
    check(a.stay()["sides"] == [], "a side done is still there")
    # leaving in the middle: the one who stays wins
    match = a.stay()["match"]
    b.say(rc.A_LEAVE)
    r = a.stay()
    check(len(r["members"]) == 4 and r["last"]["number"] == match["number"] and r["last"]["winner"] == rc.W_SECOND,
          "a member left a match: %r" % r["last"])
    # the host leaves: the next is the host
    a.say(rc.A_LEAVE)
    r = c.stay()
    check(r["host"] == [m["id"] for m in r["members"] if m["player"] == "Mai"][0], "the host after the host left")


def test_group_feed(server):
    """A match's feed, from its host, to a spectator who comes in the middle."""
    a, b, c, match, host, guest = test_group_meeting(server)
    code, ticket = match["code"], match["ticket"]
    feeder = Client(server)
    settings = bytes(range(64))
    start = os.urandom(2500)
    feeder.send(rc.feed_settings(code, ticket, settings))
    ack = feeder.recv(rc.K_FEED_ACK)
    check(ack and rc.parse_feed_ack(ack)["settings"], "the settings not taken")
    feeder.send(rc.feed_settings(code, bytes(16), settings))
    check(feeder.recv(rc.K_FEED_ACK, 0.3) is None, "a feed of a wrong ticket was taken")
    # a spectator before the start: nothing yet
    c.stay()
    c.client.send(rc.watch(c.code, c.token, code))
    got = [rc.parse_scene(c.client.recv(rc.K_SCENE)) for _ in range(2)]
    check(got[0]["part"] == rc.P_SETTINGS and got[0]["settings"] == settings and got[0]["first"] == "Goro",
          "the settings to a spectator: %r" % got[0])
    check(got[1]["part"] == rc.P_NONE and got[1]["why"] == 1, "nothing yet: %r" % got[1])
    for index in range(3):
        feeder.send(rc.feed_start(code, ticket, 600, start, index))
        feeder.recv(rc.K_FEED_ACK)
    frames = bytes(range(240)) * 4  # 240 frames, the most of a datagram
    hashes = [(630, b"H" * 8), (660, b"I" * 8)]
    feeder.send(rc.feed_inputs(code, ticket, 600, frames, hashes))
    ack = rc.parse_feed_ack(feeder.recv(rc.K_FEED_ACK))
    check(ack["start_frame"] == 600 and ack["chunks"] == {0, 1, 2} and ack["next"] == 840, "the feed's ack: %r" % ack)
    # inputs that overlap are taken from where the server is
    feeder.send(rc.feed_inputs(code, ticket, 800, bytes(4) * 100))
    check(rc.parse_feed_ack(feeder.recv(rc.K_FEED_ACK))["next"] == 900, "overlapping inputs")
    # the start, all of it, then the inputs
    c.client.drain()
    c.client.send(rc.watch(c.code, c.token, code, has_settings=True))
    pieces = {}
    while True:
        reply = c.client.recv(rc.K_SCENE, 0.3)
        if reply is None:
            break
        scene = rc.parse_scene(reply)
        if scene["part"] == rc.P_START:
            pieces[scene["index"]] = scene["data"]
    check(b"".join(pieces[i] for i in range(3)) == start, "the start as it was sent: %r" % sorted(pieces))
    c.client.send(rc.watch(c.code, c.token, code, 600, 600, True, {0, 1, 2}))
    scenes = []
    while True:
        reply = c.client.recv(rc.K_SCENE, 0.3)
        if reply is None:
            break
        scenes.append(rc.parse_scene(reply))
    inputs = b"".join(s["inputs"] for s in scenes if s["part"] == rc.P_INPUTS)
    check(inputs == frames + bytes(4) * 60, "the inputs from the start: %d bytes" % len(inputs))
    check(scenes[0]["hashes"] == hashes, "the hashes among them: %r" % scenes[0]["hashes"])
    # the end, and a spectator who is not a member
    feeder.send(rc.feed_end(code, ticket, 900, rc.W_SECOND))
    feeder.recv(rc.K_FEED_ACK)
    c.client.send(rc.watch(c.code, c.token, code, 600, 900, True, {0, 1, 2}))
    end = rc.parse_scene(c.client.recv(rc.K_SCENE))
    check(end["part"] == rc.P_END and end["frame"] == 900 and end["winner"] == rc.W_SECOND, "the end: %r" % end)
    stranger = Client(server)
    stranger.send(rc.watch(c.code, bytes(16), code))
    scene = rc.parse_scene(stranger.recv(rc.K_SCENE))
    check(scene["part"] == rc.P_NONE and scene["why"] == 3, "a stranger watched: %r" % scene)
    # the match over, its feed is kept a while for those still watching
    a.say(rc.A_RESULT, number=match["number"], arg=rc.W_SECOND)
    c.client.send(rc.watch(c.code, c.token, code, 600, 900, True, {0, 1, 2}))
    check(rc.parse_scene(c.client.recv(rc.K_SCENE))["part"] == rc.P_END, "the feed of a match over")


def test_group_refusals(server):
    salt = os.urandom(16)
    verifier = rc.verifier(salt, "kabuto")
    a = Member(server, "Ran")
    a.say(rc.A_OPEN, name="Locked many", salt=salt, secret=verifier)
    listed = [x for x in list_all(a.client) if x["code"] == a.code][0]
    check(listed["locked"] and listed["group"] and listed["salt"] == salt, "a locked room of several: %r" % listed)
    b = Member(server, "Sora")
    p = b.say(rc.A_ENTER, code=a.code)
    check(p["status"] == rc.S_PASSWORD and p["salt"] == salt and p["challenge"] != bytes(16), "PASSWORD: %r" % p)
    w = b.say(rc.A_ENTER, code=a.code, secret=rc.proof(rc.verifier(salt, "nope"), p["challenge"]))
    check(w["status"] == rc.E_WRONG_PASSWORD, "a wrong password: %r" % w["name"])
    ok = b.say(rc.A_ENTER, code=a.code, secret=rc.proof(verifier, w["challenge"]))
    check(ok["status"] == 1, "the right password: %r" % ok["name"])
    other = Member(server, "Taro")
    other.client.send(rc.room_hello(rc.A_ENTER, code=a.code, player="Taro", setup="P5-0000-0000-0001"))
    inc = rc.parse_roster(other.client.recv(rc.K_ROSTER))
    check(inc["status"] == rc.E_INCOMPATIBLE and inc["setup"] == SETUP, "INCOMPATIBLE: %r" % inc)
    check(other.say(rc.A_ENTER, code="ZZZZZZ")["status"] == rc.E_NO_ROOM, "no such room")
    check(other.say(rc.A_STAY, code=a.code, token=os.urandom(16))["status"] == rc.E_NO_ROOM, "a stranger's token")
    other.client.send(rc.room_hello(rc.A_OPEN, name="Old", player="Taro", setup=SETUP, version=1))
    check(rc.parse_roster(other.client.recv(rc.K_ROSTER))["status"] == rc.E_VERSION, "a client of version 1")
    other.client.send(rc.room_hello(rc.A_OPEN, name="x")[:100])
    check(rc.parse_roster(other.client.recv(rc.K_ROSTER))["status"] == rc.E_BAD_REQUEST, "a short ROOM")
    many = open_group(server, rc.RULE_OFF, ["P%d" % i for i in range(8)])
    full = Member(server, "Ninth").say(rc.A_ENTER, code=many[0].code)
    check(full["status"] == rc.E_ROOM_FULL, "a ninth member: %r" % full["name"])


def test_group_timeout(server, member_timeout):
    a, b, c = open_group(server, rc.RULE_FIXED, ("Uta", "Waka", "Yui"))
    pair = pair_now(a)
    end = time.monotonic() + member_timeout + 1.5
    while time.monotonic() < end:
        a.stay()
        c.stay()
        time.sleep(0.2)
    r = a.stay()
    check([m["player"] for m in r["members"]] == ["Uta", "Yui"], "a silent member is still in: %r" % r["members"])
    check(pair == ("Uta", "Waka") and r["last"]["winner"] == rc.W_FIRST, "the silent one's match: %r" % r["last"])
    check(pair_now(a) == ("Uta", "Yui") or pair_now(a) == ("Yui", "Uta"), "the next match: %r" % (pair_now(a),))


def groups_server():
    member_timeout = 3.0
    server = Server("--verbose", "--rooms-per-ip", "1000", "--creations-per-min", "100000", "--hello-rate", "100000",
                    "--query-rate", "100000", "--watch-rate", "100000", "--wrong-passwords-per-min", "1000",
                    "--member-timeout", str(member_timeout), v6=False)
    try:
        test_group_rotation(server)
        test_group_winner_stays(server)
        test_group_sides(server)
        test_group_feed(server)
        test_group_refusals(server)
        test_group_timeout(server, member_timeout)
        server.stop()
    finally:
        server.kill()


# --- the limits --------------------------------------------------------------


def limits_default():
    """Rooms per IP, HELLO and LIST rates; and the log holds counts only."""
    server = Server("--stats-interval", "0.3", v6=False)
    try:
        hosts = [Client(server) for _ in range(4)]
        made = []
        for h in hosts[:3]:
            s = h.ask(host_hello())
            check(s["detail"] == rc.S_WAITING, "rooms per IP: one of the first three refused")
            made.append(s["code"])
        s = hosts[3].ask(host_hello())
        check(s and s["detail"] == rc.E_RATE_LIMITED, "a 4th room from one IP: %r" % (s and s["name"]))
        time.sleep(1.1)
        # HELLO: 30 a second, then dropped but for one RATE_LIMITED a second.
        blaster = hosts[0]
        blaster.drain()
        for _ in range(100):
            blaster.send(host_hello())
        answers = []
        while True:
            got = blaster.recv(rc.K_STATE, 0.3)
            if got is None:
                break
            answers.append(rc.parse_state(got)["detail"])
        waiting = answers.count(rc.S_WAITING)
        limited = answers.count(rc.E_RATE_LIMITED)
        check(20 <= waiting <= 33 and 1 <= limited <= 2, "HELLO limit: %d answered, %d RATE_LIMITED" % (waiting, limited))
        # LIST: 10 a second, the rest dropped.
        time.sleep(1.1)
        lister = Client(server)
        for _ in range(50):
            lister.send(rc.list_rooms(0))
        n = 0
        while lister.recv(rc.K_ROOMS, 0.3) is not None:
            n += 1
        check(5 <= n <= 12, "LIST limit: %d answered" % n)
        time.sleep(0.5)
        server.stop()
        quiet = [line for line in server.lines if "listening on" not in line]
        check(any(line.startswith("rooms 3,") for line in quiet), "no count line with the rooms: %r" % quiet)
        for line in quiet:
            check("127.0.0.1" not in line and not any(code in line for code in made), "the log names an address or code: %r" % line)
    finally:
        server.kill()


def limits_creations():
    server = Server("--rooms-per-ip", "100", "--creations-per-min", "6", v6=False)
    try:
        for i in range(6):
            s = Client(server).ask(host_hello())
            check(s["detail"] == rc.S_WAITING, "creation %d refused" % (i + 1))
        s = Client(server).ask(host_hello())
        check(s["detail"] == rc.E_RATE_LIMITED, "a 7th creation in a minute: %r" % s["name"])
        server.stop()
    finally:
        server.kill()


def limits_server_full():
    server = Server("--max-rooms", "2", "--rooms-per-ip", "10", v6=False)
    try:
        for _ in range(2):
            check(Client(server).ask(host_hello())["detail"] == rc.S_WAITING, "a room under the limit")
        s = Client(server).ask(host_hello())
        check(s["detail"] == rc.E_SERVER_FULL, "a room over --max-rooms: %r" % s["name"])
        server.stop()
    finally:
        server.kill()


def limits_wrong_passwords():
    block = 1.0
    server = Server("--wrong-password-block", str(block), v6=False)
    try:
        salt = os.urandom(16)
        verifier = rc.verifier(salt, "kabuto")
        host = Client(server)
        code = host.ask(host_hello(salt=salt, secret=verifier))["code"]
        guest = Client(server)
        challenge = guest.ask(guest_hello(code))["challenge"]
        bad = rc.proof(rc.verifier(salt, "nope"), challenge)
        for i in range(5):
            s = guest.ask(guest_hello(code, secret=bad))
            check(s["detail"] == rc.E_WRONG_PASSWORD, "wrong password %d: %r" % (i + 1, s["name"]))
        s = guest.ask(guest_hello(code, secret=rc.proof(verifier, challenge)))
        check(s and s["detail"] == rc.E_RATE_LIMITED, "after 5 wrong passwords: %r" % (s and s["name"]))
        s = guest.ask(guest_hello(code), 0.3)
        check(s is None, "RATE_LIMITED more than once a second")
        time.sleep(block + 0.2)
        s = guest.ask(guest_hello(code))
        check(s["detail"] == rc.S_PASSWORD, "after the block: %r" % s["name"])
        s = guest.ask(guest_hello(code, secret=rc.proof(verifier, s["challenge"])))
        check(s["detail"] == rc.S_PUNCH, "the right password after the block: %r" % s["name"])
        server.stop()
    finally:
        server.kill()


def limits_relay():
    server = Server("--relay-only", "--relay-datagram-rate", "50", "--relay-byte-rate", "20000", v6=False)
    try:
        host, guest, code, _ = pair(server, want=rc.S_RELAYED)
        for _ in range(200):
            host.send(rc.header(5) + bytes(8))
        n = 0
        while guest.recv(None, 0.3) is not None:
            n += 1
        check(30 <= n <= 60, "datagram limit: %d of 200 relayed" % n)
        time.sleep(1.2)
        for _ in range(40):
            host.send(rc.header(5) + bytes(992))
            time.sleep(0.002)
        n = 0
        while guest.recv(None, 0.3) is not None:
            n += 1
        check(10 <= n <= 24, "byte limit: %d of 40 KB-sized datagrams relayed" % n)
        server.stop()
    finally:
        server.kill()


def main():
    start = time.monotonic()
    tests = [main_server, groups_server, limits_default, limits_creations, limits_server_full, limits_wrong_passwords,
             limits_relay]
    for test in tests:
        try:
            test()
        except Failure as failure:
            print("FAILED in %s: %s" % (test.__name__, failure))
            return 1
        print("  %s ok" % test.__name__)
    print("OK (%.1f s%s)" % (time.monotonic() - start, "" if IPV6 else ", IPv6 skipped: none here"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
