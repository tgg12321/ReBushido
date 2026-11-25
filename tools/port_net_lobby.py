#!/usr/bin/env python3
"""The ways between two players (docs/NETPLAY.md, section 14), on this PC:
the lobby server of tools/relay/ run here, and two programs of the port
meeting through it, every socket on loopback (127.0.0.x and ::1), each
case a fight to the kill and a rematch over a network made worse on
purpose, desync-checked by the state hashes as tools/port_net.py's
sessions are; and on Windows the same between the window's programs,
hidden (SDL's dummy drivers), the Play online page's restart, and every
refusal shown on the screen.

    python3 tools/port_net_lobby.py headless [--port-base P] [--only NAME...]
    python tools/port_net_lobby.py windows [--port-base P] [--only NAME...]
    python3 tools/port_net_lobby.py internet --lobby ADDRESS:PORT   through
                                         a real server (the owner's)

`headless` runs the headless program of this system (build/port/rebushido,
or build/port-sdl/windows/rebushido-headless.exe on Windows):

    punch    a locked room, the right password, a hole punched (127.0.0.2
             and 127.0.0.3), 60 ms each way and 5% lost
    relay    no punch allowed (--net-paths relay): the server passes every
             datagram on
    mapped   the host's router's port (--mapped), the only way the guest
             tries
    v6       IPv6 on ::1, the server too
    direct6  no lobby: --host [::1]:P and --join [::1]:P
    punchto  no lobby: --host with --punch-to and --join from a fixed port
    list     the lobby's list (relay_client.py) while a host waits: its
             name, its host, locked, its setup code
    ranked   the ranked ladder (a lobby with a store): two pairs queued and
             paired, one match rated, one disputed by a false report (its
             replays uploaded and played back, then checked and settled by
             tools/relay/disputes.py), a leaver's loss, the
             ladder's lists printed, a forged report refused (after the
             rest, as rooms-crew; ranked-win in the Windows half)
    refused  a wrong password, an incompatible disc, no such room, a lobby
             that does not answer: each turned away, and why
    four-punch   four players on four PCs (docs/NETPLAY.md, section 18): a
             locked room of four (--players 4), three guests by its code,
             each at a seat of its own, each a hole punched to the host
             (127.0.0.2 to .5); a fight of two on two, every fighter a
             player's; every hash the same on all four
    four-relay   the same through the lobby's relay alone, each guest's
             seat its own relayed pair
    rooms-four   a room of several whose matches are of four (--room-size
             4): Ayame, Bunta, Chie and Dai play, Sora watches; the second
             match, its order turned, left in its fight by Dai, whose
             fighter the CPU plays on to the match's end (after the rest,
             as are the two above; the Windows half's are -win)

`windows` (Windows' own Python) adds the window's programs: a session
headless and with a window (tools/port_net.py's, with a window: in real
time), the menu's restart (_spawnv) by address and by a room with a
password browsed and joined from the page, the router's port mapped by a
fake router (tools/relay/fake_router.py) and the guest coming through it,
and the refusals of the window: another build, disc, mods or protocol,
nobody at the address, the host gone quiet, a wrong password and an
incompatible room, each said on the guest's screen and the host's.

Every case runs at once, each with its own server and ports from
--port-base (PORTS_PER_CASE apart). Outputs in build/net-lobby/
(build/net-lobby-windows/ from Windows). Exits 1
if one failed. Needs the retail disc's image as tools/port_net.py does,
and says so and passes without it."""
import argparse
import ctypes
import json
import os
import re
import shutil
import socket
import subprocess
import sys
import threading
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))
sys.path.insert(0, os.path.join(REPO, 'tools', 'relay'))

import playtest  # noqa: E402
import port_net  # noqa: E402

IS_WINDOWS = os.name == 'nt'
HEADLESS = os.path.join(REPO, 'build/port-sdl/windows/rebushido-headless.exe' if IS_WINDOWS else 'build/port/rebushido')
WINDOW = os.path.join(REPO, 'build/port-sdl/windows/rebushido.exe' if IS_WINDOWS else 'build/port-sdl/linux/rebushido')
BASE_PORT = 45000
PORTS_PER_CASE = 10
# (each system's half its own: the gate runs the Linux and the Windows
# halves at once, and their cases of one name met in one folder, one
# half's start emptying it under the other's programs)
OUT = os.path.join(REPO, 'build', 'net-lobby-windows' if IS_WINDOWS else 'net-lobby')
SECONDS = 420
PASSWORD = 'sesame7'
TROUBLE = ['--net-latency', '60', '--net-loss', '5']
DUMMY = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', **port_net.NO_CONTROLLERS)
# A plan is made for one program's names (playtest's globals): one at a
# time, the cases running side by side.
PLAN_LOCK = threading.Lock()
# where the online pages' cases copy their pictures (RB_ONLINE_PICTURES), or none
PICTURES = os.environ.get('RB_ONLINE_PICTURES')
_write_session_plan = port_net.write_session_plan


def _locked_plan(program, side, folder, *more):
    with PLAN_LOCK:
        return _write_session_plan(program, side, folder, *more)


port_net.write_session_plan = _locked_plan


def read(path):
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            return f.read()
    except OSError:
        return ''


class Case:
    """One case: its folder, its ports, what it started, its problems."""

    def __init__(self, name, base):
        self.name = name
        self.base = base
        self.folder = os.path.join(OUT, name)
        # a run's files are its own: what an earlier run left (its room's
        # code among them) would be read for this one's
        if os.path.isdir(self.folder) and os.path.dirname(os.path.abspath(self.folder)) == os.path.abspath(OUT):
            shutil.rmtree(self.folder)
        os.makedirs(self.folder, exist_ok=True)
        self.procs = []
        self.problems = []
        self.said = []

    def port(self, n):
        return self.base + n

    def start(self, label, cmd, env=None):
        if os.path.basename(cmd[0]).lower().startswith('rebushido') and cmd[0] != HEADLESS:
            # a window's program: never a visible window, never a sound
            env = port_net.window_env(env if env is not None else dict(os.environ))
            cmd = port_net.window_cmd(cmd)
        out = open(os.path.join(self.folder, label + '.out'), 'wb')
        err = open(os.path.join(self.folder, label + '.err'), 'wb')
        proc = subprocess.Popen(cmd, cwd=REPO, stdout=out, stderr=err, stdin=subprocess.DEVNULL, env=env)
        self.procs.append((proc, out, err))
        return proc

    def err(self, label):
        return read(os.path.join(self.folder, label + '.err'))

    def server(self, *more):
        """The lobby server on 127.0.0.1 and ::1 at the case's port 0."""
        log = open(os.path.join(self.folder, 'server.log'), 'wb')
        proc = subprocess.Popen([sys.executable, 'tools/relay/rebushido_relay.py', '--listen',
                                 '127.0.0.1:%d' % self.port(0), '--listen', '[::1]:%d' % self.port(0),
                                 '--verbose'] + list(more),
                                cwd=REPO, stdout=log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)
        self.procs.append((proc, log, None))
        until = time.time() + 10
        while time.time() < until and 'listening on' not in read(os.path.join(self.folder, 'server.log')):
            time.sleep(0.05)
        return '127.0.0.1:%d' % self.port(0)

    def wait_for(self, label, pattern, seconds):
        """The first match of `pattern` in what `label` printed, waited for."""
        until = time.time() + seconds
        while time.time() < until:
            m = re.search(pattern, self.err(label) + read(os.path.join(self.folder, label + '.log')))
            if m:
                return m
            time.sleep(0.1)
        return None

    def stop(self):
        for name in os.listdir(self.folder):
            if name.endswith('.log') and name != 'server.log' and name != 'router.log':
                driver = port_net.heard_only_dummy(read(os.path.join(self.folder, name)))
                if driver:
                    self.fail('%s played sound through %s, not the dummy driver' % (name, driver))
        for proc, out, err in self.procs:
            if proc.poll() is None:
                proc.kill()
            proc.wait()
            out.close()
            if err is not None:
                err.close()

    def fail(self, text):
        self.problems.append('%s: %s' % (self.name, text))


def wait_all(procs, seconds):
    start = time.time()
    codes = []
    for proc in procs:
        try:
            codes.append(proc.wait(timeout=max(1, seconds - (time.time() - start))))
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            codes.append(None)
    return codes


def wait_session(case, labels, procs, seconds):
    """wait_all, but a program that could not play online keeps its
    window's message up until a key: there both are ended at once."""
    until = time.time() + seconds
    while time.time() < until and any(p.poll() is None for p in procs):
        if any('could not play online' in case.err(label) for label in labels):
            for proc in procs:
                if proc.poll() is None:
                    proc.kill()
            break
        time.sleep(0.25)
    return wait_all(procs, 5)


def plan_args(case, program, side):
    """The session's plan of tools/port_net.py: the select screen, a fight
    to the kill, the rematch's; and its trace."""
    path = port_net.write_session_plan(os.path.join(REPO, program), side, case.folder)
    return ['--plan', path, '--trace', '--out', os.path.join(case.folder, 'side%d' % side)]


def judge_session(case, labels, codes, path_words=None, both_paths=True):
    """Both ended well, neither found a desync, the hashes agree and are
    enough, the guest's leaving was heard; and the way taken."""
    hashes = []
    for side, label in enumerate(labels):
        err = case.err(label)
        if 'DESYNC' in err:
            case.fail('the %s found a desync' % label)
        if codes[side] != 0:
            case.fail('the %s ended with %s (%s)' % (label, codes[side], os.path.join(case.folder, label + '.err')))
        hashes.append(port_net.hashes_of(err))
        way = re.search(r'relay: the way is (.*)|relay: (no hole could be punched)', err)
        if way:
            case.said.append('%s: %s' % (label, way.group(1) or 'the relay'))
        if path_words and (side == 1 or both_paths) and not re.search(path_words, err):
            case.fail('the %s did not take the way %r (%s)' % (label, path_words, way.group(0) if way else 'none'))
    common = sorted(set(hashes[0]) & set(hashes[1]))
    differ = [f for f in common if hashes[0][f] != hashes[1][f]]
    if differ:
        case.fail('the state hashes differ from frame %d' % differ[0])
    if len(common) < 40:
        case.fail('too few state hashes in common (%d)' % len(common))
    if not case.problems:
        rollbacks = [re.search(r'netplay: \d+ frames; (\d+) rollbacks', case.err(label)) for label in labels]
        case.said.append('%d state hashes the same on both, the last %s at frame %d%s' % (
            len(common), hashes[0][common[-1]], common[-1],
            '; rollbacks %s' % ', '.join(r.group(1) for r in rollbacks if r) if any(rollbacks) else ''))


def lobby_session(case, program, host_more, guest_more, path_words, lobby_at=None, host_ip='127.0.0.2',
                  guest_ip='127.0.0.3', window=False, both_paths=True):
    """A room at the lobby (or `lobby_at`'s), hosted and joined by its code,
    with port_net's plan; judged by judge_session."""
    lobby = lobby_at or case.server()
    env = DUMMY if window else None
    win = []
    if window:
        cfg = os.path.join(case.folder, 'window.cfg')
        with open(cfg, 'w', encoding='utf-8', newline='\n') as f:
            f.write('fullscreen = off\nnet_portmap = off\n')
        win = ['--config', cfg, '--windowed', '--quick-loading', 'on']
    common = ['--image', playtest.RETAIL_CUE, '--lobby', lobby] + TROUBLE
    host = case.start('host', [program] + common + plan_args(case, program, 0) + win + [
        '--log', os.path.join(case.folder, 'host.log'), '--host', '%s:%d' % (host_ip, case.port(1)), '--room',
        'Room ' + case.name, '--player', 'Host', '--room-password', PASSWORD, '--card1',
        os.path.join(case.folder, 'card1.mcd'), '--card2', os.path.join(case.folder, 'card2.mcd')] + host_more
        if window else [program] + common + plan_args(case, program, 0) + [
        '--host', '%s:%d' % (host_ip, case.port(1)), '--room', 'Room ' + case.name, '--player', 'Host',
        '--room-password', PASSWORD, '--card1', os.path.join(case.folder, 'card1.mcd'), '--card2',
        os.path.join(case.folder, 'card2.mcd')] + host_more, env)
    code = case.wait_for('host', r"room's code is (\w+)", 30)
    if code is None:
        case.fail('the host was given no room code')
        return
    guest = case.start('guest', [program] + common + plan_args(case, program, 1) + win + (
        ['--log', os.path.join(case.folder, 'guest.log')] if window else []) + [
        '--join', code.group(1), '--net-bind', '%s:%d' % (guest_ip, case.port(2)), '--player', 'Guest',
        '--room-password', PASSWORD] + guest_more, env)
    codes = wait_session(case, ['host', 'guest'], [host, guest], SECONDS)
    judge_session(case, ['host', 'guest'], codes, path_words, both_paths)
    for card in ('card1.mcd', 'card2.mcd'):
        if os.path.exists(os.path.join(case.folder, card)):
            case.fail('%s was written, and nothing of a session is' % card)


