#!/usr/bin/env python3
"""A release of the PC package, made, signed and published: the owner's
(BUILD.md, "Publishing a release").

    python tools/release_publish.py --notes NOTES.txt [--beta] [--key-dir DIR]
    python tools/release_publish.py --dry-run [--trial] [--notes NOTES.txt]

It

  1. checks the tree: nothing uncommitted, config/release_public_key.txt
     set, and the key in --key-dir (by default %USERPROFILE%\\.rebushido-
     release-key, tools/release_sign.py --generate) its secret half;
  2. builds the package of a release, `make port-release RB_RELEASE=1`
     (wsl bash tools/build.sh port-release RB_RELEASE=1), into
     build/release/ReBushido-PC-<version>-<commit>.zip;
  3. writes update-manifest.txt beside it (the version, the commit, the
     zip's SHA-256 and every file's, the notes) and signs it,
     update-manifest.txt.sig (tools/release_sign.py), and checks the
     signature with the public half the program has;
  4. creates the GitHub release v<version> at the commit, with the zip,
     its .sha256, the manifest and its signature as assets and the notes
     as its text: `gh release create` (--beta: a prerelease, which only
     update_channel = beta takes). The players' programs find it at their
     next check.

--dry-run does all but step 4, and says the command it would run; with
--trial it packs a trial (no RB_RELEASE, a tree with changes allowed) and
signs with a key made for the run alone, in build/release/dry-run-key/,
never the owner's, so that the whole of it can be tried anywhere. Nothing
of build/release/ is ever committed. Tagging and publishing are the
owner's: no agent runs this without --dry-run.
"""
import argparse
import glob
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import ed25519  # noqa: E402
import release_sign  # noqa: E402

RELEASE = os.path.join(ROOT, 'build', 'release')
HEADER = os.path.join(ROOT, 'include', 'rebushido.h')
REPOSITORY = 'tgg12321/ReBushido'


def fail(message):
    sys.exit('release_publish.py: ' + message)


def git(*args):
    proc = subprocess.run(['git'] + list(args), capture_output=True, text=True, cwd=ROOT)
    if proc.returncode != 0:
        fail('git %s: %s' % (' '.join(args), proc.stderr.strip()))
    return proc.stdout.strip()


def version_number():
    match = re.search(r'^#define RB_VERSION_NUMBER "([^"]+)"', open(HEADER, encoding='utf-8').read(), re.M)
    if not match:
        fail('%s has no RB_VERSION_NUMBER' % HEADER)
    return match.group(1)


def build(release):
    command = ['wsl', 'bash', 'tools/build.sh', 'port-release'] + (['RB_RELEASE=1'] if release else [])
    if os.name != 'nt':
        command = ['make', 'port-release'] + (['RB_RELEASE=1'] if release else [])
    env = dict(os.environ)
    if not release and os.name == 'nt':
        # a trial's commit said to WSL, whose git cannot read a worktree's
        # (a release needs git itself, in the checkout: tools/port_release.py)
        dirty = subprocess.run(['git', 'diff', '--quiet', 'HEAD'], cwd=ROOT).returncode != 0
        env['RB_GIT_HASH'] = git('rev-parse', '--short', 'HEAD') + ('-dirty' if dirty else '')
        env['WSLENV'] = ':'.join(filter(None, [env.get('WSLENV', ''), 'RB_GIT_HASH/u']))
    print('release_publish: ' + ' '.join(command))
    if subprocess.run(command, cwd=ROOT, env=env).returncode != 0:
        fail('the package was not made')


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('--notes', metavar='FILE', help='the release\'s notes, a line each (120 bytes at most)')
    parser.add_argument('--beta', action='store_true', help='a prerelease, for update_channel = beta')
    parser.add_argument('--key-dir', default=release_sign.default_key_dir())
    parser.add_argument('--dry-run', action='store_true', help='everything but publishing')
    parser.add_argument('--trial', action='store_true', help='with --dry-run: a trial package and a key of its own')
    parser.add_argument('--no-build', action='store_true', help='the package built last')
    args = parser.parse_args()
    if args.trial and not args.dry_run:
        fail('--trial is for --dry-run alone')
    os.chdir(ROOT)
    notes = open(args.notes, encoding='utf-8').read().splitlines() if args.notes else []
    version = version_number()
    commit = git('rev-parse', '--short', 'HEAD')  # as tools/port_build.py names it
    if not args.trial:
        if git('status', '--porcelain'):
            fail('the tree has changes or files not committed: a release is of a commit')
        if release_sign.config_public_key() is None:
            fail('%s says none: make the key (python tools/release_sign.py --generate) and set its public half'
                 % release_sign.CONFIG_KEY)
        secret = release_sign.load_secret(args.key_dir)
        if ed25519.public_key(secret) != release_sign.config_public_key():
            fail('the key in %s is not the one in %s' % (args.key_dir, release_sign.CONFIG_KEY))
        if not args.dry_run and not notes:
            fail('a release has notes (--notes FILE): the players see them before they update')
        if not args.dry_run and shutil.which('gh') is None:
            fail('no gh (GitHub\'s command line) here')
    else:
        # a key made for the dry run alone, never the owner's
        key_dir = os.path.join(RELEASE, 'dry-run-key')
        if os.path.isdir(key_dir):
            shutil.rmtree(key_dir)
        os.makedirs(key_dir)
        secret = bytes(os.urandom(32))
        print('release_publish: --trial: signed with a key of its own (%s), not the owner\'s' % key_dir)
    if not args.no_build:
        build(release=not args.trial)
    zips = sorted(glob.glob(os.path.join(RELEASE, 'ReBushido-PC-%s-%s*.zip' % (version, commit))))
    zips = [z for z in zips if release_sign.ZIP_NAME.match(os.path.basename(z))]
    if len(zips) != 1:
        fail('no one package of %s at %s in %s (found %d)' % (version, commit, RELEASE, len(zips)))
    package = zips[0]
    if not args.trial and package.endswith('-dirty.zip'):
        fail('%s is of a tree with changes' % package)
    manifest, signature = release_sign.write_manifest(package, RELEASE, secret, notes,
                                                      'beta' if args.beta else 'release',
                                                      check_config=not args.trial)
    public = ed25519.public_key(secret)
    if not release_sign.verify(open(manifest, 'rb').read(), open(signature, encoding='utf-8').read(), public):
        fail('the manifest does not verify')
    files = sum(1 for line in open(manifest, encoding='utf-8') if line.startswith('file = '))
    print('release_publish: %s: %d files, signed (key %s...)' % (os.path.basename(manifest), files, public.hex()[:16]))
    tag = 'v' + version
    notes_file = os.path.join(RELEASE, 'release-notes.txt')
    with open(notes_file, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(notes) + '\n' if notes else 'ReBushido %s.\n' % version)
    command = ['gh', 'release', 'create', tag, package, package + '.sha256', manifest, signature,
               '--repo', REPOSITORY, '--target', git('rev-parse', 'HEAD'), '--title', 'ReBushido %s' % version,
               '--notes-file', notes_file] + (['--prerelease'] if args.beta else [])
    if args.dry_run:
        print('release_publish: --dry-run: nothing is published. It would run:')
        print('    ' + subprocess.list2cmdline(command))
        return 0
    print('release_publish: ' + subprocess.list2cmdline(command))
    if subprocess.run(command, cwd=ROOT).returncode != 0:
        fail('gh release create failed: nothing more is done')
    print('OK: %s is published; the players\' programs see it at their next check' % tag)
    return 0


if __name__ == '__main__':
    sys.exit(main())
