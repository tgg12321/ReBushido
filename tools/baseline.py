#!/usr/bin/env python3
"""The provers' and the bench's measurements of a commit of main, stored
once so that a change is held to them without running main again
(BUILD.md, "A stored baseline").

    python tools/baseline.py --store            after a verified merge into
                                                main: every prover on both
                                                GPUs, and the bench
    python tools/baseline.py --store --only gl,fps --gpu card
    python tools/baseline.py --list             what is stored

and in a prover, after a change:

    python tools/port_gl_prove.py --against main
    python3 tools/port_fps_prove.py --gpu llvmpipe --against main --quick
    python tools/port_stage_prove.py --stages 3,9 --against main

A prover's run with `--store-baseline` writes what it measured -- its
numbers, its verdicts, a hash of each trace and of each picture it wrote
-- into the main checkout's build/baseline/<commit>/<prover>-<gpu>.json,
with a hash of each file of the prover's own code (every module of
tools/ it imported but tools/playtest.py, and the play tests' plans it
read). With `--against REV` it finds the record of REV, or of the
nearest ancestor of REV that has one (and says which), before it runs
anything, and stops with 2 if the prover's code is not what measured it
or the run is asked for with other options: a stale baseline is never
trusted. After the run it prints every measure that is not what the
baseline has, plan by plan (stage by stage), or "no difference". The
times are not compared. Fewer plans or stages than the baseline's, or
--quick, compare what was run. The prover's own exit status is
unchanged by what differs.

The bench (tools/port_bench.py) keeps its numbers in build/bench/<commit>/
as before; --store runs it at the stored commit, and its `--against`
takes the nearest stored ancestor too and refuses numbers its code did
not measure.

--store needs a tree with nothing changed that is committed (the record
is the commit's), Windows' Python and the PC with the card; it builds
both provers and the window first. The bench runs alone, then the
llvmpipe provers (in WSL) beside the card's.
"""
import argparse
import datetime
import hashlib
import json
import os
import subprocess
import sys
import threading
import time

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(REPO, 'tools'))

# What --store runs: the prover's short name, its script.
PROVERS = {'gl': 'port_gl_prove', 'fps': 'port_fps_prove', 'stage': 'port_stage_prove'}
GPUS = ('llvmpipe', 'card')
# Code that is not the prover's measuring: the play tests' runner changes
# for reasons of its own many times a week, and what of it the provers use
# (a plan written from a play test's .toml) is held by the .toml's hash.
NOT_CODE = ('playtest.py',)
# How far back --against looks for a stored ancestor.
ANCESTORS = 400