# ---- the headless cases ----

def case_punch(case):
    lobby_session(case, HEADLESS, [], [], r'the way is a punched hole')


def case_relay(case):
    lobby_session(case, HEADLESS, ['--net-paths', 'relay'], ['--net-paths', 'relay'], r'no hole could be punched')


def case_mapped(case):
    lobby_session(case, HEADLESS, ['--mapped', '127.0.0.2:%d' % case.port(1)], ['--net-paths', 'mapped,relay'],
                  r"the way is the host's mapped port", both_paths=False)


def case_v6(case):
    case.server()
    lobby_session(case, HEADLESS, [], [], r'the way is IPv6, direct', lobby_at='[::1]:%d' % case.port(0),
                  host_ip='[::1]', guest_ip='[::1]')


def direct_session(case, host_bind, guest_join, guest_bind, host_more=()):
    host = case.start('host', [HEADLESS, '--image', playtest.RETAIL_CUE] + TROUBLE + plan_args(case, HEADLESS, 0) + [
        '--host', host_bind, '--no-cards'] + list(host_more))
    time.sleep(0.5)
    guest = case.start('guest', [HEADLESS, '--image', playtest.RETAIL_CUE] + TROUBLE + plan_args(case, HEADLESS, 1)
                       + ['--join', guest_join, '--net-bind', guest_bind, '--no-cards'])
    judge_session(case, ['host', 'guest'], wait_all([host, guest], SECONDS))


def case_direct6(case):
    direct_session(case, '[::1]:%d' % case.port(1), '[::1]:%d' % case.port(1), '[::1]:%d' % case.port(2))


def case_punchto(case):
    direct_session(case, '127.0.0.2:%d' % case.port(1), '127.0.0.2:%d' % case.port(1), '127.0.0.3:%d' % case.port(2),
                   ['--punch-to', '127.0.0.3:%d' % case.port(2)])
    if 'a punched hole (no lobby)' not in case.err('host'):
        case.fail('the host did not say it punched to the guest')


def case_list(case):
    import relay_client as rc
    import socket
    lobby = case.server()
    host = case.start('host', [HEADLESS, '--image', playtest.RETAIL_CUE, '--no-cards', '--frames', '600', '--lobby',
                               lobby, '--host', '127.0.0.2:%d' % case.port(1), '--room', 'Listed room', '--player',
                               'Lister', '--room-password', PASSWORD])
    if case.wait_for('host', r"room's code is (\w+)", 30) is None:
        case.fail('no room code')
        return
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.4', 0))
    sock.settimeout(2)
    sock.sendto(rc.list_rooms(0), ('127.0.0.1', case.port(0)))
    try:
        data, _ = sock.recvfrom(2048)
    except socket.timeout:
        data = b''
    sock.close()
    rooms = rc.parse_rooms(data) if data else None
    host.kill()
    found = [r for r in (rooms or {}).get('rooms', []) if r.get('name') == 'Listed room']
    if not found:
        case.fail('the room is not in the lobby\'s list (%r)' % (rooms,))
        return
    room = found[0]
    setup = re.search(r'setup code is (\S+)', case.err('host'))
    if not room.get('locked') or room.get('player') != 'Lister' or room.get('players') != 1 or \
            (setup and room.get('setup') != setup.group(1)):
        case.fail('the room is listed wrongly: %r' % room)
    else:
        case.said.append('listed: %s by %s, %d/2, locked, setup %s' % (room['name'], room['player'], room['players'],
                                                                      room['setup']))


def refused_run(case, label, cmd, words, seconds=60, env=None):
    proc = case.start(label, cmd, env)
    code = wait_all([proc], seconds)[0]
    said = case.err(label)
    if code in (0, None) or not re.search(words, said):
        case.fail('%s: not turned away as it must be (exit %s; %s)' % (label, code, os.path.join(case.folder,
                                                                                                label + '.err')))
    else:
        m = re.search(r'could not play online: (.*)', said)
        case.said.append('%s: %s' % (label, m.group(1) if m else 'refused'))


def case_refused(case):
    lobby = case.server()
    base = [HEADLESS, '--image', playtest.RETAIL_CUE, '--no-cards', '--lobby', lobby]
    case.start('host', base + ['--frames', '30000', '--host', '127.0.0.2:%d' % case.port(1), '--room', 'Locked',
                               '--room-password', PASSWORD])
    code = case.wait_for('host', r"room's code is (\w+)", 30)
    if code is None:
        case.fail('no room code')
        return
    code = code.group(1)
    runs = [('wrong', ['--join', code, '--room-password', 'not-it', '--net-bind', '127.0.0.3:0'],
             r'The password is not'),
            ('nopass', ['--join', code, '--net-bind', '127.0.0.3:0'], r'locked: it needs its password'),
            ('incompatible', ['--join', code, '--room-password', PASSWORD, '--session-differ', 'disc',
                              '--net-bind', '127.0.0.5:0'], r'not compatible with yours.*disc image'),
            ('noroom', ['--join', 'ZZZZZZ', '--net-bind', '127.0.0.6:0'], r'There is no room ZZZZZZ')]
    threads = [threading.Thread(target=refused_run, args=(case, n, base + more, w)) for n, more, w in runs]
    silent = [HEADLESS, '--image', playtest.RETAIL_CUE, '--no-cards', '--lobby', '127.0.0.1:%d' % case.port(5),
              '--join', 'AAAAAA', '--net-bind', '127.0.0.7:0']
    threads.append(threading.Thread(target=refused_run, args=(case, 'silent', silent, r'does not answer', 40)))
    for t in threads:
        t.start()
    for t in threads:
        t.join()


# ---- a room of several (docs/NETPLAY.md, section 16) ----

# The lobby's limits, for six programs of one address.
ROOM_SERVER = ['--rooms-per-ip', '100', '--creations-per-min', '1000', '--hello-rate', '2000', '--query-rate', '1000',
               '--watch-rate', '100000']
ROOM_SEED = 4242
# Bunta leaves the second match in the middle of its fight, at this frame
# of the session (its fight begins near 1,450 and is decided near 2,000).
LEAVE_FRAME = 1700


def room_segments(text):
    """What one program printed, by the room's match it was of: {number:
    {'role': 'player 1'|'player 2'|'watch', 'hashes': {frame: hash},
    'text': ...}}."""
    out = {}
    current = None
    for line in text.split('\n'):
        m = re.match(r'room: match (\d+) \((.*) vs (.*)\) begins: this program plays (player \d)', line)
        w = re.match(r'room: watching match (\d+) \((.*) vs (.*)\)', line)
        if m or w:
            number = int((m or w).group(1))
            current = out.setdefault(number, {'role': m.group(4) if m else 'watch', 'hashes': {}, 'text': [],
                                              'pair': ((m or w).group(2), (m or w).group(3))})
        if current is None:
            continue
        current['text'].append(line)
        h = re.match(r'netplay: frame (\d+) hash ([0-9A-F]+)', line)
        if h:
            current['hashes'][int(h.group(1))] = h.group(2)
    return out


def case_rooms(case):
    """Six programs in one room of several: a rotation of three (Ayame,
    Bunta, Chie; a fixed turn), each match watched by the one not in it; a
    spectator (Sora) who comes in the middle of the first match's fight;
    Bunta leaving the second match in the middle of its fight; and Dai and
    Eiko playing a match beside the room's meanwhile. Every hash of a match
    the same on every program that ran it, its players' and its
    spectators'."""
    lobby = case.server(*ROOM_SERVER)
    with PLAN_LOCK:
        plan = port_net.write_room_plan(HEADLESS, case.folder)
    base = [HEADLESS, '--image', playtest.RETAIL_CUE, '--no-cards', '--lobby', lobby, '--net-plan-pads',
            '--net-seed', str(ROOM_SEED), '--plan', plan, '--trace'] + TROUBLE

    def member(label, *more):
        return case.start(label, base + ['--out', os.path.join(case.folder, label), '--player', label.capitalize()] +
                          list(more))

    host = member('ayame', '--room-open', 'Many', '--room-rule', 'fixed', '--room-matches', '3')
    code = case.wait_for('ayame', r"room's code is (\w+) \(a room of several", 30)
    if code is None:
        case.fail('the room of several was given no code')
        return
    code = code.group(1)
    procs = [host]
    procs.append(member('bunta', '--room-enter', code, '--room-matches', '3', '--leave-at', '2:%d' % LEAVE_FRAME))
    # the rotation's order is the order they come in: Chie once Bunta is in
    # (under load the two programs started together came in either way)
    if case.wait_for('bunta', r'room: in room', 60) is None:
        case.fail('Bunta never came into the room')
    procs.append(member('chie', '--room-enter', code, '--room-matches', '3'))
    procs.append(member('dai', '--room-enter', code, '--room-watcher', '--room-no-watch', '--room-side', 'open'))
    procs.append(member('eiko', '--room-enter', code, '--room-watcher', '--room-no-watch', '--room-side', 'join'))
    # the spectator comes once the first match's fight is under way
    if case.wait_for('ayame', r'watch: the lobby has the fight\'s start', 240) is None:
        case.fail('the first match\'s host never gave the lobby its fight\'s start')
    time.sleep(1.0)
    procs.append(member('sora', '--room-enter', code, '--room-watcher', '--room-matches', '3'))
    labels = ['ayame', 'bunta', 'chie', 'dai', 'eiko', 'sora']
    codes = wait_all(procs, SECONDS)
    segments = {}
    for label, code_ in zip(labels, codes):
        err = case.err(label)
        if code_ != 0:
            case.fail('%s ended with %s (%s)' % (label, code_, os.path.join(case.folder, label + '.err')))
        if 'DESYNC' in err:
            case.fail('%s found a desync' % label)
        segments[label] = room_segments(err)
    judge_rooms(case, segments)


