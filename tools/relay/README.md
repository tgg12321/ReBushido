# The relay server

`rebushido_relay.py` is the server the game's online play meets at: a lobby
(the list of open rooms), a rendezvous (it tells a room's host and guest each
other's addresses so they can punch through their routers and play
directly) and a relay (when they cannot, it passes their game traffic
between them). One Python 3 process (3.8 or newer, the standard library
alone), UDP, port 7845 by default.

It holds no game data and needs no disc. It never reads the game's traffic,
only passes it on inside a relayed room between that room's two addresses.
It writes nothing to disk. What it keeps, in memory and for a room's life
only: the room's code, name, host's and guest's player names, setup code,
the salt and verifier of its password (never the password), the two
addresses and their candidates.

## The protocol

Protocol version 1, specified to the byte in `docs/NETPLAY.md`, section 14.
In short, every datagram starts with the port's 8-byte header (`RB`, kind,
detail, a little-endian u32); kinds 48 to 63 are the server's:

| Kind | Name | Way | What |
|---|---|---|---|
| 48 | HELLO | client to server | host: open or keep a room; guest: join one (208 bytes) |
| 49 | STATE | server to client | the answer to every HELLO: WAITING, PASSWORD, PUNCH, DIRECT4, DIRECT6, RELAYED, or an error |
| 50, 51 | PUNCH, PUNCH_ACK | client to client | the server drops them |
| 52 | LIST | client to server | a page of 12 rooms, from an index |
| 53 | ROOMS | server to client | the page, newest first |
| 55 | WHOAMI | client to server | |
| 56 | YOUARE | server to client | the address the server saw |

A host's HELLO makes a room with a 6-character code; a guest's HELLO with
that code joins it (a locked room first sends a challenge, answered with
`HMAC-SHA256(SHA-256(salt || password), challenge)`); then both are sent each
other's candidates and punch. If both say they got through over IPv6 the
room is DIRECT6, over IPv4 DIRECT4; after the punch window (3 s) it is
RELAYED, and the server passes every datagram of a kind below 48 between the
two. `relay_client.py` builds and reads the server's datagrams, for tests and
tools.

## Running it

    python3 tools/relay/rebushido_relay.py                    # 0.0.0.0:7845 and [::]:7845
    python3 tools/relay/rebushido_relay.py --listen 127.0.0.1:7845 --verbose
    python3 tools/relay/rebushido_relay.py --relay-only       # never punch, relay every room

`--listen ADDR:PORT` (IPv6 as `[::]:7845`) may be given several times; with
none it listens on `0.0.0.0:7845` and, where the system has IPv6,
`[::]:7845`. Each datagram is answered from the socket it came in on, and a
relayed one goes out through the socket its receiver talks to, so a room
whose host is on IPv4 and guest on IPv6 is relayed too. Ctrl+C or SIGTERM
ends it cleanly. `--help` lists the rest: the punch window, the timeouts
and every limit.

The test starts the server on `127.0.0.1` and `[::1]` (the IPv6 part is
skipped where there is none) on ports the system picks and drives fake
clients through every state, refusal and limit, in about 15 seconds:

    python tools/relay/test_relay.py          # Windows
    python3 tools/relay/test_relay.py         # WSL, Linux

## Deploying to a VPS

`deploy.sh` installs it on an Ubuntu box as a systemd service, as root:

    scp -r tools/relay root@BOX:/root/rebushido-relay-src
    ssh root@BOX bash /root/rebushido-relay-src/deploy.sh             # install or update
    ssh root@BOX bash /root/rebushido-relay-src/deploy.sh status
    ssh root@BOX bash /root/rebushido-relay-src/deploy.sh uninstall

Install is idempotent: run it again after changing the server and it
replaces what changed and restarts the service, and says nothing changed
when nothing did. It:

- refuses, changing nothing, if UDP 7845 is bound by anything but this
  service (`ss -ulpn`);
- puts the server in `/opt/rebushido-relay/` (root's, read-only) and the unit
  `rebushido-relay.service` (this folder's copy) in `/etc/systemd/system/`,
  enables and starts it, and checks that it listens;
- adds `ufw allow 7845/udp` only if ufw is active and does not allow it
  already, and remembers that it did (`/opt/rebushido-relay/ufw-rule-added`);
- prints every change it made.

`uninstall` stops and disables the service, removes the unit and the
folder, and removes the ufw rule only if install added it. Nothing else on
the box is touched: no other service is restarted or reconfigured, and
nothing of Docker's, nginx's, a VPN's or a DNS filter's is read or changed.

The service runs as a `DynamicUser`: systemd makes an unprivileged user for
each start and drops it after. The server owns no file and writes none, so a
fixed account would only be something to create, keep and remove; a dynamic
one leaves nothing behind. The unit also has no capabilities, a read-only
system (`ProtectSystem=strict`, no write paths), no home, private `/tmp` and
devices, only IPv4, IPv6 and Unix sockets, no W+X memory, the
`@system-service` system calls, and limits of 64 files, 8 tasks and 256 MB.
`Restart=on-failure`.

### The firewall

Two firewalls may stand between a player and the server, and both must let
UDP 7845 in:

- the host's own, ufw: `deploy.sh` adds the rule when ufw is active;
- a cloud firewall in front of the box (Hetzner Cloud's, set in its console
  or with `hcloud firewall add-rule FIREWALL --direction in --protocol udp
  --port 7845 --source-ips 0.0.0.0/0 --source-ips ::/0`), which no script on
  the box can see: add the inbound UDP 7845 rule there by hand.

Nothing outbound needs opening, and no TCP port is used.

## Resources

About 15 to 20 MB of memory for the Python process; a room costs a few
hundred bytes, so the default limit of 500 rooms is under a megabyte. The
lobby's own traffic is a few small datagrams a second per player while they
look for or wait in a room. A relayed match carries the game's traffic
both ways, in and out of the box, typically a few tens of KB/s per room;
each room is held to 240 datagrams and 300 KB a second, and a datagram over
1200 bytes is dropped. One Python process forwards on the order of tens of
thousands of datagrams a second, so many relayed rooms at once are bound by
one processor before memory.

Limits, all options: 500 rooms in all (then SERVER_FULL); per source IP
(an IPv6 /64), 3 rooms hosted and 6 created a minute, 30 HELLOs and 10 LISTs
or WHOAMIs a second, and 5 wrong passwords a minute (then its guests are
refused for 60 s). Over a limit the server answers RATE_LIMITED at most once
a second and drops the rest.

## What it logs

To stdout (the journal, under systemd: `journalctl -u rebushido-relay`),
once a minute, counts only:

    rooms 12, relayed 2, in 85.3/s, out 79.0/s, forwarded 60.2/s, dropped 0.1/s

plus a line when it starts (the addresses it listens on) and when it stops.
No player's address, name or room code is logged unless it is run with
`--verbose`, which logs each room opening, joining, deciding and closing and
each refusal, with addresses; that is for testing, not for a public server.
