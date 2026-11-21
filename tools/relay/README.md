# The relay server

`rebushido_relay.py` is the server the game's online play meets at: a lobby
(the list of open rooms), a rendezvous (it tells a room's host and guest each
other's addresses so they can punch through their routers and play
directly) and a relay (when they cannot, it passes their game traffic
between them). One Python 3 process (3.8 or newer, the standard library
alone), UDP, port 7845 by default.

It holds no game data and needs no disc. It never reads the game's traffic,
only passes it on inside a relayed room between that room's two addresses.
What it keeps of rooms, in memory and for a room's life only: the room's
code, name, host's and guest's player names, setup code, the salt and
verifier of its password (never the password), the two addresses and their
candidates.

It is also the **ranked ladder** (`ladder.py`, imported by the server;
`docs/NETPLAY.md`, section 17): players known by Ed25519 keys their programs
made, a queue paired by rating and round trip, matches met as a room's are,
results signed by both players, Glicko-2 ratings, the ladder's pages. That is
the one thing it writes to disk, in the folder `--state` names; with no
`--state`, ranked play is off and every ranked request is answered
`DISABLED`. What the folder holds:

    ladder.sqlite (-wal, -shm)   keys, names, ratings, results, the signed reports
    backups/ladder-DATE.sqlite   a copy a day, the last seven
    disputes/match-N-a.rbu       a disputed match's replays from each side

| What | Kept |
| --- | --- |
| a player: public key, name, rating, RD, volatility, record, when made, last seen (to the hour), last rated | two years after last seen; the name held 180 days after |
| a match: the two keys, when, its state, winner and why, rule and stage, both signed reports, the ratings before and after | two years |
| a disputed match's replays | 60 days, at most 4 MB a side and 1 GB in all |

No address is ever written (the limits by address are in memory). The file
is SQLite in write-ahead mode with full synchronisation, each change one
transaction; uploads are written beside and renamed; the backups are
SQLite's online backup, made while it runs. A failure of the ladder's (a
full disk) is logged and the lobby and relay go on.

For the owner:

    python3 /opt/rebushido-relay/ladder.py --show /var/lib/private/rebushido-relay   the ladder as JSON (read only)
    python3 tools/relay/ladder.py --unpack match-7-a.rbu DIR               a dispute's replays
    rebushido --image DISC.cue --replay-check DIR/FIGHT.rbr               ... each played back
    python3 tools/relay/ladder.py --self-test                             RFC 8032's and Glicko-2's cases

A disputed match stays unrated; to settle one by hand, stop the service,
copy the store, and change the match's row with `sqlite3` (there is no tool
for it yet).

## The protocol