def case_rooms_crew(case):
    """A room of several whose match is Crew 2 (docs/PORT.md, "Match rules",
    "Crew"): Ayame and Bunta play the rotation's first match, each picking
    his crew's second place with his own pad, and Chie watches it. The
    match is decided at the second fight's end, the result's card, as a
    Stock match's is: there it ends, on all three at the same frame, its
    hashes the same on all."""
    lobby = case.server(*ROOM_SERVER)
    with PLAN_LOCK:
        plan = port_net.write_room_plan(HEADLESS, case.folder, crew=True)
    base = [HEADLESS, '--image', playtest.RETAIL_CUE, '--no-cards', '--lobby', lobby, '--net-plan-pads',
            '--net-seed', str(ROOM_SEED), '--plan', plan, '--trace', '--match', 'crew:2'] + TROUBLE

    def member(label, *more):
        return case.start(label, base + ['--out', os.path.join(case.folder, label), '--player', label.capitalize()] +
                          list(more))

    procs = [member('ayame', '--room-open', 'Crews', '--room-rule', 'fixed', '--room-matches', '1')]
    code = case.wait_for('ayame', r"room's code is (\w+) \(a room of several", 30)
    if code is None:
        case.fail('the room of several was given no code')
        return
    procs.append(member('bunta', '--room-enter', code.group(1), '--room-matches', '1'))
    procs.append(member('chie', '--room-enter', code.group(1), '--room-watcher', '--room-matches', '1'))
    labels = ['ayame', 'bunta', 'chie']
    codes = wait_all(procs, SECONDS)
    segments = {}
    for label, code_ in zip(labels, codes):
        err = case.err(label)
        if code_ != 0:
            case.fail('%s ended with %s (%s)' % (label, code_, os.path.join(case.folder, label + '.err')))
        if 'DESYNC' in err:
            case.fail('%s found a desync' % label)
        segments[label] = room_segments(err)
    players = [label for label in labels if segments[label].get(1, {}).get('role', 'watch') != 'watch']
    if sorted(players) != ['ayame', 'bunta'] or 1 not in segments['chie']:
        case.fail('match 1 was not Ayame and Bunta\'s, watched by Chie: %r' %
                  {label: segments[label].get(1, {}).get('role') for label in labels})
        return
    for label in players:
        if "session: the host's match rule is crew, 2" not in case.err(label) and \
                'match rule crew, 2' not in case.err(label):
            case.fail('%s did not play the host\'s Crew 2' % label)
        if 'room: match 1 over: its' not in case.err(label):
            case.fail('%s\'s match 1 did not end with a winner' % label)
    # both fights fought: the match was not over at the first's card
    outs = ''.join(read(os.path.join(case.folder, label + '.out')) for label in players)
    if 'cp kill2 ' not in outs:
        case.fail('no player printed the second fight\'s kill: the match did not go on to it')
    # the rule first, the crews of different fighters, and the first
    # fight's winner wounded into the second, where a player printed them
    plan_text = read(plan.replace('room_plan.txt', 'room.toml'))
    printed = set()
    for label in players:
        failed, seen = port_net.plan_claims(plan, read(os.path.join(case.folder, label + '.out')), plan_text)
        printed.update(seen)
        for name, claim in failed:
            case.fail('%s at `%s`: not %s' % (label, name, claim))
    for name in ('crew2', 'stages', 'card', 'fight2'):
        if name not in printed:
            case.fail('no player printed `%s`' % name)
    # back to the room: each player's session ended with the match, his
    # own game put back, the room going on (its rotation's next ask), not
    # at the game's top menu
    for label in players:
        after = case.err(label).split('room: match 1 over', 1)[-1]
        if "online: the player's own game put back" not in after:
            case.fail('%s did not go back to the room after the match' % label)
        if 'the players went back to the top menu' in case.err(label):
            case.fail('%s\'s room match ended at the top menu, not at the match\'s end' % label)
    # the players' hashes alike, and the spectator's (fewer: it runs from
    # the fight's start) the same as theirs, as case_rooms holds them
    hashes = [segments[label][1]['hashes'] for label in labels]
    common = sorted(set(hashes[0]) & set(hashes[1]))
    differ = [f for f in common if hashes[0][f] != hashes[1][f]]
    watched = sorted(set(hashes[2]) & set(common))
    if differ:
        case.fail('match 1: the players\' hashes differ from frame %d' % differ[0])
    elif len(common) < 40:
        case.fail('match 1: too few hashes in common on the players (%d)' % len(common))
    elif len(watched) < 5 or any(hashes[2][f] != hashes[0][f] for f in watched):
        case.fail('match 1: Chie\'s hashes are not the players\' (%d in common)' % len(watched))
    else:
        case.said.append('a Crew 2 match in a room: Ayame and Bunta picked their crews, two fights, decided at '
                         'the result\'s card; %d hashes the same on both players, %d of them Chie\'s watching'
                         % (len(common), len(watched)))


def case_rooms_duo(case):
    """A room of several whose match is two on two (docs/PORT.md, "Two on
    two"; --teams cpu, the partners the CPU's): Ayame and Bunta play the
    rotation's first match, a single fight of four, and Chie watches it.
    The fight decided, the match is over: the players' hashes the same,
    and the spectator's theirs, who ran the four from the fight's start."""
    lobby = case.server(*ROOM_SERVER)
    with PLAN_LOCK:
        plan = port_net.write_room_plan(HEADLESS, case.folder, duo=True)
    base = [HEADLESS, '--image', playtest.RETAIL_CUE, '--no-cards', '--lobby', lobby, '--net-plan-pads',
            '--net-seed', str(ROOM_SEED), '--plan', plan, '--trace', '--teams', 'cpu'] + TROUBLE

    def member(label, *more):
        return case.start(label, base + ['--out', os.path.join(case.folder, label), '--player', label.capitalize()] +
                          list(more))

    procs = [member('ayame', '--room-open', 'Fours', '--room-rule', 'fixed', '--room-matches', '1')]
    code = case.wait_for('ayame', r"room's code is (\w+) \(a room of several", 30)
    if code is None:
        case.fail('the room of several was given no code')
        return
    procs.append(member('bunta', '--room-enter', code.group(1), '--room-matches', '1'))
    procs.append(member('chie', '--room-enter', code.group(1), '--room-watcher', '--room-matches', '1'))
    labels = ['ayame', 'bunta', 'chie']
    codes = wait_all(procs, SECONDS)
    segments = {}
    for label, code_ in zip(labels, codes):
        err = case.err(label)
        if code_ != 0:
            case.fail('%s ended with %s (%s)' % (label, code_, os.path.join(case.folder, label + '.err')))
        if 'DESYNC' in err:
            case.fail('%s found a desync' % label)
        segments[label] = room_segments(err)
    players = [label for label in labels if segments[label].get(1, {}).get('role', 'watch') != 'watch']
    if sorted(players) != ['ayame', 'bunta'] or 1 not in segments['chie']:
        case.fail('match 1 was not Ayame and Bunta\'s, watched by Chie: %r' %
                  {label: segments[label].get(1, {}).get('role') for label in labels})
        return
    for label in players:
        if "session: the host's teams are two on two" not in case.err(label):
            case.fail('%s did not play the host\'s two on two' % label)
        if 'room: match 1 over: its' not in case.err(label):
            case.fail('%s\'s match 1 did not end with a winner' % label)
    outs = ''.join(read(os.path.join(case.folder, label + '.out')) for label in players)
    if 'cp kill ' not in outs:
        case.fail('no player printed the fight\'s end (a team out)')
    hashes = [segments[label][1]['hashes'] for label in labels]
    common = sorted(set(hashes[0]) & set(hashes[1]))
    differ = [f for f in common if hashes[0][f] != hashes[1][f]]
    watched = sorted(set(hashes[2]) & set(common))
    if differ:
        case.fail('match 1: the players\' hashes differ from frame %d' % differ[0])
    elif len(common) < 40:
        case.fail('match 1: too few hashes in common on the players (%d)' % len(common))
    elif len(watched) < 5 or any(hashes[2][f] != hashes[0][f] for f in watched):
        case.fail('match 1: Chie\'s hashes are not the players\' (%d in common)' % len(watched))
    else:
        case.said.append('a two on two match in a room: Ayame and Bunta, their partners the CPU\'s, to a team '
                         'out; %d hashes the same on both players, %d of them Chie\'s watching'
                         % (len(common), len(watched)))


def judge_rooms(case, segments):
    """The rotation's order, every match's hashes alike on all who ran it,
    the spectators', the leave and the match beside."""
    turns = {}
    for label, seg in segments.items():
        for number, part in seg.items():
            if part['role'] != 'watch':
                turns[number] = part['pair']
    rotation = [turns.get(n) for n in (1, 2, 3)]
    if rotation != [('Ayame', 'Bunta'), ('Bunta', 'Chie'), ('Chie', 'Ayame')]:
        case.fail('the rotation was not A+B, B+C, C+A: %r' % rotation)
    else:
        case.said.append('the rotation: %s' % ', '.join('%s+%s' % p for p in rotation))
    numbers = sorted({n for seg in segments.values() for n in seg})
    for number in numbers:
        who = {label: seg[number] for label, seg in segments.items() if number in seg}
        players = [label for label, part in who.items() if part['role'] != 'watch']
        watchers = [label for label, part in who.items() if part['role'] == 'watch']
        if len(players) != 2:
            case.fail('match %d: %d programs played it (%s)' % (number, len(players), ', '.join(who)))
            continue
        reference = who[players[0]]['hashes']
        frames = set(reference)
        for label in players[1:] + watchers:
            mine = who[label]['hashes']
            common = sorted(set(mine) & frames)
            differ = [f for f in common if mine[f] != reference[f]]
            if differ:
                case.fail('match %d: %s\'s hash of frame %d is not %s\'s' % (number, label, differ[0], players[0]))
            least = 10 if label in watchers else 40
            if len(common) < least:
                case.fail('match %d: %s has %d hashes in common with %s' % (number, label, len(common), players[0]))
        held = {label: re.search(r'(\d+) hashes the same as its players\'', '\n'.join(who[label]['text']))
                for label in watchers}
        case.said.append('match %d (%s vs %s): played by %s, %d hashes; watched by %s' % (
            number, who[players[0]]['pair'][0], who[players[0]]['pair'][1], ' and '.join(players),
            len(reference), ', '.join('%s (%s held)' % (w, held[w].group(1) if held[w] else 'none')
                                      for w in watchers) or 'nobody'))
        for label in watchers:
            if not held[label] or int(held[label].group(1)) < 5:
                case.fail('match %d: %s held too few of its hashes to the players\' (%s)' % (
                    number, label, held[label].group(0) if held[label] else 'none'))
    # the spectator came in the middle of the first match's fight
    sora = segments.get('sora', {})
    first = sora.get(1)
    if first is None:
        case.fail('sora did not watch the first match')
    else:
        start = re.search(r'watch: from frame (\d+)', '\n'.join(first['text']))
        if not start or not first['hashes'] or min(first['hashes']) > int(start.group(1)) + 30:
            case.fail('sora did not run the first match from its fight\'s start')
        else:
            case.said.append('sora came in the middle of match 1 and ran it from its fight\'s start, frame %s' %
                             start.group(1))
    # Bunta left the second match in its fight: Chie won it, both went on
    bunta = '\n'.join(segments.get('bunta', {}).get(2, {}).get('text', []))
    chie = '\n'.join(segments.get('chie', {}).get(2, {}).get('text', []))
    if 'this player left the session at frame %d' % LEAVE_FRAME not in bunta or \
            'room: match 2 over: its second player won' not in bunta + chie or \
            "the player's own game put back" not in bunta or "the player's own game put back" not in chie:
        case.fail('the leave in the middle of match 2 was not as it must be')
    else:
        case.said.append('Bunta left match 2 at frame %d, in its fight; Chie won it, both back to their own '
                         'game and the room' % LEAVE_FRAME)
    # the match beside: Dai and Eiko
    beside = [part for label in ('dai', 'eiko') for part in segments.get(label, {}).values()
              if part['role'] != 'watch']
    if len(beside) != 2 or 'a match beside the room' not in '\n'.join(beside[0]['text'] + beside[1]['text']):
        case.fail('Dai and Eiko did not play a match beside the room\'s')


# ---- the ranked ladder (docs/NETPLAY.md, section 17) ----

RANKED_SERVER = ROOM_SERVER + ['--ranked-same-address', '--identities-per-day', '100', '--ranked-rate', '100000',
                               '--signed-rate', '100000', '--ranked-first-to', '1', '--gone-timeout', '10',
                               '--report-wait', '3', '--ranked-stages', '0']
# (the stage drawn is the stage pages' first place, the one the scripted
# fight of port_net's plans is written for: on another its presses may
# never cut the other down, and the match never ends)
RANKED_SEED = 5151
# Fumi leaves her match in the middle of its fight, at this frame of the
# session (its fight begins near 1,050, with the ladder's stage taken at once,
# and the first cut is near 1,560)
RANKED_LEAVE_FRAME = 1150


def ranked_logs(text):
    """What a ranked program said: {'match': N, 'state': ..., 'won': ...,
    'before': B, 'after': A, 'digest': hex, 'reason': ..., 'stage': S}."""
    out = {}
    m = re.search(r'ranked: match (\d+): \S+ \((\d+)\) against (\S+) \((\d+)\), first to (\d+), stage (\d+); '
                  r'this program is player (\d)', text)
    if m:
        out.update(match=int(m.group(1)), opponent=m.group(3), first_to=int(m.group(5)), stage=int(m.group(6)),
                   player=int(m.group(7)))
    m = re.search(r'ranked: match \d+ ended \(([^)]*)\): reported ([^,]*), digest ([0-9a-f]+)', text)
    if m:
        out.update(reason=m.group(1), reported=m.group(2), digest=m.group(3))
    m = re.search(r'ranked: match \d+ is (\w+): ([^,]*), (\d+) to (\d+)', text)
    if m:
        out.update(state=m.group(1), won=m.group(2), before=int(m.group(3)), after=int(m.group(4)))
    out['ladder'] = re.findall(r'ranked: the ladder: (\d+)\. (\S+) (\d+)\??', text)
    out['hashes'] = dict((int(f), h) for f, h in re.findall(r'netplay: frame (\d+) hash ([0-9A-F]+)', text))
    return out


