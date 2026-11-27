#!/usr/bin/env python3
"""The PC program's updater, held to a release server of its own (docs/
PORT.md, "Updates"; BUILD.md, "Publishing a release").

    python tools/port_update_test.py [--image CUE] [--only CASE,...] [--keep]

On Windows (Windows' own Python), after `make port-sdl-windows`. Nothing is
asked of GitHub: a server on 127.0.0.1 answers as GitHub's API does
(`/releases/latest` and the assets it names), and the program is pointed
at it with --update-url and given a key made for the run with
--update-key, which a trial build alone takes. Each case installs the
build's program in a folder of its own under build/tests/port_update/
with a player's files beside it (rebushido.cfg, a card, a log, a
screenshot, a mod made from the disc, a mod of the player's own), runs it
as a test runs the window (no window shown, no sound: SDL's dummy drivers,
--hidden, --no-sound, --no-cards, --frames), and reads what it did from
its logs, update/apply.log and the files:

  up-to-date     the newest release is this version: nothing downloaded
  newer          a newer one: downloaded, every file held to the signed
                 manifest, put in place at the next start by the step of
                 update/apply/, started as the new version; the player's
                 files as they were, the file the new version dropped
                 taken away, update/old/ and update/staged/ gone
  bad-signature  a manifest signed by another key: refused, nothing
                 downloaded
  bad-hash       a signed manifest whose zip's SHA-256 is not the zip's:
                 the download refused, nothing staged
  bad-file       ... whose file's SHA-256 is not the file's: refused
  truncated      the zip cut short: refused
  offline        no server: the game starts and plays as ever
  timeout        a server that never answers: the same, the check given up
  rollback       the new version's program cannot start: every file put
                 back and the old program started again, which says so
  off            updates = off: not one request
  menu           updates = off, and the settings menu's Updates page's
                 Check for updates now chosen: it asks, and finds the newer

The disc image: --image, REBUSHIDO_RETAIL, or the .cue at the repository's
root (or the main checkout's, from a worktree). The emulator is not used.
"""
import argparse
import glob
import hashlib
import http.server
import io
import json
import os
import shutil
import socketserver
import subprocess
import sys
import threading
import time
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import ed25519  # noqa: E402
import release_sign  # noqa: E402

BUILT = os.path.join(ROOT, 'build', 'port-sdl', 'windows')
WORK = os.path.join(ROOT, 'build', 'tests', 'port_update')
PROGRAM = ['rebushido.exe', 'SDL2.dll', 'rebushido.exe.sym']
COMMIT = '0000000'
FRAMES = 900
RUN_SECONDS = 120
CASES = ['up-to-date', 'newer', 'bad-signature', 'bad-hash', 'bad-file', 'truncated', 'offline', 'timeout',
         'rollback', 'off', 'menu']

# The player's files, which no update may touch.
PLAYER_FILES = {
    'rebushido.cfg': None,  # the case's settings
    'card1.mcd': b'MC' + bytes(range(256)) * 64,
    'logs/rebushido-20260101-000000.log': b'rebushido: an old session\n',
    'screenshots/shot-20260101-000000-000.png': b'\x89PNG\r\n\x1a\nnot really\n',
    'mods/art/made-from-disc.txt': b'key = ReBushido 0.1 disc 1-00000000\n',
    'mods/art/notes.txt': b'made from the disc\n',
    'mods/mine/mod.txt': b'name = mine\nversion = 1.0\n',
    'mods/mine/notes.txt': b'a mod of my own\n',
}