def git(*args):
    try:
        return subprocess.run(['git'] + list(args), cwd=REPO, capture_output=True, text=True,
                              check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def main_checkout():
    import port_budget

    return port_budget.main_checkout()


def store_dir(commit=None):
    base = os.path.join(main_checkout(), 'build', 'baseline')
    return os.path.join(base, commit) if commit else base


def file_hash(path):
    h = hashlib.sha1()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()


def lines_hash(lines):
    h = hashlib.sha1()
    for line in lines:
        h.update(line.encode('utf-8', 'replace'))
    return h.hexdigest()


def code_hashes(extra=()):
    """The prover's own code: each module of tools/ this process imported
    (but NOT_CODE) and the files it names in `extra`, by their path from
    the tree's root."""
    tools = os.path.join(REPO, 'tools')
    files = set()
    for module in list(sys.modules.values()):
        path = getattr(module, '__file__', None)
        if not path:
            continue
        path = os.path.abspath(path)
        if os.path.dirname(path) == tools and path.endswith('.py') \
                and os.path.basename(path) not in NOT_CODE:
            files.add(path)
    for path in extra:
        files.add(os.path.abspath(os.path.join(REPO, path)))
    return {os.path.relpath(p, REPO).replace(os.sep, '/'): file_hash(p)
            for p in sorted(files) if os.path.isfile(p)}


def pictures(folder, since=0.0):
    """A hash of each PNG of `folder` written since `since` (a run's own,
    not one an older run left there)."""
    out = {}
    if not os.path.isdir(folder):
        return out
    for name in sorted(os.listdir(folder)):
        path = os.path.join(folder, name)
        if name.endswith('.png') and os.path.getmtime(path) >= since - 2:
            out[name] = file_hash(path)
    return out


def plain(value):
    """`value` as JSON gives it back: sets sorted, tuples as lists."""
    return json.loads(json.dumps(value, default=lambda o: sorted(o)))


def is_time(key):
    return 'seconds' in key or key.endswith(('_us', '_ms')) or key in ('took',)


def resolve(rev):
    return git('rev-parse', '--verify', '--quiet', rev + '^{commit}')


def nearest(rev, path_of):
    """The file `path_of(commit)` of `rev`, or of its nearest ancestor that
    has one: (path, commit, how many commits before rev), or None."""
    full = resolve(rev)
    if not full:
        return None
    listed = git('rev-list', '--max-count=%d' % ANCESTORS, full) or ''
    for n, commit in enumerate(listed.split()):
        path = path_of(commit)
        if os.path.exists(path):
            return path, commit, n
    return None


def find_record(rev, prover, gpu):
    found = nearest(rev, lambda commit: os.path.join(store_dir(commit), '%s-%s.json' % (prover, gpu)))
    if found:
        return found, None
    return None, ('nothing of %s on %s is stored for %s or its last %d ancestors (%s): '
                  'python tools/baseline.py --store, in a checkout of it'
                  % (prover, gpu, rev, ANCESTORS, store_dir()))


def describe(rev, commit, n):
    subject = git('log', '-1', '--format=%s', commit) or ''
    if n == 0:
        return '%s (%s, its own: %s)' % (rev, commit[:9], subject)
    return ('%s: the nearest stored ancestor, %s, %d commit%s before it (%s)'
            % (rev, commit[:9], n, '' if n == 1 else 's', subject))


def stale(stored, now):
    """The files of the prover's code that are not what measured the
    baseline."""
    return sorted(name for name in set(stored) | set(now) if stored.get(name) != now.get(name))


def add_options(ap, against=True):
    if against:
        ap.add_argument('--against', metavar='REV',
                        help="hold the run to the stored baseline of REV (tools/baseline.py)")
    ap.add_argument('--store-baseline', action='store_true',
                    help="keep this run as the baseline of the commit (a clean tree; tools/baseline.py)")


class Baseline:
    """A prover's side: made before the run (it checks the baseline then,
    and stops a run that cannot be compared), finished after it."""

    def __init__(self, prover, gpu, options, args, code_extra=()):
        self.prover, self.gpu = prover, gpu
        self.options = plain(options)
        self.code = code_hashes(code_extra)
        self.rev = getattr(args, 'against', None)
        self.store = args.store_baseline
        self.started = time.time()
        self.record = None
        if self.store:
            if git('status', '--porcelain', '--untracked-files=no'):
                sys.exit('%s: --store-baseline: the tree has changes not committed; '
                         'a baseline is a commit\'s' % prover)
        if self.rev:
            found, why = find_record(self.rev, prover, gpu)
            if not found:
                print('baseline: %s' % why)
                sys.exit(2)
            path, commit, n = found
            with open(path, encoding='utf-8') as f:
                self.record = json.load(f)
            self.said = describe(self.rev, commit, n)
            print('baseline: against %s' % self.said)
            changed = stale(self.record['code'], self.code)
            if changed:
                print('baseline: STALE: the prover\'s code is not what measured the baseline of %s: %s'
                      % (commit[:9], ', '.join(changed)))
                print('baseline: run the prover on both commits, or store the baseline again '
                      '(python tools/baseline.py --store, in a checkout of %s)' % commit[:9])
                sys.exit(2)
            unlike = sorted(k for k in set(self.options) | set(self.record['options'])
                            if self.options.get(k) != self.record['options'].get(k))
            if unlike:
                print('baseline: the run is asked for otherwise than the baseline was, so it cannot '
                      'be held to it: %s' % ', '.join('%s %s (baseline %s)'
                                                     % (k, self.options.get(k),
                                                        self.record['options'].get(k))
                                                     for k in unlike))
                sys.exit(2)

    def finish(self, units):
        """`units`: plan (or stage) -> what was measured of it, with
        'pictures' (name -> hash) and 'trace' (a hash) where it has them."""
        units = plain(units)
        if self.store:
            commit = git('rev-parse', 'HEAD')
            folder = store_dir(commit)
            os.makedirs(folder, exist_ok=True)
            path = os.path.join(folder, '%s-%s.json' % (self.prover, self.gpu))
            with open(path, 'w', encoding='utf-8', newline='\n') as f:
                json.dump({'prover': self.prover, 'gpu': self.gpu, 'commit': commit,
                           'stored': datetime.datetime.now().isoformat(timespec='seconds'),
                           'seconds': round(time.time() - self.started),
                           'options': self.options, 'code': self.code, 'units': units},
                          f, indent=1, sort_keys=True)
            print('baseline: stored %s' % path)
        if self.record is not None:
            return compare(self.record, units, self.said, round(time.time() - self.started))
        return None


def flatten(value, prefix=''):
    if isinstance(value, dict):
        out = {}
        for key, item in value.items():
            out.update(flatten(item, '%s.%s' % (prefix, key) if prefix else key))
        return out
    return {prefix: value}


def compare(record, units, said, seconds, most=25):
    """Prints what differs; returns how many measures do."""
    theirs = record['units']
    differences = 0
    compared = 0
    for name, mine in units.items():
        if name not in theirs:
            print('baseline: %s: not in the baseline' % name)
            differences += 1
            continue
        compared += 1
        was = dict(theirs[name])
        now = dict(mine)
        lines = []
        was_pictures = was.pop('pictures', None)
        now_pictures = now.pop('pictures', None)
        if was_pictures and now_pictures:
            both = sorted(set(was_pictures) & set(now_pictures))
            other = [p for p in both if was_pictures[p] != now_pictures[p]]
            only = sorted(set(was_pictures) ^ set(now_pictures))
            if other or only:
                lines.append('pictures: %d of %d differ%s%s'
                             % (len(other), len(both), ' (%s)' % ', '.join(other[:6]) if other else '',
                                '; in one run only: %s' % ', '.join(only[:6]) if only else ''))
        a, b = flatten(was), flatten(now)
        for key in sorted(set(a) | set(b)):
            if is_time(key.split('.')[-1]):
                continue
            if key not in a or key not in b:
                # a measure one side has not: something the prover measures
                # that the other did not print
                lines.append('%s: %s -> %s' % (key, short(a.get(key, '(none)')), short(b.get(key, '(none)'))))
            elif a[key] != b[key]:
                lines.append('%s: %s -> %s' % (key, short(a[key]), short(b[key])))
        if lines:
            differences += len(lines)
            print('baseline: %s: %d differ%s' % (name, len(lines), 's' if len(lines) == 1 else ''))
            for line in lines[:most]:
                print('    %s' % line)
            if len(lines) > most:
                print('    and %d more' % (len(lines) - most))
    skipped = len(set(theirs) - set(units))
    print('baseline: %s against %s: %d of the baseline\'s %d compared%s; %d s against its %d s'
          % ('no difference' if differences == 0 else '%d DIFFERENCES' % differences, said,
             compared, len(theirs), ' (%d not run)' % skipped if skipped else '',
             seconds, record.get('seconds', 0)))
    return differences


def short(value):
    text = json.dumps(value)
    return text if len(text) <= 80 else text[:77] + '...'


# --store

def run_lane(name, commands, log_dir, results):
    for label, cmd, env in commands:
        log = os.path.join(log_dir, label + '.log')
        started = time.time()
        print('baseline: %s: %s' % (name, label), flush=True)
        with open(log, 'w', encoding='utf-8', errors='replace', newline='\n') as f:
            done = subprocess.run(cmd, cwd=REPO, stdout=f, stderr=subprocess.STDOUT, env=env)
        results.append((label, done.returncode, time.time() - started, log))
        print('baseline: %s: %s ended %d in %.0f s' % (name, label, done.returncode, time.time() - started),
              flush=True)


def store(args):
    if os.name != 'nt':
        print('baseline: --store runs from Windows\' Python, on the PC with the card')
        return 1
    if git('status', '--porcelain', '--untracked-files=no'):
        print('baseline: the tree has changes not committed; a baseline is a commit\'s')
        return 1
    commit = git('rev-parse', 'HEAD')
    only = args.only.split(',')
    gpus = args.gpu.split(',')
    folder = store_dir(commit)
    os.makedirs(folder, exist_ok=True)
    print('baseline: storing %s (%s) in %s' % (commit[:9], git('log', '-1', '--format=%s'), folder),
          flush=True)
    started = time.time()
    builds = []
    if 'llvmpipe' in gpus and set(only) & set(PROVERS):
        builds.append(('build-port-gl', ['wsl', 'python3', 'tools/port_gl_build.py']))
    if 'card' in gpus and set(only) & set(PROVERS):
        builds.append(('build-port-gl-windows', ['wsl', 'bash', 'tools/build.sh', 'port-gl-windows']))
    if 'bench' in only:
        builds.append(('build-port-sdl-windows', ['wsl', 'bash', 'tools/build.sh', 'port-sdl-windows']))
    results = []
    for label, cmd in builds:
        run_lane('build', [(label, cmd, None)], folder, results)
        if results[-1][1] != 0:
            print('baseline: %s failed: %s' % (label, results[-1][3]))
            return 1
    if 'bench' in only:
        # alone: it measures time, and two runs side by side share the card
        run_lane('bench', [('port_bench', [sys.executable, 'tools/port_bench.py'], None)], folder, results)
    quiet = dict(os.environ, SDL_AUDIODRIVER='dummy')
    lanes = {}
    for short_name in [p for p in PROVERS if p in only]:
        script = 'tools/%s.py' % PROVERS[short_name]
        if 'llvmpipe' in gpus:
            lanes.setdefault('llvmpipe', []).append(
                ('%s-llvmpipe' % PROVERS[short_name],
                 ['wsl', 'python3', script, '--gpu', 'llvmpipe', '--store-baseline'], None))
        if 'card' in gpus:
            lanes.setdefault('card', []).append(
                ('%s-card' % PROVERS[short_name],
                 [sys.executable, script, '--gpu', 'card', '--store-baseline'], quiet))
    for cmds in lanes.values():
        for label, _, _ in cmds:
            old = os.path.join(folder, label + '.json')
            if os.path.exists(old):
                os.remove(old)
    threads = [threading.Thread(target=run_lane, args=(name, cmds, folder, results))
               for name, cmds in lanes.items()]
    for t in threads:
        t.start()
    for t in threads:
        t.join()
    print()
    failed = False
    provers = set(label for cmds in lanes.values() for label, _, _ in cmds)
    for label, code, seconds, log in results:
        # a prover that fails its own verdict still stores what it measured
        # (the baseline is what main does, not what it should); one that
        # stored nothing failed
        stored = label not in provers or os.path.exists(os.path.join(folder, label + '.json'))
        print('  %-28s %3d %6.0f s%s' % (label, code, seconds, '' if stored else '  NOT STORED: ' + log))
        failed = failed or not stored
    print('baseline: %s in %.0f min: %s' % ('FAILED' if failed else 'stored', (time.time() - started) / 60,
                                            folder))
    return 1 if failed else 0


def list_stored():
    base = store_dir()
    if not os.path.isdir(base):
        print('baseline: nothing stored (%s)' % base)
        return 0
    for commit in sorted(os.listdir(base), key=lambda c: os.path.getmtime(os.path.join(base, c))):
        names = sorted(n[:-5] for n in os.listdir(os.path.join(base, commit)) if n.endswith('.json'))
        print('%s %s  %s' % (commit[:9], (git('log', '-1', '--format=%s', commit) or '')[:50],
                             ' '.join(names)))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--store', action='store_true', help="measure HEAD and keep it as its baseline")
    ap.add_argument('--only', default='gl,fps,stage,bench', help='of gl, fps, stage, bench')
    ap.add_argument('--gpu', default='llvmpipe,card', help='of llvmpipe, card')
    ap.add_argument('--list', action='store_true', help='what is stored')
    args = ap.parse_args()
    if args.store:
        return store(args)
    return list_stored()


if __name__ == '__main__':
    sys.exit(main())