def ranked_public(path):
    """The public key of a program's identity file."""
    m = re.search(r'^public ([0-9a-f]{64})$', read(path), re.M)
    return bytes.fromhex(m.group(1)) if m else None


def case_ranked(case):
    """Ranked play through a local lobby with a ladder (its store in the
    case's folder): two pairs queued, paired and played, each match's two
    reports signed; Ayame and Bunta's agree and are rated, Dai's names the
    other winner and his match is disputed, its replays uploaded, and one
    of them played back by --replay-check; Fumi leaves her match in its
    fight and loses it; the ladder's lists printed by a program, as its
    page shows them; and a report forged under Ayame's key refused."""
    import relay_client as rc
    import ladder
    state = os.path.join(case.folder, 'ladder')
    lobby = case.server('--state', state, *RANKED_SERVER)
    with PLAN_LOCK:
        plan = port_net.write_room_plan(HEADLESS, case.folder)
    base = [HEADLESS, '--image', playtest.RETAIL_CUE, '--no-cards', '--lobby', lobby, '--net-plan-pads',
            '--net-seed', str(RANKED_SEED), '--plan', plan, '--trace', '--ranked', '--ranked-matches', '1'] + TROUBLE

    def player(label, *more):
        return case.start(label, base + ['--out', os.path.join(case.folder, label), '--player', label.capitalize(),
                                         '--identity', os.path.join(case.folder, label + '.key'),
                                         '--replays', os.path.join(case.folder, label + '-replays')] + list(more))

    procs = {'ayame': player('ayame')}
    if case.wait_for('ayame', r'ranked: \S+ goes into the queue', 30) is None:
        case.fail('Ayame never queued')
        return
    procs['bunta'] = player('bunta', '--ranked-lists')
    if case.wait_for('bunta', r'ranked: match \d+:', 60) is None:
        case.fail('Ayame and Bunta were never paired')
    procs['chie'] = player('chie')
    if case.wait_for('chie', r'ranked: \S+ goes into the queue', 30) is None:
        case.fail('Chie never queued')
    procs['dai'] = player('dai', '--ranked-lie')
    if case.wait_for('dai', r'ranked: match \d+:', 60) is None:
        case.fail('Chie and Dai were never paired')
    labels = ['ayame', 'bunta', 'chie', 'dai']
    codes = wait_all([procs[label] for label in labels], SECONDS)
    logs = {}
    for label, code in zip(labels, codes):
        if code != 0:
            case.fail('%s ended with %s (%s)' % (label, code, os.path.join(case.folder, label + '.err')))
        if 'DESYNC' in case.err(label):
            case.fail('%s found a desync' % label)
        logs[label] = ranked_logs(case.err(label))
    # the leaver's pair, after: no more than four programs at once
    procs['eiko'] = player('eiko')
    if case.wait_for('eiko', r'ranked: \S+ goes into the queue', 30) is None:
        case.fail('Eiko never queued')
    procs['fumi'] = player('fumi', '--leave-at', '1:%d' % RANKED_LEAVE_FRAME)
    for label, code in zip(['eiko', 'fumi'], wait_all([procs['eiko'], procs['fumi']], SECONDS)):
        if code != 0:
            case.fail('%s ended with %s (%s)' % (label, code, os.path.join(case.folder, label + '.err')))
        logs[label] = ranked_logs(case.err(label))
    judge_ranked(case, logs, state, lobby, rc, ladder)


def judge_ranked(case, logs, state, lobby, rc, ladder):
    a, b, c, d, e, f = (logs[k] for k in ('ayame', 'bunta', 'chie', 'dai', 'eiko', 'fumi'))
    # the pairs, the rule and the stage
    if a.get('match') is None or a.get('match') != b.get('match') or {a.get('player'), b.get('player')} != {1, 2}:
        case.fail('Ayame and Bunta did not play one match: %r, %r' % (a.get('match'), b.get('match')))
        return
    if a.get('first_to') != 1 or a.get('stage') != b.get('stage'):
        case.fail('the rule or the stage is not the lobby\'s on both: %r' % ((a.get('stage'), b.get('stage')),))
    for label in ('ayame', 'bunta'):
        if 'session: the host\'s match rule is first_to, 1' not in case.err(label):
            case.fail('%s did not play the ladder\'s First to 1' % label)
    # agreeing reports, rated, the ratings moved
    if a.get('digest') is None or a.get('digest') != b.get('digest'):
        case.fail('the two digests of match %s differ: %s and %s' % (a.get('match'), a.get('digest'), b.get('digest')))
    if a.get('state') != 'rated' or b.get('state') != 'rated':
        case.fail('Ayame and Bunta\'s match is not rated: %r, %r' % (a.get('state'), b.get('state')))
    elif not (a['before'] == b['before'] == 1500 and a['after'] != 1500 and b['after'] != 1500 and
              (a['after'] > 1500) == (a['won'] == 'won') and (b['after'] > 1500) == (b['won'] == 'won') and
              a['won'] != b['won']):
        case.fail('the ratings did not move as the result: %r, %r' % (a, b))
    else:
        case.said.append('match %d (Ayame and Bunta, the lobby\'s stage taken on both: place %d): both digests '
                         '%s..., rated: '
                         'Ayame %s, 1500 to %d; Bunta %s, 1500 to %d'
                         % (a['match'], a['stage'], a['digest'][:16], a['won'], a['after'], b['won'], b['after']))
    common = sorted(set(a['hashes']) & set(b['hashes']))
    if len(common) < 30 or any(a['hashes'][x] != b['hashes'][x] for x in common):
        case.fail('match %s\'s hashes are not alike on both (%d in common)' % (a.get('match'), len(common)))
    # the ladder printed, as its page shows it
    names = {name: int(rating) for _, name, rating in b['ladder']}
    if names.get('Ayame') != a.get('after') or names.get('Bunta') != b.get('after'):
        case.fail('the ladder Bunta printed is not the ratings: %r' % b['ladder'])
    else:
        case.said.append('the ladder as Bunta\'s program had it: %s' % ', '.join(
            '%s. %s %s' % row for row in b['ladder']))
    # a dispute: Dai reported the other winner
    if c.get('match') is None or c.get('match') != d.get('match'):
        case.fail('Chie and Dai did not play one match')
    elif c.get('state') != 'disputed' or d.get('state') != 'disputed':
        case.fail('Dai\'s false report was not disputed: %r, %r' % (c.get('state'), d.get('state')))
    else:
        uploaded = sorted(n for n in os.listdir(os.path.join(state, 'disputes')) if n.endswith('.rbu'))
        if len(uploaded) != 2:
            case.fail('the dispute\'s replays are not both kept: %r' % uploaded)
        else:
            out = os.path.join(case.folder, 'dispute')
            files = ladder.unpack(os.path.join(state, 'disputes', uploaded[0]), out)
            checked = [port_replay_check(path) for path in files if path.endswith('.rbr')]
            if not checked or any(code != 0 for code, _ in checked):
                case.fail('the disputed replays do not play back: %r' % checked)
            else:
                case.said.append('match %d (Chie and Dai, Dai reporting the other winner): disputed, unrated; both '
                                 'sides\' replays kept (%s), %d played back by --replay-check: %s'
                                 % (c['match'], ', '.join(uploaded), len(checked), checked[0][1]))
                settle_dispute(case, state, c['match'], ladder)
    # a leaver loses
    if f.get('reason') != 'this player left' or e.get('reason') != 'the other left':
        case.fail('the leave was not reported as one: %r, %r' % (e.get('reason'), f.get('reason')))
    elif e.get('state') != 'rated' or e.get('won') != 'won' or f.get('won') != 'lost':
        case.fail('the leaver did not lose: %r, %r' % (e, f))
    else:
        case.said.append('match %d: Fumi left in its fight (frame %d) and lost it: Eiko 1500 to %d, Fumi 1500 to %d'
                         % (e['match'], RANKED_LEAVE_FRAME, e['after'], f['after']))
    # a forgery: a report under Ayame's key, signed by another's
    host, port = lobby.rsplit(':', 1)
    key = ranked_public(os.path.join(case.folder, 'ayame.key'))
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(2.0)
    try:
        sock.sendto(rc.rank(ladder.A_HELLO, None, key=key), (host, int(port)))
        hello = rc.parse_ranked(sock.recv(2048))
        mallory = rc.Identity()
        winner = ladder.WON_FIRST if a.get('player') == 2 else ladder.WON_SECOND
        sock.sendto(rc.rank(ladder.A_REPORT, None, key=key, nonce=hello['nonce'], sign_with=mallory,
                            match=a.get('match') or 0, winner=winner, reason=ladder.R_DECIDED, rule=2, count=1,
                            digest=b'F' * 32, frames=1), (host, int(port)))
        forged = rc.parse_ranked(sock.recv(2048))
        sock.sendto(rc.rank(ladder.A_HELLO, None, key=key), (host, int(port)))
        after = rc.parse_ranked(sock.recv(2048))
    except (OSError, ValueError) as error:
        case.fail('the lobby did not answer the forgery: %r' % error)
        return
    finally:
        sock.close()
    if forged['status'] != ladder.E_BAD_SIGNATURE or after['rating'] != a.get('after'):
        case.fail('a forged report: %s, Ayame\'s rating %r' % (forged['name'], after['rating']))
    elif 'signature is not its key\'s' not in read(os.path.join(case.folder, 'server.log')):
        case.fail('the forgery is not in the lobby\'s log')
    else:
        case.said.append('a report forged under Ayame\'s key: refused (BAD_SIGNATURE), her rating still %d'
                         % after['rating'])


def settle_dispute(case, state, number, ladder):
    """The owner's tools/relay/disputes.py on the dispute, beside the running
    lobby: both sides' replays checked by the headless program, Chie's
    report borne out and Dai's not, then the match settled as the replays
    show and rated."""
    tool = os.path.join(REPO, 'tools', 'relay', 'disputes.py')
    verdict_path = os.path.join(case.folder, 'verdict.json')
    done = subprocess.run([sys.executable, tool, '--state', state, 'check', str(number), '--image',
                           playtest.RETAIL_CUE, '--program', HEADLESS, '--save', verdict_path], cwd=REPO,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=SECONDS)
    said = done.stdout.decode('utf-8', 'replace')
    with open(os.path.join(case.folder, 'disputes-check.txt'), 'w', encoding='utf-8') as f:
        f.write(said)
    if done.returncode != 0 or not os.path.isfile(verdict_path):
        case.fail('disputes.py check of match %d ended %d: %s' % (number, done.returncode, said[-400:]))
        return
    with open(verdict_path, encoding='ascii') as f:
        verdict = json.load(f)
    reports = {r['name']: r['borne_out'] for r in verdict['reports'].values()}
    sides = verdict['sides'].values()
    if verdict['suggest']['state'] != 'rated' or reports != {'Chie': True, 'Dai': False} or \
            not all(side['held'] for side in sides) or verdict['same_fights']['alike'] < 1:
        case.fail('disputes.py did not bear out Chie\'s report and not Dai\'s: %s' % said[-600:])
        return
    done = subprocess.run([sys.executable, tool, '--state', state, 'resolve', str(number), '--winner', 'replays',
                           '--verdict', verdict_path, '--reason', 'the replays of both sides', '--yes'], cwd=REPO,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL, timeout=SECONDS)
    settled = done.stdout.decode('utf-8', 'replace').strip().splitlines()
    store = ladder.Store(state, server=False)
    try:
        m = store.match(number)
    finally:
        store.close()
    if done.returncode != 0 or m['state'] != ladder.M_RATED or m['winner'] != verdict['suggest']['winner_code']:
        case.fail('disputes.py resolve of match %d: %s' % (number, settled[-1:] if settled else done.returncode))
        return
    case.said.append('disputes.py: both sides\' replays hold, %d fight(s) alike on both; %s; %s' % (
        verdict['same_fights']['alike'], verdict['suggest']['why'], settled[-1]))


