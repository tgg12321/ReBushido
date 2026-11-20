#!/usr/bin/env python3
"""The release key, and the signed manifest of a PC package (BUILD.md,
"Publishing a release"; docs/PORT.md, "Updates").

    python tools/release_sign.py --generate [--key-dir DIR]
    python tools/release_sign.py --manifest ZIP [--notes FILE] [--channel beta]
                                 [--key-dir DIR] [--out FOLDER]
    python tools/release_sign.py --verify MANIFEST [--public HEX]
    python tools/release_sign.py --self-test

--generate makes the release key once: an Ed25519 key pair in DIR, which
is outside the repository (by default %USERPROFILE%\\.rebushido-release-key,
~/.rebushido-release-key elsewhere): secret.txt, the hex of its 32 secret
bytes, and public.txt. It never writes over a key that is there, and
refuses a folder inside the repository. The public half goes into
config/release_public_key.txt (it says how); the secret half never leaves
the owner's PC and is never committed. Keep a copy of the folder somewhere
safe: a release signed with another key is refused by every program
already out, and they can then only be updated by hand.

--manifest writes update-manifest.txt and update-manifest.txt.sig beside
the zip (or into --out) for the package ZIP (tools/port_release.py's): its
version and commit from the name, the zip's size and SHA-256, every file's
SHA-256, size and path, the notes (--notes, a line each) and the updater
the program must have; and signs it with the key in DIR. It refuses a key
whose public half is not config/release_public_key.txt's, since no
program built from this tree would take it.

--verify holds a manifest to its .sig with the key of the config (or
--public). The program does the same with Monocypher
(src/port/window/update_core.c); this uses tools/ed25519.py.
"""
import argparse
import hashlib
import os
import re

import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import ed25519  # noqa: E402  (tools/ed25519.py)

CONFIG_KEY = os.path.join(ROOT, 'config', 'release_public_key.txt')
UPDATE_H = os.path.join(ROOT, 'src', 'port', 'window', 'update.h')
MANIFEST = 'update-manifest.txt'
SIGNATURE = MANIFEST + '.sig'
FIRST_LINE = 'rebushido-update 1'
ZIP_NAME = re.compile(r'^ReBushido-PC-(?P<version>[0-9][0-9.]*)(?P<dev>-dev)?-(?P<commit>[0-9a-f]{7,40}(?:-dirty)?)\.zip$')


def fail(message):
    sys.exit('release_sign.py: ' + message)


def default_key_dir():
    home = os.environ.get('USERPROFILE') or os.path.expanduser('~')
    return os.path.join(home, '.rebushido-release-key')


def is_inside_repository(path):
    path = os.path.realpath(path)
    root = os.path.realpath(ROOT)
    return os.path.commonpath([path, root]) == root


def updater_version():
    """PORT_UPDATER_VERSION of src/port/window/update.h."""
    match = re.search(r'^#define PORT_UPDATER_VERSION (\d+)', open(UPDATE_H, encoding='utf-8').read(), re.M)
    if not match:
        fail('%s has no PORT_UPDATER_VERSION' % UPDATE_H)
    return int(match.group(1))


def config_public_key():
    """config/release_public_key.txt's key as bytes, or None for `none`."""
    words = [line.strip() for line in open(CONFIG_KEY, encoding='utf-8')
             if line.strip() and not line.strip().startswith('#')]
    if words == ['none']:
        return None
    if len(words) != 1 or not re.fullmatch(r'[0-9a-f]{64}', words[0]):
        fail('%s is neither `none` nor 64 hex digits' % CONFIG_KEY)
    return bytes.fromhex(words[0])


# ---- the key ----


def generate(key_dir):
    if is_inside_repository(key_dir):
        fail('%s is inside the repository: the key is kept outside it (--key-dir)' % key_dir)
    secret_path = os.path.join(key_dir, 'secret.txt')
    if os.path.exists(secret_path):
        fail('%s is there already: a release key is made once (delete it yourself if you mean to replace '
             'it; every program already out would then refuse the next release)' % secret_path)
    os.makedirs(key_dir, exist_ok=True)
    secret = os.urandom(32)  # (not secrets: tests/random.py shadows random)
    public = ed25519.public_key(secret)
    # the secret file made readable by its owner alone where the system
    # has such modes
    descriptor = os.open(secret_path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, 'w', newline='\n') as f:
        f.write(secret.hex() + '\n')
    with open(os.path.join(key_dir, 'public.txt'), 'w', newline='\n') as f:
        f.write(public.hex() + '\n')
    print('The release key is made in %s.' % key_dir)
    print('Keep a copy of that folder somewhere safe and private; never commit it.')
    print()
    print('Its public half, to put in %s in place of `none`:' % os.path.relpath(CONFIG_KEY, ROOT))
    print()
    print('    ' + public.hex())
    print()
    print('Then commit that file: every build from then on takes updates signed with this key.')
    return 0


def load_secret(key_dir):
    path = os.path.join(key_dir, 'secret.txt')
    if not os.path.isfile(path):
        fail('no release key in %s (python tools/release_sign.py --generate)' % key_dir)
    text = open(path, encoding='utf-8').read().strip()
    if not re.fullmatch(r'[0-9a-f]{64}', text):
        fail('%s is not a key' % path)
    return bytes.fromhex(text)


# ---- the manifest ----


