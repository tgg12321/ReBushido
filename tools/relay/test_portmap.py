#!/usr/bin/env python3
"""The window's port mapping (src/port/window/portmap.c) against a fake
router (tools/relay/fake_router.py): each way of mapping, the renewal, the
removal, the fallbacks and the failures.

    python3 tools/relay/test_portmap.py      in WSL or on Linux: built with gcc
    python tools/relay/test_portmap.py       on Windows: built with WSL's
                                             x86_64-w64-mingw32-gcc, run here
    --no-build    run the program built before
    -v            show every line the router and the program printed

It builds tools/relay/portmap_test.c with portmap.c into
build/portmap_test/, and for each case starts the fake router, runs the
program against it, and checks the program's line and the requests the
router got. Exit 1 if a case failed.
"""
import os
import re
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
WINDOWS = os.name == 'nt'
OUT_DIR = 'build/portmap_test'
PROGRAM = OUT_DIR + '/portmap_test' + ('.exe' if WINDOWS else '')
SOURCES = ['tools/relay/portmap_test.c', 'src/port/window/portmap.c']
FLAGS = ['-O2', '-g', '-std=gnu99', '-Wall', '-Wextra', '-Werror', '-Wno-format-truncation', '-Iinclude',
         '-Isrc/port/window']
ROUTER = os.path.join(ROOT, 'tools', 'relay', 'fake_router.py')
PORT = 7845
WAIT_MS = 3000

# name, the router's options, the program's, what its line must have, and
# the router's lines: (pattern, how many times)
CASES = [
    ('pcp', ['--external', '203.0.113.7'], ['--renew'],
     ['ok=1 method=PCP external=203.0.113.7:7845 lease=7200 '],
     [(r'pcp map UDP 7845 -> 7845 lease 7200 \(client 127\.0\.0\.1\)$', 2),
      (r'pcp delete UDP 7845', 1), (r'pmp ', 0), (r'upnp (GET|Add|Get)', 0)]),
    ('pcp-short-lease', ['--lease-cap', '30'], [],
     ['ok=1 method=PCP external=127.0.0.1:7845 lease=30 '],
     [(r'pcp map UDP 7845 -> 7845 lease 30 ', 1), (r'pcp delete UDP 7845', 1)]),
    ('nat-pmp', ['--no-pcp', '--external', '198.51.100.9'], ['--renew'],
     ['ok=1 method=NAT-PMP external=198.51.100.9:7845 lease=7200 '],
     [(r'pcp request answered as NAT-PMP', 1), (r'pmp external address$', 1),
      (r'pmp map UDP 7845 -> 7845 lease 7200$', 2), (r'pmp delete UDP 7845$', 1), (r'upnp (GET|Add)', 0)]),
    ('upnp', ['--no-pcp', '--no-pmp', '--external', '192.0.2.44'], ['--renew'],
     ['ok=1 method=UPnP external=192.0.2.44:7845 lease=7200 '],
     [(r'upnp M-SEARCH urn:schemas-upnp-org:device:InternetGatewayDevice:1$', 1),
      (r'upnp GET /rootDesc\.xml$', 1), (r'upnp GetExternalIPAddress$', 1),
      (r'upnp AddPortMapping UDP 7845 -> 127\.0\.0\.1:7845 lease 7200$', 2),
      (r'upnp DeletePortMapping UDP 7845$', 1), (r'refused', 0), (r'description', 0)]),
    ('upnp-chunked', ['--no-pcp', '--no-pmp', '--chunked'], [],
     ['ok=1 method=UPnP external=127.0.0.1:7845 lease=7200 '],
     [(r'upnp AddPortMapping UDP 7845 -> 127\.0\.0\.1:7845 lease 7200$', 1),
      (r'upnp DeletePortMapping UDP 7845$', 1)]),
    ('upnp-permanent-only', ['--no-pcp', '--no-pmp', '--permanent-only'], ['--renew'],
     ['ok=1 method=UPnP external=127.0.0.1:7845 lease=0 '],
     [(r'upnp AddPortMapping UDP 7845 -> 127\.0\.0\.1:7845 lease 7200 refused 725$', 1),
      (r'upnp AddPortMapping UDP 7845 -> 127\.0\.0\.1:7845 lease 0$', 1),
      (r'upnp DeletePortMapping UDP 7845$', 1)]),
    ('upnp-port-taken', ['--no-pcp', '--no-pmp', '--conflict', '7845'], [],
     ['ok=1 method=UPnP external=127.0.0.1:7846 lease=7200 '],
     [(r'upnp AddPortMapping UDP 7845 -> 127\.0\.0\.1:7845 lease 7200 refused 718$', 1),
      (r'upnp AddPortMapping UDP 7846 -> 127\.0\.0\.1:7845 lease 7200$', 1),
      (r'upnp DeletePortMapping UDP 7846$', 1), (r'DeletePortMapping UDP 7845', 0)]),
    ('upnp-refused', ['--no-pcp', '--no-pmp', '--refuse', '606'], [],
     ['ok=0 method= ', "why=The router's UPnP does not let programs open ports: forward UDP port 7845 by hand"],
     [(r'upnp AddPortMapping', 1), (r'DeletePortMapping', 0)]),
    ('upnp-no-search', ['--no-pcp', '--no-pmp', '--no-ssdp'], [],
     ['ok=0 ', 'why=No router answered (UPnP, NAT-PMP and PCP): forward UDP port 7845 by hand, or use a lobby room'],
     [(r'upnp', 0)]),
    ('none', ['--no-upnp', '--no-pcp', '--no-pmp'], [],
     ['ok=0 method= external=0.0.0.0:0 lease=0 ',
      'why=No router answered (UPnP, NAT-PMP and PCP): forward UDP port 7845 by hand, or use a lobby room'],
     [(r'(pcp|pmp|upnp)', 0)]),
]