def port_replay_check(path):
    """`--replay-check` of a replay by the headless program: (exit code,
    its last line)."""
    done = subprocess.run([HEADLESS, '--image', playtest.RETAIL_CUE, '--no-cards', '--replay-check', path], cwd=REPO,
                          stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, stdin=subprocess.DEVNULL, timeout=SECONDS)
    said = [line for line in done.stderr.decode('utf-8', 'replace').splitlines() if line.startswith('replay:')]
    return done.returncode, said[-1] if said else '(nothing said)'


# ---- four players (docs/NETPLAY.md, section 18) ----

FOUR_NAMES = ['Ayame', 'Bunta', 'Chie', 'Dai']


def four_judge(case, labels, codes, path_words, least=60):
    """Every program of a session of four ended well, found no desync,
    took the way `path_words` says; every hash the same on all."""
    hashes = []
    for label, code in zip(labels, codes):
        err = case.err(label)
        if 'DESYNC' in err:
            case.fail('%s found a desync' % label)
        if code != 0:
            case.fail('%s ended with %s (%s)' % (label, code, os.path.join(case.folder, label + '.err')))
        if '4 players on 4 PCs' not in err:
            case.fail('%s did not say the session was of four players on four PCs' % label)
        ways = re.findall(r'relay: the way is (.*)|relay: (no hole could be punched)', err)
        if path_words and not re.search(path_words, err):
            case.fail('%s did not take the way %r' % (label, path_words))
        case.said.append('%s: %s' % (label, '; '.join(a or 'the relay' for a, _ in ways) or 'no way said'))
        hashes.append(port_net.hashes_of(err))
    common = sorted(set.intersection(*(set(h) for h in hashes)))
    for label, mine in zip(labels[1:], hashes[1:]):
        differ = [f for f in sorted(set(mine) & set(hashes[0])) if mine[f] != hashes[0][f]]
        if differ:
            case.fail('%s\'s hash of frame %d is not the host\'s' % (label, differ[0]))
    if len(common) < least:
        case.fail('too few state hashes in common on all four (%d)' % len(common))
    if not case.problems:
        rollbacks = [re.search(r'netplay: \d+ frames; (\d+) rollbacks', case.err(label)) for label in labels]
        case.said.append('%d state hashes the same on all four, the last %s at frame %d; rollbacks %s' % (
            len(common), hashes[0][common[-1]], common[-1], ', '.join(r.group(1) for r in rollbacks if r)))


def four_session(case, paths, path_words):
    """A room of four at the lobby: the host's (--players 4) and three
    guests by its code, each a seat, each its way to the host (`paths`,
    --net-paths: every way, or the relay alone); a fight of four to a team
    out, with port_net's plan of four (every pad's, each program its own:
    --net-plan-pads)."""
    lobby = case.server(*ROOM_SERVER)
    with PLAN_LOCK:
        host_plan = port_net.write_four_plan(HEADLESS, 0, None, case.folder)
        guest_plan = port_net.write_four_plan(HEADLESS, 1, None, case.folder)
    common = ['--image', playtest.RETAIL_CUE, '--lobby', lobby, '--net-plan-pads', '--trace'] + TROUBLE + \
        (['--net-paths', paths] if paths else [])
    labels = [name.lower() for name in FOUR_NAMES]
    procs = [case.start(labels[0], [HEADLESS] + common + [
        '--plan', host_plan, '--out', os.path.join(case.folder, labels[0]), '--host', '127.0.0.2:%d' % case.port(1),
        '--players', '4', '--room', 'Fours ' + case.name, '--player', FOUR_NAMES[0], '--room-password', PASSWORD,
        '--net-seed', '4242', '--card1', os.path.join(case.folder, 'card1.mcd'), '--card2',
        os.path.join(case.folder, 'card2.mcd')])]
    code = case.wait_for(labels[0], r"room's code is (\w+)", 30)
    if code is None:
        case.fail('the host was given no room code')
        return
    for n in (1, 2, 3):
        procs.append(case.start(labels[n], [HEADLESS] + common + [
            '--plan', guest_plan, '--out', os.path.join(case.folder, labels[n]), '--join', code.group(1),
            '--net-bind', '127.0.0.%d:%d' % (2 + n, case.port(1 + n)), '--player', FOUR_NAMES[n],
            '--room-password', PASSWORD]))
    codes = wait_session(case, labels, procs, SECONDS)
    four_judge(case, labels, codes, path_words)
    for card in ('card1.mcd', 'card2.mcd'):
        if os.path.exists(os.path.join(case.folder, card)):
            case.fail('%s was written, and nothing of a session is' % card)


def case_four_punch(case):
    four_session(case, None, r'the way is a punched hole')


def case_four_relay(case):
    four_session(case, 'relay', r'no hole could be punched')


# The second match of the room of four is left in the middle of its fight
# by one of its players, at this frame of its session (its fight begins
# near frame 3,000, after the partners' grid and the stage page).
FOUR_LEAVE_FRAME = 3600


def case_rooms_four(case):
    """A room of several whose matches are of four (--room-size 4): Ayame
    opens it (a fixed turn), Bunta, Chie and Dai play, Sora watches. The
    rotation's first match is Ayame, Bunta, Chie and Dai, Ayame and Chie a
    team against Bunta and Dai, met under the match's code by its ticket,
    each guest at the seat of its place; Sora watches it from its fight's
    start. The second match, the order turned (Bunta first), is left in
    the middle of its fight by Dai, whose fighter the CPU plays on to the
    match's end (docs/NETPLAY.md, section 19). Every hash of a
    match the same on its four players and its spectator."""
    lobby = case.server(*ROOM_SERVER)
    with PLAN_LOCK:
        plan = port_net.write_room_plan(HEADLESS, case.folder, four=True)
    base = [HEADLESS, '--image', playtest.RETAIL_CUE, '--no-cards', '--lobby', lobby, '--net-plan-pads',
            '--net-seed', str(ROOM_SEED), '--plan', plan, '--trace'] + TROUBLE

    def member(label, *more):
        return case.start(label, base + ['--out', os.path.join(case.folder, label), '--player', label.capitalize()] +
                          list(more))

    procs = [member('ayame', '--room-open', 'Fours', '--room-rule', 'fixed', '--room-size', '4', '--room-matches',
                    '2')]
    code = case.wait_for('ayame', r"room's code is (\w+) \(a room of several", 30)
    if code is None:
        case.fail('the room of several was given no code')
        return
    code = code.group(1)
    # the rotation's order is the order they come in
    for label, more in (('bunta', ()), ('chie', ()), ('dai', ('--leave-at', '2:%d' % FOUR_LEAVE_FRAME))):
        procs.append(member(label, '--room-enter', code, '--room-matches', '2', *more))
        if case.wait_for(label, r'room: in room', 60) is None:
            case.fail('%s never came into the room' % label.capitalize())
    procs.append(member('sora', '--room-enter', code, '--room-watcher', '--room-matches', '2'))
    labels = ['ayame', 'bunta', 'chie', 'dai', 'sora']
    codes = wait_all(procs, SECONDS * 2)
    segments = {}
    for label, code_ in zip(labels, codes):
        err = case.err(label)
        if code_ != 0:
            case.fail('%s ended with %s (%s)' % (label, code_, os.path.join(case.folder, label + '.err')))
        if 'DESYNC' in err:
            case.fail('%s found a desync' % label)
        segments[label] = room_segments(err)
    for number, players in ((1, ('ayame', 'bunta', 'chie', 'dai')), (2, ('bunta', 'chie', 'dai', 'ayame'))):
        who = {label: seg[number] for label, seg in segments.items() if number in seg}
        playing = sorted(label for label, part in who.items() if part['role'] != 'watch')
        if playing != sorted(players) or 'sora' not in who:
            case.fail('match %d was not %s\'s, watched by Sora: %r' % (
                number, ', '.join(players), {label: part['role'] for label, part in who.items()}))
            continue
        roles = {label: who[label]['role'] for label in players}
        if [roles[label] for label in players] != ['player 1', 'player 2', 'player 3', 'player 4']:
            case.fail('match %d: the places were not the rotation\'s: %r' % (number, roles))
        reference = who[players[0]]['hashes']
        cpu_from = re.search(r'the CPU plays his fighter from frame (\d+)', '\n'.join(who[players[0]]['text']))
        for label in list(players[1:]) + ['sora']:
            mine = who[label]['hashes']
            if cpu_from and label == 'dai':
                # the leaver's own hashes after the CPU took his pad are
                # his inputs', not the match's (NETPLAY.md, section 19)
                mine = {f: h for f, h in mine.items() if f < int(cpu_from.group(1))}
            shared = sorted(set(mine) & set(reference))
            differ = [f for f in shared if mine[f] != reference[f]]
            if differ:
                case.fail('match %d: %s\'s hash of frame %d is not %s\'s' % (number, label, differ[0], players[0]))
            if len(shared) < (5 if label == 'sora' else 30):
                case.fail('match %d: %s has %d hashes in common with %s' % (number, label, len(shared), players[0]))
        held = re.search(r'(\d+) hashes the same as its players\'', '\n'.join(who['sora']['text']))
        case.said.append('match %d (%s and %s vs %s and %s): %d hashes on its host, the same on all four; Sora '
                         'held %s of them' % ((number,) + tuple(p.capitalize() for p in
                                                                (players[0], players[2], players[1], players[3])) +
                                             (len(reference), held.group(1) if held else 'none')))
        if not held or int(held.group(1)) < 5:
            case.fail('match %d: Sora held too few of its hashes to the players\'' % number)
    first = '\n'.join(sum((segments[label].get(1, {}).get('text', []) for label in labels[:4]), []))
    if 'room: match 1: the' not in first or \
            any("the host's teams are two on two" not in case.err(label) for label in labels[:4]):
        case.fail('match 1 was not a fight of two on two decided for a team')
    second = '\n'.join(segments['dai'].get(2, {}).get('text', []))
    stayed = {label: '\n'.join(segments[label].get(2, {}).get('text', [])) for label in ('ayame', 'bunta', 'chie',
                                                                                         'sora')}
    taken = [re.search(r'netplay: player 3 has left the (?:session|match): the CPU plays his fighter from frame '
                       r'(\d+)', text) for text in stayed.values()]
    over = re.search(r'room: match 2 over: (its \w+ player won|nobody won)', stayed['bunta'])
    if 'this player left the session at frame %d' % FOUR_LEAVE_FRAME not in second or \
            'its end is the other players\' to tell' not in second or not all(taken) or \
            len(set(t.group(1) for t in taken)) != 1 or not over or \
            any('netplay: the match was decided at frame' not in stayed[label] for label in ('ayame', 'bunta',
                                                                                             'chie')):
        case.fail('Dai\'s leave in the middle of match 2 was not as it must be (the CPU playing his fighter on)')
    else:
        case.said.append('Dai left match 2 at frame %d, in its fight: the CPU played his fighter from frame %s on, '
                         'on the three who stayed and Sora; the match played to its end (%s, the room told by '
                         'those who stayed), all back to their own games and the room'
                         % (FOUR_LEAVE_FRAME, taken[0].group(1), over.group(1)))

HEADLESS_CASES = [('punch', case_punch), ('relay', case_relay), ('mapped', case_mapped), ('v6', case_v6),
                  ('direct6', case_direct6), ('punchto', case_punchto), ('list', case_list),
                  ('refused', case_refused),
                  # (a name of each system's: the gate runs the Linux and the
                  # Windows halves at once, and this one is long enough for
                  # the two to meet in one folder)
                  ('rooms-win' if IS_WINDOWS else 'rooms', case_rooms)]
# Run after the cases above, not beside them (they all run at once, and
# three programs more starve those that count their round trips), and in
# the Linux half alone: a room's Crew match.
LATER_CASES = [('rooms-crew', case_rooms_crew), ('rooms-duo', case_rooms_duo),
               ('ranked', case_ranked)]
# ... and in the Windows half, its own ranked case, after the rest
WINDOWS_LATER_CASES = [('ranked-win', case_ranked)]
# Four players, after those (four programs each): through the lobby, a
# punched hole and the relay; then a room of several's matches of four.
FOUR_CASES = [('four-punch-win' if IS_WINDOWS else 'four-punch', case_four_punch),
              ('four-relay-win' if IS_WINDOWS else 'four-relay', case_four_relay)]