Protocol version 3: summarized in `docs/NETPLAY.md`, sections 14, 16 and 17; every field, to the byte, in `relay_client.py` (and the client's `src/port/relay.c`, `room.c`, `watch.c` and `ranked.c`).
Version 3 adds the ranked ladder (kinds 63 `RANK` and 54 `RANKED`); a client
of version 1 or 2 is answered VERSION.
Version 2 adds rooms of several (kinds 57 to 62: a room's members, its
rotation, its matches' tickets, and each match's feed kept in memory for
its spectators while it lasts and 30 seconds after) and each host's round
trip in the list. A client of version 1 is answered VERSION: a release of
protocol 9 (setup codes `P9-`) cannot use a server of version 2, and one of
protocol 10 cannot use a server of version 1.
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
| 57 | ROOM | member to server | a room of several: open, enter, stay, leave, rule, result, a match beside |
| 58 | ROSTER | server to member | the room: members, the rotation's match (its ticket to its players), matches beside |
| 59, 60 | FEED, FEED_ACK | a match's host, server | the match's settings, its fight's start, inputs and hashes; what the server has |
| 61, 62 | WATCH, SCENE | spectator, server | what it has; what it lacks |
| 63 | RANK | client to server | the ladder: hello, name, queue, stay, leave, report, upload, a list (name, queue and report signed) |
| 54 | RANKED | server to client | the player, the queue, the match, its result, a list's rows |

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
    python3 tools/relay/rebushido_relay.py --state DIR        # the ranked ladder too, its store in DIR

`--listen ADDR:PORT` (IPv6 as `[::]:7845`) may be given several times; with
none it listens on `0.0.0.0:7845` and, where the system has IPv6,
`[::]:7845`. Each datagram is answered from the socket it came in on, and a
relayed one goes out through the socket its receiver talks to, so a room
whose host is on IPv4 and guest on IPv6 is relayed too. Ctrl+C or SIGTERM
ends it cleanly. `--help` lists the rest: the punch window, the timeouts
and every limit.

The test starts the server on `127.0.0.1` and `[::1]` (the IPv6 part is
skipped where there is none) on ports the system picks and drives fake
clients through every state, refusal and limit, and the ladder (identities
and forged signatures, names, the queue, agreeing and disputed results,
leavers, limits, a restart, the backup, Glicko-2's worked example), in
about 45 seconds:

    python tools/relay/test_relay.py          # Windows
    python3 tools/relay/test_relay.py         # WSL, Linux

## Deploying to a VPS

`deploy.sh` installs it on an Ubuntu box as a systemd service, as root:

    ssh root@BOX mkdir -p /root/rebushido-relay-src
    scp tools/relay/rebushido_relay.py tools/relay/ladder.py tools/relay/relay_client.py \
        tools/relay/deploy.sh tools/relay/rebushido-relay.service root@BOX:/root/rebushido-relay-src/
    ssh root@BOX bash /root/rebushido-relay-src/deploy.sh             # install or update
    ssh root@BOX bash /root/rebushido-relay-src/deploy.sh status
    ssh root@BOX bash /root/rebushido-relay-src/deploy.sh uninstall   # keeps the ladder's store
    ssh root@BOX bash /root/rebushido-relay-src/deploy.sh uninstall --purge   # ... and removes it

(`scp -r tools/relay` into a folder that is there already copies it into
`relay/` inside it: copy the files.)

Install is idempotent: run it again after changing the server and it
replaces what changed and restarts the service, and says nothing changed
when nothing did. It:

- refuses, changing nothing, if UDP 7845 is bound by anything but this
  service (`ss -ulpn`);
- checks that Python has `sqlite3`, puts the server and `ladder.py` in
  `/opt/rebushido-relay/` (root's, read-only), runs the ladder's self-test
  there, puts the unit `rebushido-relay.service` (this folder's copy) in
  `/etc/systemd/system/`, enables and starts it, and checks that it listens;
- adds `ufw allow 7845/udp` only if ufw is active and does not allow it
  already, and remembers that it did (`/opt/rebushido-relay/ufw-rule-added`);
- prints every change it made.

`uninstall` stops and disables the service, removes the unit and the
folder, and removes the ufw rule only if install added it. It keeps the
ladder's store (`/var/lib/private/rebushido-relay`) unless given `--purge`. Nothing else on
the box is touched: no other service is restarted or reconfigured, and
nothing of Docker's, nginx's, a VPN's or a DNS filter's is read or changed.

The service runs as a `DynamicUser`: systemd makes an unprivileged user for
each start and drops it after. Its one writable folder is its
`StateDirectory=`, `/var/lib/private/rebushido-relay` (and the link
`/var/lib/rebushido-relay`), mode 700, which systemd makes, hands to the user
of each start and keeps across restarts and redeploys; the server is started
with `--state /var/lib/rebushido-relay`. Backing it up is the server's own
work (a copy a day in `backups/`); to keep one off the box as well, copy
`backups/` from time to time. The unit also has no capabilities, a read-only
system (`ProtectSystem=strict`, the state folder alone writable), no home,
private `/tmp` and
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

A room of several's members cost a few hundred bytes each; each match's
feed is its fight's start (about 215 KB, at most 1 MB) and 4 bytes a
frame, kept while the match lasts and `--feed-linger` (30 s) after, so a
busy server holds a few hundred KB a match in play. A spectator coming in
is sent its match's start once, then a few KB a second.

Limits, all options: 500 rooms in all (then SERVER_FULL); per source IP
(an IPv6 /64), 3 rooms hosted and 6 created a minute, 30 HELLOs and 10 LISTs
or WHOAMIs a second, and 5 wrong passwords a minute (then its guests are
refused for 60 s), and 400 WATCHes and FEEDs a second (`--watch-rate`).
Over a limit the server answers RATE_LIMITED at most once a second and drops
the rest. A room of several's member is let go after 20 s unheard
(`--member-timeout`), a match beside a room after 120 s with nobody to play
it (`--side-timeout`).

## What it logs

To stdout (the journal, under systemd: `journalctl -u rebushido-relay`),
once a minute, counts only:

    rooms 12, relayed 2, in 85.3/s, out 79.0/s, forwarded 60.2/s, dropped 0.1/s
    ranked: queue 3, playing 2, matches 40, rated 37, disputed 1, void 2, refused 5

plus a line when it starts (the addresses it listens on, the ladder's file)
and when it stops, one for each ranked match made and decided (the two
names and ratings: the ladder is public), each backup, and each request
refused for a signature that is not its key's.
No player's address, name or room code is logged unless it is run with
`--verbose`, which logs each room opening, joining, deciding and closing and
each refusal, with addresses; that is for testing, not for a public server.