def run_build():
    os.makedirs(os.path.join(ROOT, OUT_DIR), exist_ok=True)
    if WINDOWS:
        cmd = (['wsl', '--cd', ROOT, 'x86_64-w64-mingw32-gcc'] + FLAGS + SOURCES
               + ['-o', PROGRAM, '-lws2_32', '-static-libgcc'])
    else:
        cmd = ['gcc'] + FLAGS + SOURCES + ['-o', PROGRAM]
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if proc.returncode != 0 or proc.stderr.strip():
        print('FAILED: build: %s\n%s%s' % (' '.join(cmd), proc.stdout, proc.stderr))
        return False
    return True


def start_router(options):
    router = subprocess.Popen([sys.executable, ROUTER] + options, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                              stderr=subprocess.STDOUT, text=True, cwd=ROOT)
    line = router.stdout.readline()
    found = re.search(r'ready ssdp=(\d+) http=(\d+) gateway=(\d+)', line)
    if not found:
        router.kill()
        raise RuntimeError('the fake router did not start: %r' % line)
    return router, int(found.group(1)), int(found.group(3))


def run_case(name, router_options, test_options, wanted, requests, verbose):
    router, ssdp, gateway = start_router(router_options)
    try:
        proc = subprocess.run([os.path.join(ROOT, PROGRAM), '--ssdp', '127.0.0.1:%d' % ssdp, '--gateway',
                               '127.0.0.1:%d' % gateway, '--port', str(PORT), '--wait', str(WAIT_MS)]
                              + test_options, capture_output=True, text=True, timeout=30)
    finally:
        time.sleep(0.1)
        rest, _ = router.communicate(input='', timeout=10)
    lines = [line[len('fake_router: '):] for line in rest.splitlines() if line.startswith('fake_router: ')]
    problems = []
    if proc.returncode != 0:
        problems.append('the program ended with %d' % proc.returncode)
    map_line = next((line for line in proc.stdout.splitlines() if line.startswith('map ')), '')
    for part in wanted:
        if part not in map_line:
            problems.append('its line has no %r' % part)
    if 'unmapped' not in proc.stdout:
        problems.append('it did not get to the end')
    took = re.search(r' took=(\d+) ', map_line)
    if took and int(took.group(1)) > WAIT_MS + 500:
        problems.append('it took %s ms, over its %d' % (took.group(1), WAIT_MS))
    for pattern, count in requests:
        got = sum(1 for line in lines if re.search(pattern, line))
        if got != count:
            problems.append('the router got %r %d times, not %d' % (pattern, got, count))
    print('%-6s %-20s %s' % ('ok' if not problems else 'FAILED', name, map_line))
    for problem in problems:
        print('         ' + problem)
    if verbose or problems:
        for line in proc.stdout.splitlines() + proc.stderr.splitlines():
            print('         program: ' + line)
        for line in lines:
            print('         router:  ' + line)
    return not problems


def run_silent(verbose):
    """Nothing at either end answers, nor says that nothing listens: the
    whole wait is used and no more."""
    quiet = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
    for sock in quiet:
        sock.bind(('127.0.0.1', 0))
    wait = 2000
    proc = subprocess.run([os.path.join(ROOT, PROGRAM), '--ssdp', '127.0.0.1:%d' % quiet[0].getsockname()[1],
                           '--gateway', '127.0.0.1:%d' % quiet[1].getsockname()[1], '--wait', str(wait)],
                          capture_output=True, text=True, timeout=30)
    quiet[1].settimeout(1)
    try:
        pcp = quiet[1].recv(2048)
    except OSError:
        pcp = b''
    for sock in quiet:
        sock.close()
    map_line = next((line for line in proc.stdout.splitlines() if line.startswith('map ')), '')
    took = re.search(r' took=(\d+) ', map_line)
    problems = []
    # PCP, NAT-PMP and the search take six eighths of it; HTTP would
    # have the rest
    if not took or not wait // 2 <= int(took.group(1)) <= wait + 300:
        problems.append('it took %s ms of its %d' % (took.group(1) if took else '?', wait))
    if 'ok=0 ' not in map_line or 'No router answered' not in map_line:
        problems.append('its line is not a failure that nobody answered')
    if len(pcp) != 60 or pcp[0] != 2 or pcp[1] != 1:
        problems.append('the gateway did not get a PCP MAP request of 60 bytes')
    print('%-6s %-20s %s' % ('ok' if not problems else 'FAILED', 'silent', map_line))
    for problem in problems:
        print('         ' + problem)
    if verbose:
        print('         program: ' + proc.stdout.replace('\n', '\n         program: '))
    return not problems


def main(args):
    verbose = '-v' in args
    if '--no-build' not in args and not run_build():
        return 1
    print('%s: %s' % ('Windows' if WINDOWS else 'Linux', PROGRAM))
    failed = 0
    for case in CASES:
        if not run_case(*case, verbose=verbose):
            failed += 1
    if not run_silent(verbose):
        failed += 1
    print('%d of %d cases failed' % (failed, len(CASES) + 1) if failed else 'all %d cases passed' % (len(CASES) + 1))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