FOUR_LATER_CASES = [('rooms-four-win' if IS_WINDOWS else 'rooms-four', case_rooms_four)]


# ---- the window's cases (Windows) ----

def open_pid(pid):
    """A handle of a process this program did not start (Windows), taken
    while it runs, so that its end can be waited for and its code read
    however soon it ends: None if it has gone already."""
    return ctypes.windll.kernel32.OpenProcess(0x00100000 | 0x1000, False, pid) or None  # SYNCHRONIZE | QUERY


def wait_pid(handle, seconds):
    """The process of `handle` waited for: its exit code, or None."""
    kernel = ctypes.windll.kernel32
    if not handle:
        return None
    try:
        if kernel.WaitForSingleObject(handle, int(seconds * 1000)) != 0:
            kernel.TerminateProcess(handle, 9)
            return None
        code = ctypes.c_ulong()
        kernel.GetExitCodeProcess(handle, ctypes.byref(code))
        return code.value
    finally:
        kernel.CloseHandle(handle)


def suspend(pid, on):
    """NtSuspendProcess / NtResumeProcess: a PC that stops a program."""
    kernel = ctypes.windll.kernel32
    ntdll = ctypes.windll.ntdll
    handle = kernel.OpenProcess(0x0800, False, pid)  # PROCESS_SUSPEND_RESUME
    if handle:
        (ntdll.NtSuspendProcess if on else ntdll.NtResumeProcess)(handle)
        kernel.CloseHandle(handle)


def case_win_headless(case):
    args = argparse.Namespace(out=case.folder, latency=100, loss=5, delay=2)
    case.problems += port_net.check_session('windows-headless', os.path.join(REPO, HEADLESS), args,
                                            (case.port(1), case.port(2)), False)


def case_win_window(case):
    args = argparse.Namespace(out=case.folder, latency=60, loss=5, delay=2)
    case.problems += port_net.check_session('windows-window', os.path.join(REPO, WINDOW), args,
                                            (case.port(1), case.port(2)), True)


def menu_pair(case, settings, keys, first_waits=None):
    """Two window programs started as a player starts them, each opening the
    menu and beginning the session from the Play online page in its own
    process (docs/NETPLAY.md, section 15): both waited for, neither started
    again, and their hashes held to each other."""
    procs = []
    for side in (0, 1):
        cfg = os.path.join(case.folder, 'side%d.cfg' % side)
        with open(cfg, 'w', encoding='utf-8', newline='\n') as f:
            f.write('fullscreen = off\nnet_listen = local\n' + settings[side])
        label = 'side%d' % side
        if side == 1 and first_waits is not None:
            first_waits()
        procs.append(case.start(label, [os.path.join(REPO, WINDOW), '--image', playtest.RETAIL_CUE, '--config', cfg,
                                        '--windowed', '--frames', '1500', '--log',
                                        os.path.join(case.folder, label + '.log'), '--menu-at', '120',
                                        '--menu-keys', keys[side](), '--online-keys', 'until:lobby accept', '--card1',
                                        os.path.join(case.folder, 'card1.mcd'), '--card2',
                                        os.path.join(case.folder, 'card2.mcd')], DUMMY))
    hashes = []
    codes = wait_all(procs, SECONDS)
    for side, code in enumerate(codes):
        label = 'side%d' % side
        err = case.err(label)
        if code != 0:
            case.fail('%s ended with %s' % (label, code))
        if 'session 1 begins in this program' not in err or 'started again as process' in err:
            case.fail('%s did not begin the session in its own process' % label)
        hashes.append(port_net.hashes_of(err))
    common = sorted(set(hashes[0]) & set(hashes[1]))
    if not common or any(hashes[0][f] != hashes[1][f] for f in common):
        case.fail('the two programs\' hashes are not the same (%d in common)' % len(common))
    elif not case.problems:
        case.said.append('both began the session in their own processes (%d, %d), no start again, and played on: '
                         '%d state hashes the same' % (procs[0].pid, procs[1].pid, len(common)))


def case_win_menu(case):
    port = case.port(1)
    # up from the first row comes round to Quit, Back to the game, Updates,
    # then Play online; the page's rows: by address the host's are You, its port, Start; the
    # guest's You, the host's address, Start
    keys = [lambda: 'up up up up accept down down accept',
            lambda: 'up up up up accept down accept type:127.0.0.1:%d down accept' % port]
    settings = ['net_role = host\nnet_port = %d\nnet_delay = 2\n' % port, 'net_role = join\n']
    menu_pair(case, settings, keys)


def case_win_rooms(case):
    lobby = case.server()
    port = case.port(1)
    # Host a room: You, Lobby, Room's name, Password, Your name, Start;
    # Browse rooms: You, Lobby, Your name, Look for rooms, the rooms
    keys = [lambda: 'up up up up accept down down down accept type:%s down down accept' % PASSWORD,
            lambda: 'up up up up accept down down down accept down accept type:%s' % PASSWORD]
    settings = ['net_role = room\nnet_lobby = %s\nnet_room = Menu room\nnet_player = Hostess\nnet_port = %d\n'
                % (lobby, port), 'net_role = rooms\nnet_lobby = %s\nnet_player = Browser\n' % lobby]

    def host_open():
        if case.wait_for('side0', r"room's code is (\w+)", 60) is None:
            case.fail('the host\'s room was not opened from the menu')
    menu_pair(case, settings, keys, host_open)
    if 'a session as the guest by way of the lobby' not in case.err('side1'):
        case.fail('the guest did not join by way of the lobby\'s list')


def case_win_portmap(case):
    router_log = open(os.path.join(case.folder, 'router.log'), 'wb')
    router = subprocess.Popen([sys.executable, '-u', 'tools/relay/fake_router.py', '--external', '127.0.0.2', '--no-pcp',
                               '--no-pmp', '--seconds', str(SECONDS)],
                              cwd=REPO, stdout=router_log, stderr=subprocess.STDOUT, stdin=subprocess.PIPE)
    case.procs.append((router, router_log, None))
    ready = None
    until = time.time() + 10
    while ready is None and time.time() < until:
        ready = re.search(r'ready ssdp=(\d+) http=(\d+) gateway=(\d+)', read(os.path.join(case.folder, 'router.log')))
        time.sleep(0.05)
    if ready is None:
        case.fail('the fake router did not start')
        return
    host_more = ['--portmap', 'on', '--portmap-ssdp', '127.0.0.1:%s' % ready.group(1), '--portmap-gateway',
                 '127.0.0.1:%s' % ready.group(3)]
    lobby_session(case, WINDOW, host_more, ['--net-paths', 'mapped,relay'], r"the way is the host's mapped port",
                  window=True, both_paths=False)
    # the window's config turns mapping off: --portmap on must win
    log = read(os.path.join(case.folder, 'router.log'))
    if 'AddPortMapping' not in log:
        case.fail('the host did not ask the router for its port')
    elif 'DeletePortMapping' not in log:
        case.fail('the host did not give the port back as it ended')
    else:
        case.said.append('the router: ' + '; '.join(l.split('fake_router: ', 1)[1] for l in log.splitlines()
                                                    if 'PortMapping' in l))


def window_pair_refused(case, label, guest_more, words, host_words=r'A player was turned away'):
    port = case.port(1) + len(case.said) * 2 + len(case.problems) * 2
    cfg = os.path.join(case.folder, label + '.cfg')
    with open(cfg, 'w', encoding='utf-8', newline='\n') as f:
        f.write('fullscreen = off\nnet_portmap = off\n')
    common = [os.path.join(REPO, WINDOW), '--image', playtest.RETAIL_CUE, '--config', cfg, '--windowed', '--no-cards']
    host = case.start(label + '-host', common + ['--log', os.path.join(case.folder, label + '-host.log'), '--host',
                                                 '127.0.0.1:%d' % port], DUMMY) if host_words else None
    time.sleep(0.5)
    case.start(label + '-guest', common + ['--log', os.path.join(case.folder, label + '-guest.log'), '--join',
                                           '127.0.0.1:%d' % port, '--net-bind', '127.0.0.1:0'] + guest_more, DUMMY)
    shown = case.wait_for(label + '-guest', r'netplay: could not play online: (.*)', 60)
    if shown is None or not re.search(words, shown.group(1)):
        case.fail('%s: the guest\'s window did not say %r (%s)' % (label, words, shown.group(1) if shown else 'nothing'))
    else:
        case.said.append('%s, the guest\'s screen: %s' % (label, shown.group(1)))
    if host_words:
        seen = case.wait_for(label + '-host', r'netplay: (%s[^\n]*)' % host_words, 10)
        if seen is None:
            case.fail('%s: the host\'s window did not say %r' % (label, host_words))
        else:
            case.said.append('%s, the host\'s screen: %s' % (label, seen.group(1)))
        host.kill()


def case_win_refused(case):
    runs = [('build', ['--session-differ', 'build'], r'another build of ReBushido'),
            ('disc', ['--session-differ', 'disc'], r'disc image is not the same as yours'),
            ('mods', ['--session-differ', 'mods'], r'gameplay mods'),
            ('protocol', ['--session-differ', 'protocol'], r'another version')]
    for label, more, words in runs:
        window_pair_refused(case, label, more, words)
    window_pair_refused(case, 'nobody', [], r'Nobody answered at 127\.0\.0\.1', host_words=None)


def case_win_quiet(case):
    """The host gone quiet once the guest is in: the guest's window says so;
    then the host, woken, says the guest went."""
    cfg = os.path.join(case.folder, 'quiet.cfg')
    with open(cfg, 'w', encoding='utf-8', newline='\n') as f:
        f.write('fullscreen = off\nnet_portmap = off\n')
    port = case.port(1)
    common = [os.path.join(REPO, WINDOW), '--image', playtest.RETAIL_CUE, '--config', cfg, '--windowed']
    host = case.start('host', common + ['--log', os.path.join(case.folder, 'host.log'), '--host', '127.0.0.1:%d' % port,
                                        '--net-latency', '1500', '--card1', os.path.join(case.folder, 'c1.mcd'),
                                        '--card2', os.path.join(case.folder, 'c2.mcd')], DUMMY)
    time.sleep(0.5)
    case.start('guest', common + ['--log', os.path.join(case.folder, 'guest.log'), '--join', '127.0.0.1:%d' % port,
                                  '--net-bind', '127.0.0.1:0', '--no-cards'], DUMMY)
    if case.wait_for('guest', r'the host has let this program in', 40) is None:
        case.fail('the guest was never let in')
        return
    suspend(host.pid, True)
    shown = case.wait_for('guest', r'netplay: could not play online: (.*)', 40)
    suspend(host.pid, False)
    if shown is None or 'stopped answering' not in shown.group(1):
        case.fail('the guest\'s window did not say the host stopped answering (%s)' % (shown.group(1) if shown else
                                                                                       'nothing'))
    else:
        case.said.append('the guest\'s screen: %s' % shown.group(1))
    if case.wait_for('host', r'(The other player stopped answering|turned yours away)', 30) is None:
        case.fail('the host\'s window did not say the guest went')
    else:
        case.said.append('the host, woken: the other player stopped answering; it waits for another')


def case_win_lobby_refused(case):
    """In the window: a wrong password and an incompatible build at the
    lobby, each said on the guest's screen."""
    lobby = case.server()
    cfg = os.path.join(case.folder, 'w.cfg')
    with open(cfg, 'w', encoding='utf-8', newline='\n') as f:
        f.write('fullscreen = off\nnet_portmap = off\n')
    common = [os.path.join(REPO, WINDOW), '--image', playtest.RETAIL_CUE, '--config', cfg, '--windowed', '--no-cards',
              '--lobby', lobby]
    case.start('host', common + ['--log', os.path.join(case.folder, 'host.log'), '--host',
                                 '127.0.0.2:%d' % case.port(1), '--room', 'Shut', '--room-password', PASSWORD], DUMMY)
    code = case.wait_for('host', r"room's code is (\w+)", 30)
    if code is None:
        case.fail('no room code')
        return
    for label, more, words in (('password', ['--room-password', 'wrong1'], r'The password is not'),
                               ('build', ['--room-password', PASSWORD, '--session-differ', 'build'],
                                r'not compatible with yours.*the build')):
        case.start(label, common + ['--log', os.path.join(case.folder, label + '.log'), '--join', code.group(1),
                                    '--net-bind', '127.0.0.3:0'] + more, DUMMY)
        shown = case.wait_for(label, r'netplay: could not play online: (.*)', 40)
        if shown is None or not re.search(words, shown.group(1)):
            case.fail('%s: the guest\'s window did not say %r (%s)' % (label, words,
                                                                     shown.group(1) if shown else 'nothing'))
        else:
            case.said.append('%s, the guest\'s screen: %s' % (label, shown.group(1)))


