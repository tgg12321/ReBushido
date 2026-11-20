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
    check(answer(rc.hello(rc.F_HOST | 0x20, name="x")) == rc.E_BAD_REQUEST, "an unknown detail bit")
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
    check(answer(rc.hello(rc.F_HOST, name="x", version=2)) == rc.E_VERSION, "version 2")
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
    tests = [main_server, limits_default, limits_creations, limits_server_full, limits_wrong_passwords, limits_relay]
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