def manifest_text(zip_path, notes=(), channel='release'):
    """update-manifest.txt for the package `zip_path`, as text."""
    name = os.path.basename(zip_path)
    match = ZIP_NAME.match(name)
    if not match:
        fail('%s is not a package\'s name (ReBushido-PC-<version>-<commit>.zip)' % name)
    folder = name[:-len('.zip')]
    data = open(zip_path, 'rb').read()
    lines = [FIRST_LINE,
             'version = %s' % match.group('version'),
             'commit = %s' % match.group('commit'),
             'channel = %s' % channel,
             'updater = %d' % updater_version(),
             'zip = %s' % name,
             'zip_size = %d' % len(data),
             'zip_sha256 = %s' % hashlib.sha256(data).hexdigest(),
             'folder = %s' % folder]
    for note in notes:
        note = note.rstrip()
        if len(note.encode('utf-8')) >= 120:
            fail('a line of the notes is longer than the program shows (120 bytes): %s' % note)
        lines.append('note = %s' % note if note else 'note =')
    with zipfile.ZipFile(zip_path) as z:
        for info in sorted(z.infolist(), key=lambda i: i.filename):
            if info.filename.endswith('/'):
                continue
            if not info.filename.startswith(folder + '/'):
                fail('%s is not in the package\'s folder %s/' % (info.filename, folder))
            body = z.read(info)
            lines.append('file = %s %d %s' % (hashlib.sha256(body).hexdigest(), len(body),
                                              info.filename[len(folder) + 1:]))
    return '\n'.join(lines) + '\n'


def sign(text, secret):
    """The signature of `text` (its UTF-8 bytes) as the .sig file has it."""
    return ed25519.sign(secret, text.encode('utf-8')).hex() + '\n'


def verify(text_bytes, signature_text, public):
    try:
        signature = bytes.fromhex(signature_text.strip())
    except ValueError:
        return False
    return ed25519.verify(public, text_bytes, signature)


def write_manifest(zip_path, out_dir, secret, notes=(), channel='release', check_config=True):
    """update-manifest.txt and its .sig into `out_dir`: their paths."""
    public = ed25519.public_key(secret)
    if check_config:
        configured = config_public_key()
        if configured is None:
            fail('%s says none: put the release key\'s public half there first' % CONFIG_KEY)
        if configured != public:
            fail('the key signing is not the one in %s: no program built from this tree would take it' % CONFIG_KEY)
    text = manifest_text(zip_path, notes, channel)
    signature = sign(text, secret)
    if not verify(text.encode('utf-8'), signature, public):
        fail('the signature just made does not verify')
    manifest_path = os.path.join(out_dir, MANIFEST)
    with open(manifest_path, 'w', encoding='utf-8', newline='\n') as f:
        f.write(text)
    with open(manifest_path + '.sig', 'w', encoding='utf-8', newline='\n') as f:
        f.write(signature)
    return manifest_path, manifest_path + '.sig'


def self_test():
    failures = ed25519.self_test()
    secret = bytes(range(32))
    public = ed25519.public_key(secret)
    text = FIRST_LINE + '\nversion = 9.9\n'
    signature = sign(text, secret)
    if not verify(text.encode(), signature, public):
        failures.append('a manifest\'s signature does not verify')
    if verify((text + ' ').encode(), signature, public):
        failures.append('a changed manifest verifies')
    if verify(text.encode(), signature, ed25519.public_key(bytes(32))):
        failures.append('another key\'s verifies')
    if ZIP_NAME.match('ReBushido-PC-0.1-abc1234.zip') is None or ZIP_NAME.match('ReBushido-PC-0.1-dev-abc1234.zip') \
            is None or ZIP_NAME.match('x.zip') is not None:
        failures.append('a package\'s name is not read')
    return failures


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--generate', action='store_true')
    parser.add_argument('--manifest', metavar='ZIP')
    parser.add_argument('--verify', metavar='MANIFEST')
    parser.add_argument('--self-test', action='store_true')
    parser.add_argument('--key-dir', default=default_key_dir())
    parser.add_argument('--notes', metavar='FILE')
    parser.add_argument('--channel', choices=['release', 'beta'], default='release')
    parser.add_argument('--out', metavar='FOLDER')
    parser.add_argument('--public', metavar='HEX')
    args = parser.parse_args()
    if args.self_test:
        failures = self_test()
        for failure in failures:
            print('FAILED: ' + failure)
        print('release_sign self-test: %s' % ('FAILED' if failures else 'OK'))
        return 1 if failures else 0
    if args.generate:
        return generate(args.key_dir)
    if args.manifest:
        notes = open(args.notes, encoding='utf-8').read().splitlines() if args.notes else []
        out = args.out or os.path.dirname(os.path.abspath(args.manifest))
        paths = write_manifest(args.manifest, out, load_secret(args.key_dir), notes, args.channel)
        print('OK: %s and %s' % paths)
        return 0
    if args.verify:
        public = bytes.fromhex(args.public) if args.public else config_public_key()
        if public is None:
            fail('no key to verify with: %s says none (--public HEX)' % CONFIG_KEY)
        ok = verify(open(args.verify, 'rb').read(), open(args.verify + '.sig', encoding='utf-8').read(), public)
        print('%s: %s' % (args.verify, 'the release key\'s signature' if ok else 'NOT the release key\'s: refused'))
        return 0 if ok else 1
    parser.print_help()
    return 2


if __name__ == '__main__':
    sys.exit(main())