# ---- the online pages (online_pages.c): sessions begun in the running
# window, with no start again (docs/NETPLAY.md, section 15)

def pages_window(case, label, settings, keys, plan_side, more=()):
    """A window program started as a player starts it, its online pages
    opened at blank 120 (what Online Play on the top menu opens) and told
    `keys`; its pictures in the case's folder `shots`: its process."""
    cfg = os.path.join(case.folder, label + '.cfg')
    os.makedirs(os.path.join(case.folder, 'shots', label), exist_ok=True)
    with open(cfg, 'w', encoding='utf-8', newline='\n') as f:
        f.write('fullscreen = off\nnet_listen = local\nnet_portmap = off\n' + settings)
    cmd = [WINDOW, '--image', playtest.RETAIL_CUE, '--config', cfg, '--windowed', '--log',
           os.path.join(case.folder, label + '.log'), '--online-at', '120', '--online-keys', keys,
           '--menu-shots', os.path.join(case.folder, 'shots', label), '--card1',
           os.path.join(case.folder, label + '-card1.mcd'), '--card2', os.path.join(case.folder, label + '-card2.mcd'), '--quick-loading', 'on'] + list(more)
    if plan_side is not None:
        with PLAN_LOCK:
            plan = port_net.write_leave_plan(WINDOW, plan_side, case.folder)
        cmd += ['--plan', plan]
    return case.start(label, cmd, DUMMY)


def session_hashes(text, n):
    """The hashes of session `n` (1 on) of what a program printed."""
    parts = re.split(r'online: session \d+ begins in this program', text)
    return port_net.hashes_of(parts[n]) if len(parts) > n else {}


def case_pages(case):
    """Two sessions in one host window, from its online pages: the host
    opens a locked room (Host a room, a password typed); a guest finds it
    in Browse rooms, types its password and joins; both say ready in the
    lobby before the fight; the session plays from power on to the versus
    select screen, where the guest's Triangle takes both back to the top
    menu, which ends it on both. The host is back at its own game with the
    pages saying so; then it takes Quick Match, which opens a room of its
    own, and a second guest's Quick Match finds it: a second session, in
    the same process. Each session's hashes are the same on both sides,
    and the host never starts again."""
    lobby = case.server()
    host_keys = ('shot down accept shot down accept type:%s shot done down accept '
                 'until:waiting wait:20 shot until:lobby wait:10 shot accept '
                 'wait:5 shot accept up accept until:lobby accept' % PASSWORD)
    guest_keys = 'down down accept wait:10 shot accept type:%s shot done until:lobby wait:10 shot accept' % PASSWORD
    host = pages_window(case, 'host', 'net_lobby = %s\nnet_player = Hostess\nnet_room = Pages room\nnet_delay = 2\n'
                        'net_port = %d\n' % (lobby, case.port(1)), host_keys, 0)
    if case.wait_for('host', r"room's code is (\w+)", 90) is None:
        case.fail('the host\'s room was not opened from the pages')
        return
    guest = pages_window(case, 'guest1', 'net_lobby = %s\nnet_player = Browser\n' % lobby, guest_keys, 1)
    for label in ('host', 'guest1'):
        if case.wait_for(label, r'session 1 has ended', 240) is None:
            case.fail('%s: the first session did not end (%s.err)' % (label, label))
            return
    guest.kill()
    first = [session_hashes(case.err(label), 1) for label in ('host', 'guest1')]
    if len(re.findall(r"room's code is (\w+)", case.err('host'))) < 1 or \
            case.wait_for('host', r"Quick Match found no room to join", 60) is None:
        case.fail('the host did not take Quick Match after the first session')
        return
    until = time.time() + 60
    while time.time() < until and len(re.findall(r"room's code is (\w+)", case.err('host'))) < 2:
        time.sleep(0.1)
    guest2 = pages_window(case, 'guest2', 'net_lobby = %s\nnet_player = Quick\n' % lobby, 'accept until:lobby accept',
                          1)
    for label in ('host', 'guest2'):
        if case.wait_for(label, r'session \d+ has ended', 240) is None or \
                (label == 'host' and case.wait_for(label, r'session 2 has ended', 240) is None):
            case.fail('%s: the second session did not end (%s.err)' % (label, label))
            return
    second = [session_hashes(case.err('host'), 2), session_hashes(case.err('guest2'), 1)]
    for n, pair in enumerate((first, second)):
        common = sorted(set(pair[0]) & set(pair[1]))
        if len(common) < 10 or any(pair[0][f] != pair[1][f] for f in common):
            case.fail('session %d: the two windows\' hashes are not the same (%d in common)' % (n + 1, len(common)))
    host_err = case.err('host')
    if 'starts again' in host_err or 'started again as process' in host_err:
        case.fail('the host started again')
    if host.poll() is not None:
        case.fail('the host\'s process ended (exit %s): it must be the one that played both' % host.returncode)
    if 'Quick Match joins room' not in read(os.path.join(case.folder, 'guest2.log')):
        case.fail('the second guest\'s Quick Match did not join the host\'s room')
    if not case.problems:
        case.said.append('two sessions in one host process (%d), begun from its online pages: a locked room browsed '
                         'and joined with its password, then Quick Match on both; %d and %d state hashes the same; '
                         'each ended by the way back to the top menu' % (
                             host.pid, len(set(first[0]) & set(first[1])), len(set(second[0]) & set(second[1]))))
    guest2.kill()
    host.kill()
    save_pictures(case, PICTURES)


def case_pages_four(case):
    """A room of four hosted from a window's online pages (Host a room,
    Room for: four players, a PC each), three headless guests joining it
    by its code (window programs too, of the command line: a setup code
    is its build's): the lobby before the fight with all four, the host's
    Ready, the session from power on to the versus select screen, where
    the first guest's Triangle takes all back to the top menu, which ends
    the session; the host window back at its own game, never started
    again, and every hash the same on all four."""
    lobby = case.server()
    host_keys = ('shot down accept down down down accept shot up accept '
                 'until:waiting wait:20 shot until:lobby wait:20 shot accept')
    host = pages_window(case, 'host', 'net_lobby = %s\nnet_player = Hostess\nnet_room = Fours\nnet_delay = 2\n'
                        'net_port = %d\n' % (lobby, case.port(1)), host_keys, 0)
    code = case.wait_for('host', r"room's code is (\w+)", 90)
    if code is None:
        case.fail('the host\'s room of four was not opened from the pages')
        return
    guests = []
    # (each guest's Triangle at the select screen: the one that came second,
    # player 2, takes all back; the third and fourth pads' do nothing there)
    with PLAN_LOCK:
        plans = [port_net.write_leave_plan(WINDOW, 1, case.folder)]
    cfg = os.path.join(case.folder, 'guests.cfg')
    with open(cfg, 'w', encoding='utf-8', newline='\n') as f:
        f.write('fullscreen = off\nnet_portmap = off\n')
    for n, name in enumerate(('Bunta', 'Chie', 'Dai')):
        label = name.lower()
        guests.append(case.start(label, [WINDOW, '--image', playtest.RETAIL_CUE, '--config', cfg, '--windowed',
                                         '--quick-loading', 'on', '--log', os.path.join(case.folder, label + '.log'),
                                         '--lobby', lobby, '--join', code.group(1), '--player', name, '--plan',
                                         plans[0]], DUMMY))
    if case.wait_for('host', r'session 1 has ended', 300) is None:
        case.fail('the session of four did not end on the host (host.err)')
        return
    # (a window of a session of the command line plays on alone after it)
    for label in ('bunta', 'chie', 'dai'):
        if case.wait_for(label, r'the other program ended after frame', 60) is None:
            case.fail('%s was not told the session had ended' % label)
    for guest in guests:
        guest.kill()
    codes = [0, 0, 0]
    hashes = [session_hashes(case.err('host'), 1)] + [port_net.hashes_of(case.err(n)) for n in ('bunta', 'chie',
                                                                                                'dai')]
    common = sorted(set.intersection(*(set(h) for h in hashes)))
    if len(common) < 10 or any(h[f] != hashes[0][f] for h in hashes for f in common):
        case.fail('the four programs\' hashes are not the same (%d in common)' % len(common))
    for label, code_ in zip(('bunta', 'chie', 'dai'), codes):
        if code_ != 0:
            case.fail('%s ended with %s' % (label, code_))
        if '4 players on 4 PCs' not in case.err(label):
            case.fail('%s did not play a session of four' % label)
    host_err = case.err('host')
    if 'starts again' in host_err or 'started again as process' in host_err:
        case.fail('the host started again')
    if '4 players on 4 PCs' not in host_err or 'all 4 players are here and ready: go' not in host_err:
        case.fail('the host did not meet its three guests and say go')
    if host.poll() is not None:
        case.fail('the host\'s process ended (exit %s)' % host.returncode)
    if not case.problems:
        case.said.append('a room of four from the host window\'s pages, three guests by its code; the session '
                         'ended at the select screen on all, %d state hashes the same on all four' % len(common))
    host.kill()
    save_pictures(case, PICTURES)


def case_pages_refused(case):
    """A guest of another build (--session-differ) joins a room by its code
    from the pages: the lobby turns it away, the pages say why in plain
    words and stay up, and the program goes on."""
    lobby = case.server()
    host = case.start('host', [HEADLESS, '--image', playtest.RETAIL_CUE, '--no-cards', '--lobby', lobby, '--host',
                               '127.0.0.2:%d' % case.port(1), '--room', 'Shut'])
    code = case.wait_for('host', r"room's code is (\w+)", 30)
    if code is None:
        case.fail('no room code')
        return
    keys = ('down down down down accept shot back up accept type:%s shot done wait:100 shot accept wait:5 shot'
            % code.group(1))
    guest = pages_window(case, 'guest', 'net_lobby = %s\nnet_player = Odd\n' % lobby, keys, None,
                         ['--session-differ', 'build', '--frames', '100000'])
    shown = case.wait_for('guest', r'netplay: could not play online: (.*)', 60)
    if shown is None or 'not compatible' not in shown.group(1):
        case.fail('the guest\'s pages did not say it was turned away (%s)' % (shown.group(1) if shown else 'nothing'))
    elif case.wait_for('guest', r'online pages: open', 30) is None or \
            case.wait_for('guest', r'online-002\.png', 30) is None or guest.poll() is not None:
        case.fail('the guest did not go back to its pages, in the same process')
    else:
        case.said.append('turned away, on the pages: %s' % shown.group(1))
    guest.kill()
    host.kill()
    save_pictures(case, PICTURES)


# The room page's host leaves the first match in its fight: two Escapes,
# at these blanks of the session (its fight is from near blank 2,900 to
# 4,000).
ROOM_ESCAPES = '3200,3300'


