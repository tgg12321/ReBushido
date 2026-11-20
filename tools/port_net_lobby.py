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
    refused  a wrong password, an incompatible disc, no such room, a lobby
             that does not answer: each turned away, and why

`windows` (Windows' own Python) adds the window's programs: a session
headless and with a window (tools/port_net.py's, with a window: in real
time), the menu's restart (_spawnv) by address and by a room with a
password browsed and joined from the page, the router's port mapped by a
fake router (tools/relay/fake_router.py) and the guest coming through it,
and the refusals of the window: another build, disc, mods or protocol,
nobody at the address, the host gone quiet, a wrong password and an
incompatible room, each said on the guest's screen and the host's.

Every case runs at once, each with its own server and ports from
--port-base (PORTS_PER_CASE apart). Outputs in build/net-lobby/. Exits 1
if one failed. Needs the retail disc's image as tools/port_net.py does,
and says so and passes without it."""
import argparse
import ctypes
import os
import re
import shutil
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
OUT = os.path.join(REPO, 'build', 'net-lobby')
SECONDS = 420
PASSWORD = 'sesame7'
TROUBLE = ['--net-latency', '60', '--net-loss', '5']
DUMMY = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy', **port_net.NO_CONTROLLERS)
# A plan is made for one program's names (playtest's globals): one at a
# time, the cases running side by side.
PLAN_LOCK = threading.Lock()
_write_session_plan = port_net.write_session_plan


def _locked_plan(program, side, folder):
    with PLAN_LOCK:
        return _write_session_plan(program, side, folder)


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


HEADLESS_CASES = [('punch', case_punch), ('relay', case_relay), ('mapped', case_mapped), ('v6', case_v6),
                  ('direct6', case_direct6), ('punchto', case_punchto), ('list', case_list),
                  ('refused', case_refused)]


# ---- the window's cases (Windows) ----

def wait_pid(pid, seconds):
    """A process this program did not start, waited for (Windows): its exit
    code, or None."""
    kernel = ctypes.windll.kernel32
    handle = kernel.OpenProcess(0x00100000 | 0x1000, False, pid)  # SYNCHRONIZE | QUERY_LIMITED
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
    menu and starting again from the Play online page: then the new ones,
    whose process numbers they print, waited for and their hashes held to
    each other."""
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
                                        '--menu-keys', keys[side](), '--card1',
                                        os.path.join(case.folder, 'card1.mcd'), '--card2',
                                        os.path.join(case.folder, 'card2.mcd')], DUMMY))
    hashes = []
    for side, proc in enumerate(procs):
        label = 'side%d' % side
        first = wait_all([proc], 120)[0]
        pid = case.wait_for(label, r'started again as process (\d+)', 5)
        if first != 0 or pid is None:
            case.fail('%s did not start again from the menu (exit %s)' % (label, first))
            continue
        code = wait_pid(int(pid.group(1)), SECONDS)
        if code != 0:
            case.fail('%s, started again (process %s), ended with %s' % (label, pid.group(1), code))
        hashes.append(port_net.hashes_of(case.err(label)))
    if len(hashes) == 2:
        common = sorted(set(hashes[0]) & set(hashes[1]))
        if not common or any(hashes[0][f] != hashes[1][f] for f in common):
            case.fail('the two programs\' hashes are not the same (%d in common)' % len(common))
        else:
            case.said.append('both started again (_spawnv) and played on: %d state hashes the same' % len(common))


def case_win_menu(case):
    port = case.port(1)
    # the page's rows: by address the host's are You, its port, Start; the
    # guest's You, the host's address, Start
    keys = [lambda: 'up up up accept down down accept',
            lambda: 'up up up accept down accept type:127.0.0.1:%d down accept' % port]
    settings = ['net_role = host\nnet_port = %d\nnet_delay = 2\n' % port, 'net_role = join\n']
    menu_pair(case, settings, keys)


def case_win_rooms(case):
    lobby = case.server()
    port = case.port(1)
    # Host a room: You, Lobby, Room's name, Password, Your name, Start;
    # Browse rooms: You, Lobby, Your name, Look for rooms, the rooms
    keys = [lambda: 'up up up accept down down down accept type:%s down down accept' % PASSWORD,
            lambda: 'up up up accept down down down accept down accept type:%s' % PASSWORD]
    settings = ['net_role = room\nnet_lobby = %s\nnet_room = Menu room\nnet_player = Hostess\nnet_port = %d\n'
                % (lobby, port), 'net_role = rooms\nnet_lobby = %s\nnet_player = Browser\n' % lobby]

    def host_open():
        if case.wait_for('side0', r"room's code is (\w+)", 60) is None:
            case.fail('the host\'s room was not opened from the menu')
    menu_pair(case, settings, keys, host_open)
    if 'starts again as the guest of a session by way of the lobby' not in case.err('side1'):
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


WINDOW_CASES = [('win-headless', case_win_headless), ('win-window', case_win_window), ('win-menu', case_win_menu),
                ('win-rooms', case_win_rooms), ('win-portmap', case_win_portmap), ('win-refused', case_win_refused),
                ('win-quiet', case_win_quiet), ('win-lobby-refused', case_win_lobby_refused)]


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
    if args.only:
        cases = [c for c in cases if c[0] in args.only]
    for program in {HEADLESS} | ({WINDOW} if args.what == 'windows' else set()):
        if not os.path.isfile(os.path.join(REPO, program)):
            print('FAILED: port_net_lobby: no %s (build it first)' % program)
            return 1
    problems = run(cases, args.port_base)
    for problem in problems:
        print('FAILED: port_net_lobby: ' + problem)
    if not problems:
        print('OK: port_net_lobby %s' % args.what)
    return 1 if problems else 0


if __name__ == '__main__':
    sys.exit(main())