def fail(message):
    sys.exit('port_update_test.py: ' + message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def version_number():
    import re
    text = open(os.path.join(ROOT, 'include', 'rebushido.h'), encoding='utf-8').read()
    return re.search(r'^#define RB_VERSION_NUMBER "([^"]+)"', text, re.M).group(1)


def newer_version(version):
    """A version after `version` of the same length, so that the program's
    text can be changed in place: its last digit one more."""
    if not version[-1].isdigit() or version[-1] == '9':
        fail('the test makes the next version by its last digit, which %s cannot have' % version)
    return version[:-1] + str(int(version[-1]) + 1)


def find_image(named):
    if named:
        return os.path.abspath(named)
    if os.environ.get('REBUSHIDO_RETAIL'):
        return os.environ['REBUSHIDO_RETAIL']
    places = [ROOT]
    proc = subprocess.run(['git', 'rev-parse', '--git-common-dir'], capture_output=True, text=True, cwd=ROOT)
    if proc.returncode == 0:
        places.append(os.path.dirname(os.path.abspath(os.path.join(ROOT, proc.stdout.strip()))))
    for place in places:
        cues = sorted(glob.glob(os.path.join(place, '*.cue')))
        if cues:
            return cues[0]
    fail('no disc image: --image CUE or REBUSHIDO_RETAIL')


# ---- the packages ----


def package_files(files):
    lines = ['# made up for the test']
    lines += ['%s %d %s' % (sha256(files[rel]), len(files[rel]), rel) for rel in sorted(files)
              if rel != 'package-files.txt']
    return ('\n'.join(lines) + '\n').encode('utf-8')


def old_package():
    """{path: bytes} of the version installed: the build's program and
    mods, a README, and a licence the newer version drops."""
    files = {}
    for name in PROGRAM:
        path = os.path.join(BUILT, name)
        if not os.path.isfile(path):
            fail('%s is not built: wsl bash tools/build.sh port-sdl-windows' % path)
        files[name] = open(path, 'rb').read()
    for folder, dirs, names in os.walk(os.path.join(BUILT, 'mods')):
        for name in names:
            path = os.path.join(folder, name)
            files['mods/' + os.path.relpath(path, os.path.join(BUILT, 'mods')).replace(os.sep, '/')] = \
                open(path, 'rb').read()
    files['README.txt'] = b'the old version\r\n'
    files['licenses/dropped.txt'] = b'only the old version has this\r\n'
    files['package-files.txt'] = package_files(files)
    return files


def new_package(old, own, new, broken=False):
    """The newer version: its program saying `new`, a README changed, a
    licence added and one dropped. `broken`: a program that cannot start."""
    files = dict(old)
    del files['licenses/dropped.txt']
    before = ('ReBushido %s-dev (' % own).encode()
    after = ('ReBushido %s-dev (' % new).encode()
    if before not in files['rebushido.exe']:
        fail('the program does not say %r' % before)
    files['rebushido.exe'] = b'MZ not a program at all\r\n' * 64 if broken else \
        files['rebushido.exe'].replace(before, after)
    files['README.txt'] = b'the new version\r\n'
    files['licenses/added.txt'] = b'only the new version has this\r\n'
    files['package-files.txt'] = package_files(files)
    return files


def make_zip(files, version):
    folder = 'ReBushido-PC-%s-%s' % (version, COMMIT)
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for rel in sorted(files):
            info = zipfile.ZipInfo('%s/%s' % (folder, rel), date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, files[rel])
    return folder + '.zip', out.getvalue()


class Release:
    """What the server offers: the zip, its manifest and signature."""

    def __init__(self, zip_name, zip_data, secret, manifest_change=None, sign_with=None):
        scratch = os.path.join(WORK, 'release-scratch')
        os.makedirs(scratch, exist_ok=True)
        path = os.path.join(scratch, zip_name)
        with open(path, 'wb') as f:
            f.write(zip_data)
        text = release_sign.manifest_text(path, ['A test release.', '', 'Nothing in it is real.'])
        os.remove(path)
        if manifest_change:
            text = manifest_change(text)
        self.assets = {zip_name: zip_data, release_sign.MANIFEST: text.encode('utf-8'),
                       release_sign.SIGNATURE: release_sign.sign(text, sign_with or secret).encode()}
        self.zip_name = zip_name


# ---- the server ----


class Server(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = False

    def __init__(self):
        super().__init__(('127.0.0.1', 0), Handler)
        self.release = None
        self.mode = 'normal'
        self.requests = []
        self.stop_hanging = threading.Event()

    @property
    def url(self):
        return 'http://127.0.0.1:%d' % self.server_address[1]


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def send(self, data, kind='application/octet-stream', length=None):
        self.send_response(200)
        self.send_header('Content-Type', kind)
        self.send_header('Content-Length', str(len(data) if length is None else length))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        server = self.server
        server.requests.append(self.path)
        if server.mode == 'hang':
            server.stop_hanging.wait(60)
            return
        release = server.release
        if self.path.startswith('/releases'):
            if release is None:
                self.send_error(404)
                return
            body = {'tag_name': 'v-test', 'prerelease': False, 'body': 'notes',
                    'assets': [{'name': name, 'size': len(data),
                                'browser_download_url': '%s/download/v-test/%s' % (server.url, name)}
                               for name, data in release.assets.items()]}
            if self.path.startswith('/releases?'):
                body = [body]
            self.send(json.dumps(body, indent=2).encode(), 'application/json')
            return
        name = self.path.rsplit('/', 1)[-1]
        if not self.path.startswith('/download/') or release is None or name not in release.assets:
            self.send_error(404)
            return
        data = release.assets[name]
        if server.mode == 'truncate' and name == release.zip_name:
            self.send(data[:len(data) // 2])
            return
        self.send(data)


# ---- a case ----


class Case:
    def __init__(self, name, image, server, public, settings):
        self.name = name
        self.image = image
        self.server = server
        self.public = public
        self.dir = os.path.join(WORK, name)
        self.settings = settings

    def install(self, files):
        if os.path.isdir(self.dir):
            remove_tree(self.dir)
        os.makedirs(self.dir)
        for rel, data in files.items():
            self.write(rel, data)
        player = dict(PLAYER_FILES)
        player['rebushido.cfg'] = ('# a player\'s settings\n%s\ndisc_mods = off\n' % self.settings).encode()
        for rel, data in player.items():
            self.write(rel, data)
        self.player = player

    def write(self, rel, data):
        path = os.path.join(self.dir, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)

    def read(self, rel):
        path = os.path.join(self.dir, *rel.split('/'))
        return open(path, 'rb').read() if os.path.isfile(path) else None

    def exists(self, rel):
        return os.path.exists(os.path.join(self.dir, *rel.split('/')))

    def run(self, url=None, more=()):
        """The program once, to its end: its exit code."""
        command = [os.path.join(self.dir, 'rebushido.exe'), '--image', self.image, '--hidden', '--no-sound',
                   '--no-cards', '--update-url', url or self.server.url, '--update-key', self.public.hex(),
                   '--frames', str(FRAMES)] + list(more)
        env = dict(os.environ, SDL_VIDEODRIVER='dummy', SDL_AUDIODRIVER='dummy')
        with open(os.path.join(self.dir, 'run-output.txt'), 'ab') as out:
            proc = subprocess.Popen(command, cwd=self.dir, env=env, stdout=out, stderr=subprocess.STDOUT,
                                    stdin=subprocess.DEVNULL)
            try:
                return proc.wait(RUN_SECONDS)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait()
                return 'killed after %d seconds' % RUN_SECONDS

    def logs(self):
        text = ''
        for path in sorted(glob.glob(os.path.join(self.dir, 'logs', 'rebushido-*.log'))):
            text += open(path, encoding='utf-8', errors='replace').read()
        return text

    def apply_log(self):
        return (self.read('update/apply.log') or b'').decode('utf-8', 'replace')

    def wait_for(self, what, seconds=RUN_SECONDS):
        """Until `what()` is true (the program the step started, which this
        did not start, has ended): whether it was."""
        until = time.time() + seconds
        while time.time() < until:
            if what():
                return True
            time.sleep(0.5)
        return what()

    def player_files_intact(self):
        problems = []
        for rel, data in self.player.items():
            if self.read(rel) != data:
                problems.append('%s was changed' % rel)
        return problems

    def files_are(self, files):
        problems = []
        for rel, data in files.items():
            got = self.read(rel)
            if got is None:
                problems.append('%s is missing' % rel)
            elif got != data:
                problems.append('%s is not the version\'s' % rel)
        return problems


def remove_tree(path):
    path = os.path.abspath(path)
    if not path.startswith(os.path.abspath(WORK) + os.sep):
        fail('will not remove %s: not a folder of %s' % (path, WORK))
    shutil.rmtree(path)


def sessions_ended(case, count):
    return case.logs().count('session: ended') >= count


def run_case(name, image, server, secret, public, own, new, old_files):
    """The problems of case `name`: an empty list if it passed."""
    settings = 'updates = off' if name in ('off', 'menu') else 'updates = auto'
    case = Case(name, image, server, public, settings)
    case.install(old_files)
    server.requests.clear()
    server.mode = 'normal'
    server.release = None
    problems = []
    newer = new_package(old_files, own, new, broken=(name == 'rollback'))
    if name == 'up-to-date':
        zip_name, data = make_zip(old_files, own)
        server.release = Release(zip_name, data, secret)
    elif name in ('newer', 'rollback', 'off', 'truncated', 'menu'):
        zip_name, data = make_zip(newer, new)
        server.release = Release(zip_name, data, secret)
        server.mode = 'truncate' if name == 'truncated' else 'normal'
    elif name == 'bad-signature':
        zip_name, data = make_zip(newer, new)
        server.release = Release(zip_name, data, secret, sign_with=os.urandom(32))
    elif name == 'bad-hash':
        zip_name, data = make_zip(newer, new)
        server.release = Release(zip_name, data, secret, manifest_change=lambda text: text.replace(
            'zip_sha256 = ' + sha256(data), 'zip_sha256 = ' + sha256(b'other'), 1))
    elif name == 'bad-file':
        zip_name, data = make_zip(newer, new)
        readme = sha256(newer['README.txt'])
        server.release = Release(zip_name, data, secret,
                                 manifest_change=lambda text: text.replace(readme, sha256(b'other'), 1))
    elif name == 'timeout':
        server.mode = 'hang'
    url = 'http://127.0.0.1:9' if name == 'offline' else None

    # the menu: its first page's rows are the seven groups (Training the
    # sixth, Tournament the seventh), Replays, Stats, Play online and
    # Updates; on the Updates page the first row is Check for updates now
    more = ['--menu-at', '300', '--menu-keys',
            'down,down,down,down,down,down,down,down,down,down,accept,accept,back,close'] \
        if name == 'menu' else []
    code = case.run(url, more)
    log = case.logs()
    if code != 0:
        problems.append('the program ended %s' % code)
    if 'session: ended as asked' not in log:
        problems.append('the game did not run to its end')

    if name == 'off':
        if server.requests:
            problems.append('updates = off asked %s' % server.requests)
        if 'update: ' in log and 'asking' in log:
            problems.append('updates = off asked something')
    elif name == 'menu':
        if not any(r.startswith('/releases') for r in server.requests):
            problems.append('Check for updates now asked nothing')
        if 'update: 0.2 is available' not in log.replace(new, '0.2'):
            problems.append('it did not find the newer version')
        if any('/download/v-test/ReBushido' in r for r in server.requests):
            problems.append('updates = off downloaded the zip by itself')
    elif name == 'up-to-date':
        if 'update: up to date' not in log:
            problems.append('it did not say it is up to date')
        if case.exists('update/download') or case.exists('update/ready.txt'):
            problems.append('something was downloaded')
    elif name in ('offline', 'timeout'):
        if 'update: the releases cannot be asked' not in log:
            problems.append('it did not say the releases cannot be asked')
        if name == 'timeout' and 'no answer in time' not in log:
            problems.append('it did not say there was no answer in time')
    elif name in ('bad-signature', 'bad-hash', 'bad-file', 'truncated'):
        said = {'bad-signature': 'manifest is refused', 'bad-hash': 'SHA-256 is not the manifest',
                'bad-file': 'is not the manifest\'s: its SHA-256 differs', 'truncated': 'came short'}[name]
        if said not in log:
            problems.append('it did not refuse it as it should ("%s")' % said)
        if case.exists('update/ready.txt') or case.exists('update/staged'):
            problems.append('something is staged')
        if name == 'bad-signature' and any('/download/v-test/ReBushido' in r for r in server.requests):
            problems.append('the zip was downloaded though its manifest was refused')
        # the next start installs nothing
        code = case.run()
        if code != 0 or case.apply_log():
            problems.append('the next start installed something')
    if name in ('newer', 'rollback'):
        if 'is ready: every file is the manifest\'s' not in log:
            problems.append('the update was not made ready')
            return problems + case.player_files_intact()
        # the next start: put in place by update/apply/, then the new
        # program (or, for rollback, the old one again) runs to its end
        code = case.run()
        if code != 0:
            problems.append('the start that installs it ended %s' % code)
        ended = case.wait_for(lambda: sessions_ended(case, 3) and (
            'started: kept' in case.apply_log() or 'old program started again' in case.apply_log()))
        if not ended:
            problems.append('the program after the update did not run to its end')
        apply_log = case.apply_log()
        log = case.logs()
        if name == 'newer':
            if 'version %s started: kept' % new not in apply_log:
                problems.append('update/apply.log does not say it was kept')
            if 'version: ReBushido %s-dev' % new not in log:
                problems.append('the new version did not run')
            problems += case.files_are(newer)
            if case.exists('licenses/dropped.txt'):
                problems.append('the file the new version dropped is there')
            for rel in ('update/old', 'update/staged', 'update/ready.txt', 'update/started.txt'):
                if case.exists(rel):
                    problems.append('%s is left' % rel)
        else:
            if 'put back:' not in apply_log or 'old program started again' not in apply_log:
                problems.append('update/apply.log does not say it was put back')
            if 'update: %s did not start, and %s was put back' % (new, own) not in log:
                problems.append('the old program did not say it was put back')
            problems += case.files_are(old_files)
            state = (case.read('update/state.txt') or b'').decode()
            if 'failed = %s' % new not in state:
                problems.append('update/state.txt does not keep the version that did not start')
    problems += case.player_files_intact()
    return problems


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--image')
    parser.add_argument('--only', help='cases, a comma between: ' + ','.join(CASES))
    parser.add_argument('--keep', action='store_true', help='keep build/tests/port_update/')
    args = parser.parse_args()
    if os.name != 'nt':
        print('port_update_test: skipped: it runs on Windows, with Windows\' own Python (the program it tests '
              'is the Windows one)')
        return 0
    image = find_image(args.image)
    cases = args.only.split(',') if args.only else CASES
    for name in cases:
        if name not in CASES:
            fail('no case %s' % name)
    for name in os.listdir(WORK) if os.path.isdir(WORK) else []:
        remove_tree(os.path.join(WORK, name))
    os.makedirs(WORK, exist_ok=True)
    secret = os.urandom(32)
    public = ed25519.public_key(secret)
    own = version_number()
    new = newer_version(own)
    old_files = old_package()
    server = Server()
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    print('port_update_test: %s -> %s, a key of the run\'s own (%s...), the server at %s, the disc %s'
          % (own, new, public.hex()[:16], server.url, image))
    failed = 0
    try:
        for name in cases:
            began = time.time()
            problems = run_case(name, image, server, secret, public, own, new, old_files)
            server.stop_hanging.set()
            server.stop_hanging.clear()
            if problems:
                failed += 1
                print('FAILED: %s (%.0f s): %s' % (name, time.time() - began, '; '.join(problems)))
            else:
                print('ok: %s (%.0f s)' % (name, time.time() - began))
    finally:
        server.stop_hanging.set()
        server.shutdown()
    if not args.keep and not failed:
        shutil.rmtree(WORK)
    print('port_update_test: %s (%d of %d cases)' % ('FAILED' if failed else 'OK', len(cases) - failed, len(cases)))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