def case_win_room(case):
    """A room of several from the online pages of a window: its host opens
    it (Host a room, Room for: several, Open), two windows of the command
    line come in, and the rotation's first match is the host's; the host
    leaves it in the middle of its fight with Escape twice, and is back on
    its pages, in the room; the second match, the two others', it watches
    from its pages. Each match's hashes the same on the three windows."""
    lobby = case.server(*ROOM_SERVER)
    with PLAN_LOCK:
        plan = port_net.write_room_plan(WINDOW, case.folder)
    os.makedirs(os.path.join(case.folder, 'shots', 'hostess'), exist_ok=True)
    common = ['--image', playtest.RETAIL_CUE, '--windowed', '--quick-loading', 'on', '--plan', plan,
              '--net-plan-pads', '--net-seed', str(ROOM_SEED), '--no-cards']

    def window(label, settings, more):
        cfg = os.path.join(case.folder, label + '.cfg')
        with open(cfg, 'w', encoding='utf-8', newline='\n') as f:
            f.write('fullscreen = off\nnet_listen = local\nnet_portmap = off\nnet_lobby = %s\nnet_player = %s\n%s'
                    % (lobby, label.capitalize(), settings))
        return case.start(label, [WINDOW, '--config', cfg, '--log', os.path.join(case.folder, label + '.log'),
                                  '--out', os.path.join(case.folder, label)] + common + more, DUMMY)

    # Room for: two players, four on four PCs, two on each of two, several
    keys = ('down accept down down down accept accept accept shot up accept until:room wait:10 shot '
            'until:message wait:10 shot accept until:message wait:10 shot accept')
    host = window('hostess', 'net_room = Group room\nnet_delay = 2\nnet_port = %d\n' % case.port(1),
                  ['--online-at', '120', '--online-keys', keys, '--escape-at', ROOM_ESCAPES,
                   '--menu-shots', os.path.join(case.folder, 'shots', 'hostess')])
    code = case.wait_for('hostess', r"room's code is (\w+) \(a room of several", 90)
    if code is None:
        case.fail('the room of several was not opened from the pages')
        return
    members = [window(label, '', ['--lobby', lobby, '--room-enter', code.group(1), '--room-matches', '2',
                                  '--player', label.capitalize()])
               for label in ('bunta', 'chie')]
    codes = wait_all(members, SECONDS)
    watched = case.wait_for('hostess', r'room: match 2 watched', 120)
    host.kill()
    for label, code_ in zip(('bunta', 'chie'), codes):
        if code_ != 0:
            case.fail('%s ended with %s' % (label, code_))
    segments = {label: room_segments(case.err(label) + read(os.path.join(case.folder, label + '.log')))
                for label in ('hostess', 'bunta', 'chie')}
    for number in (1, 2):
        who = {label: seg[number] for label, seg in segments.items() if number in seg}
        if len(who) != 3:
            case.fail('match %d was run by %s, not all three' % (number, ', '.join(who) or 'nobody'))
            continue
        players = [label for label, part in who.items() if part['role'] != 'watch']
        reference = who[players[0]]['hashes'] if players else {}
        for label, part in who.items():
            common_frames = sorted(set(part['hashes']) & set(reference))
            if any(part['hashes'][f] != reference[f] for f in common_frames) or \
                    (label != players[0] and len(common_frames) < 5):
                case.fail('match %d: %s\'s hashes are not its players\' (%d in common)' % (
                    number, label, len(common_frames)))
        case.said.append('match %d (%s vs %s): played by %s, watched by %s; the hashes the same' % (
            number, who[players[0]]['pair'][0], who[players[0]]['pair'][1], ' and '.join(players),
            ', '.join(l for l in who if l not in players)))
    hostess = case.err('hostess') + read(os.path.join(case.folder, 'hostess.log'))
    left = re.search(r'this player left the session at frame (\d+)', hostess)
    if not left or int(left.group(1)) < 1387 or 'You left the fight' not in hostess or \
            'room: match 1 over: its second player won' not in hostess:
        case.fail('the host did not leave match 1 in its fight, back to its pages (%s)' %
                  (left.group(0) if left else 'no leave'))
    else:
        case.said.append('the host left match 1 at frame %s, in its fight, with Escape twice; back on its pages, '
                         'in the room, it watched match 2' % left.group(1))
    if watched is None:
        case.fail('the host did not watch match 2 from its pages')
    if 'starts again' in hostess or 'started again as process' in hostess:
        case.fail('the host started again')
    save_pictures(case, PICTURES)


def case_win_ranked(case):
    """Ranked play from a window's pages (Online Play, Ranked, Find a ranked
    match) against a window of the command line (--ranked): the lobby pairs
    them, the window's pages begin the match in the running program, both
    report, the match is rated; the window's Ranked page shows the result
    and its ladder page the two (pictures in its shots)."""
    state = os.path.join(case.folder, 'ladder')
    lobby = case.server('--state', state, *RANKED_SERVER)
    with PLAN_LOCK:
        plan = port_net.write_room_plan(WINDOW, case.folder)
    shots = os.path.join(case.folder, 'shots', 'kaede')
    os.makedirs(shots, exist_ok=True)
    cfg = os.path.join(case.folder, 'kaede.cfg')
    with open(cfg, 'w', encoding='utf-8', newline='\n') as f:
        f.write('fullscreen = off\nnet_listen = local\nnet_portmap = off\nnet_lobby = %s\nnet_player = Kaede\n'
                'fps = off\n' % lobby)
    # Online Play's rows: Quick Match, Host, Browse, Code, Address, Ranked
    keys = ('down down down down down accept wait:10 shot accept until:queued wait:10 shot '
            'until:ranked wait:30 shot accept wait:10 shot down accept until:list wait:10 shot')
    window = case.start('kaede', [WINDOW, '--config', cfg, '--log', os.path.join(case.folder, 'kaede.log'),
                                  '--out', os.path.join(case.folder, 'kaede'), '--image', playtest.RETAIL_CUE,
                                  '--windowed', '--quick-loading', 'on', '--plan', plan, '--net-plan-pads',
                                  '--net-seed', str(RANKED_SEED), '--no-cards', '--online-at', '120',
                                  '--online-keys', keys, '--menu-shots', shots,
                                  '--identity', os.path.join(case.folder, 'kaede.key')], DUMMY)
    if case.wait_for('kaede', r'ranked: \S+ goes into the queue', 90) is None:
        case.fail('the window never queued from its pages')
        window.kill()
        return
    # (a window of the command line: its setup code is the other window's,
    # where the headless program's build is another)
    goro_cfg = os.path.join(case.folder, 'goro.cfg')
    with open(goro_cfg, 'w', encoding='utf-8', newline='\n') as f:
        f.write('fullscreen = off\nnet_listen = local\nnet_portmap = off\n')
    other = case.start('goro', [WINDOW, '--config', goro_cfg, '--log', os.path.join(case.folder, 'goro.log'),
                                '--out', os.path.join(case.folder, 'goro'), '--image', playtest.RETAIL_CUE,
                                '--windowed', '--quick-loading', 'on', '--plan', plan, '--net-plan-pads',
                                '--net-seed', str(RANKED_SEED), '--no-cards', '--lobby', lobby, '--ranked',
                                '--ranked-matches', '1', '--player', 'Goro',
                                '--identity', os.path.join(case.folder, 'goro.key')], DUMMY)
    code = wait_all([other], SECONDS)[0]
    shown = case.wait_for('kaede', r'a picture of the window, .*online-004\.png', 120)
    time.sleep(2.0)
    window.kill()
    kaede = ranked_logs(case.err('kaede') + read(os.path.join(case.folder, 'kaede.log')))
    goro = ranked_logs(case.err('goro') + read(os.path.join(case.folder, 'goro.log')))
    if code != 0:
        case.fail('the command line\'s window ended with %s' % code)
    if kaede.get('match') is None or kaede.get('match') != goro.get('match'):
        case.fail('the two windows did not play one match: %r, %r' %
                  (kaede.get('match'), goro.get('match')))
    elif kaede.get('state') != 'rated' or goro.get('state') != 'rated' or kaede.get('digest') != goro.get('digest'):
        case.fail('the match is not rated alike on both: %r, %r' % (kaede, goro))
    else:
        case.said.append('match %d from the window\'s pages: Kaede %s, 1500 to %d; Goro %s, 1500 to %d; the digests '
                         'alike' % (kaede['match'], kaede['won'], kaede['after'], goro['won'], goro['after']))
    if shown is None:
        case.fail('the window\'s pages did not show the ladder after the match')
    if 'starts again' in case.err('kaede') + read(os.path.join(case.folder, 'kaede.log')):
        case.fail('the window started again')
    save_pictures(case, PICTURES)


def save_pictures(case, into):
    """The case's pictures of the pages, copied into `into`."""
    shots = os.path.join(case.folder, 'shots')
    if not into or not os.path.isdir(shots):
        return
    for root, _, names in os.walk(shots):
        for name in names:
            if name.endswith('.png'):
                who = os.path.basename(root)
                os.makedirs(into, exist_ok=True)
                shutil.copy(os.path.join(root, name), os.path.join(into, '%s-%s-%s' % (case.name, who, name)))


WINDOW_CASES = [('win-headless', case_win_headless), ('win-window', case_win_window), ('win-menu', case_win_menu),
                ('win-rooms', case_win_rooms), ('win-portmap', case_win_portmap), ('win-refused', case_win_refused),
                ('win-quiet', case_win_quiet), ('win-lobby-refused', case_win_lobby_refused),
                ('win-pages', case_pages), ('win-pages-refused', case_pages_refused), ('win-room', case_win_room),
                ('win-pages-four', case_pages_four)]
# (and the window's ranked page with a match, after the rest: WINDOWS_LATER_CASES)
WINDOWS_LATER_CASES.append(('win-ranked', case_win_ranked))
# the online pages' cases, which the window's half of tools/port_net.py runs on Linux too
PAGES_CASES = [('pages', case_pages), ('pages-refused', case_pages_refused), ('room', case_win_room),
               ('pages-four', case_pages_four)]


# ---- through a real server ----

def internet_session(case, lobby, paths, words):
    lobby_session(case, HEADLESS, ['--net-paths', paths], ['--net-paths', paths], words, lobby_at=lobby,
                  host_ip='0.0.0.0', guest_ip='0.0.0.0')


def run(cases, base):
    results = []
    threads = []
    for n, (name, fn) in enumerate(cases):
        case = Case(name, base + n * PORTS_PER_CASE)

        def go(case=case, fn=fn):
            start = time.time()
            try:
                fn(case)
            except Exception as e:  # a case's own failure, said as one
                case.fail('raised %r' % e)
            finally:
                case.stop()
            case.seconds = time.time() - start
        threads.append(threading.Thread(target=go))
        results.append(case)
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    problems = []
    for case in results:
        print('port_net_lobby: %s: %s (%.0f s)' % (case.name, 'FAILED' if case.problems else 'ok', case.seconds),
              flush=True)
        for line in case.said:
            print('    ' + line)
        problems += case.problems
    return problems


def main():
    ap = argparse.ArgumentParser(description='The ways between two players, through a lobby server, on this PC.')
    ap.add_argument('what', choices=('headless', 'windows', 'internet'))
    ap.add_argument('--port-base', type=int, default=BASE_PORT)
    ap.add_argument('--only', nargs='*', help='only these cases')
    ap.add_argument('--lobby', help='internet: the real server, ADDRESS:PORT')
    args = ap.parse_args()
    if not os.path.isfile(playtest.RETAIL_CUE):
        print('port_net_lobby: skipped: no %s' % playtest.RETAIL_CUE)
        return 0
    if args.what == 'internet':
        if not args.lobby:
            ap.error('internet needs --lobby')
        cases = [('internet-punch', lambda c: internet_session(c, args.lobby, 'public,lan,mapped,v6,relay', r'the way')),
                 ('internet-relay', lambda c: internet_session(c, args.lobby, 'relay', r'no hole could be punched'))]
    elif args.what == 'windows':
        if not IS_WINDOWS:
            ap.error('windows: from Windows\' own Python')
        cases = HEADLESS_CASES + WINDOW_CASES
    else:
        cases = HEADLESS_CASES
    later = LATER_CASES if args.what == 'headless' else WINDOWS_LATER_CASES if args.what == 'windows' else []
    fours = [FOUR_CASES, FOUR_LATER_CASES] if args.what != 'internet' else []
    if args.only:
        cases = [c for c in cases if c[0] in args.only]
        later = [c for c in later if c[0] in args.only]
        fours = [[c for c in batch if c[0] in args.only] for batch in fours]
    for program in {HEADLESS} | ({WINDOW} if args.what == 'windows' else set()):
        if not os.path.isfile(os.path.join(REPO, program)):
            print('FAILED: port_net_lobby: no %s (build it first)' % program)
            return 1
    problems = run(cases, args.port_base) if cases else []
    if later:
        problems += run(later, args.port_base + len(cases) * PORTS_PER_CASE)
    base = args.port_base + (len(cases) + len(later)) * PORTS_PER_CASE
    for batch in fours:
        if batch:
            problems += run(batch, base)
            base += len(batch) * PORTS_PER_CASE
    for problem in problems:
        print('FAILED: port_net_lobby: ' + problem)
    if not problems:
        print('OK: port_net_lobby %s' % args.what)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
