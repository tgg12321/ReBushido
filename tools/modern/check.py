#!/usr/bin/env python3
"""Give the game's C to the host's modern gcc and collect what it says.

    python3 tools/modern/check.py              run; write build/modern/report.txt; print the counts
    python3 tools/modern/check.py --check      the same, and exit 1 if a serious kind has a finding
                                               the baseline does not
    python3 tools/modern/check.py --update     the same, and write the baseline from the tree as it is
    python3 tools/modern/check.py --list KIND  print every finding of one kind (or of `serious`)
    python3 tools/modern/check.py --raw FILE   print the compiler's own output for one file, with the
                                               analyser's paths, and write nothing
    --no-analyzer                              leave -fanalyzer out
    --rebushido                                with ReBushido's master switch on (include/rebushido.h)
                                               and src/rebushido/*.c as well; the report is
                                               build/rebushido/modern/report.txt. The baseline is
                                               the same one, the matching source's: ReBushido's
                                               code gets no rows in it, so --update is refused.
                                               The port's switch is on too (RB_PORT, docs/PORT.md):
                                               the C beside each piece of assembly is read, with
                                               src/port/*.c, and Sony's GTE macros are C on the
                                               port's GTE (tools/port_gte_macros.py)

`make modern` and `tools/build.sh modern` run `--check`; `make
rebushido-modern` runs `--check --rebushido`. Run it in WSL: it
needs the host's gcc and nothing else (no 32-bit libc: nothing is linked and
no host header is read).

The game is built by GCC 2.7.2, which warns about little. Here the same
source (src/main/*.c and movovl/src/*.c; not Sony's library code) is
compiled by the host's gcc as 32-bit x86 (`-m32`: pointers and `int` are the
size the PlayStation's are, so the casts between them say nothing) to
assembly that is thrown away (`-S -o /dev/null`), only for the warnings.
Nothing it makes is linked or used, and the game's build never sees any of
this.

What is changed for the modern compiler, and so what it cannot see:

  - tools/modern/shim.h is included first. A function that stays assembly
    (INCLUDE_ASM) has no body here.
  - Sony's GTE macros (psxsdk/inline_c.h, inline_o.h) are MIPS coprocessor
    instructions; each becomes a call to `modern_gte("name", args...)`,
    which the compiler takes to read and write whatever its arguments point
    at. A GTE load from something never set is therefore not reported.
  - `printf` is declared with its format (Sony's `extern void printf();`
    says nothing), so -Wformat reads the game's printf calls.
  - GCC 2.7.2 lets a cast be assigned to: `*((u16 *)cursor)++`. That is
    rewritten on the line to `*(*(u16 **)&cursor)++`, which means the same.

The rewritten headers go in build/modern/include/, ahead of include/.

A finding is a warning's kind (the -W option that gave it), the file and the
function it is in. Kinds are serious or style (STYLE_KINDS below; anything
not listed there is serious). tools/modern/verdicts.tsv says what was
decided about a finding that was read: kind, file, function, a piece of the
message (or `*` for any), the verdict and the reason. The verdicts: `tool`
(the compiler is wrong; such a finding is set aside and not counted),
`original` (the original's bug or oddity, kept as it is; counted, and marked
in the report) or `ours` (how the source has to be written for the bytes;
counted and marked). The baseline is
tools/modern/baseline.tsv: kind, file, function, count; a style kind is
counted by file only (function `*`). `--check` fails when a serious kind has
more in a function than the baseline says.

The findings are one gcc's: another release's analyser finds other things.
The baseline's first line names the gcc that wrote it (`# gcc 13.3`, Ubuntu
24.04's), and a run under another says so, before the counts and again
beside anything it reports as new, so that a failure that is the compiler's
and not the change's can be told apart.
"""
import concurrent.futures
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_gte_macros  # noqa: E402  (tools/port_gte_macros.py)

OUT_DIR = 'build/modern'
REPORT = OUT_DIR + '/report.txt'
REBUSHIDO_REPORT = 'build/rebushido/modern/report.txt'
REBUSHIDO_DEFS = ['-DREBUSHIDO=1', '-DRB_PORT_BUILD=1']
BASELINE = 'tools/modern/baseline.tsv'
VERDICTS = 'tools/modern/verdicts.tsv'
SHIM = 'tools/modern/shim.h'

DEFS = ['-Dmips', '-D__mips__', '-D__mips', '-Dpsx', '-D__psx__', '-D__psx', '-D_PSYQ',
        '-D__EXTENSIONS__', '-D_MIPSEL', '-D_LANGUAGE_C', '-DLANGUAGE_C']

# As the game's compiler has it: char unsigned, no built-in functions, GNU C
# of the day. -fno-strict-aliasing: the source reads memory through any type.
BASE_FLAGS = ['-m32', '-std=gnu89', '-ffreestanding', '-fno-builtin', '-funsigned-char',
              '-fno-strict-aliasing', '-nostdinc', '-S', '-o', '/dev/null', '-x', 'c',
              '-fdiagnostics-plain-output', '-include', SHIM]

WARN_FLAGS = ['-Wall', '-Wextra', '-Wshadow', '-Wstrict-prototypes', '-Wmissing-prototypes',
              '-Wold-style-definition', '-Wformat=2', '-Wimplicit-fallthrough',
              '-Warray-bounds=2', '-Wstringop-overflow=4', '-Wlogical-op', '-Wduplicated-cond',
              '-Wduplicated-branches', '-Wnull-dereference', '-Wredundant-decls',
              '-Wnested-externs', '-Wundef', '-Wjump-misses-init', '-Wshift-overflow=2']

ANALYZER_FLAGS = ['-fanalyzer', '-Wanalyzer-too-complex']

# Kinds that say how the source is written, not that it is wrong. Counted and
# reported, never a failure. Every other kind is serious.
STYLE_KINDS = {
    'unused-parameter', 'unused-variable', 'unused-but-set-variable', 'unused-but-set-parameter',
    'unused-function', 'unused-label', 'unused-value', 'unused-const-variable',
    'sign-compare', 'pointer-sign', 'missing-prototypes', 'strict-prototypes',
    'old-style-definition', 'parentheses', 'missing-field-initializers', 'missing-braces',
    'redundant-decls', 'nested-externs', 'duplicated-branches', 'empty-body',
    'implicit-fallthrough', 'char-subscripts', 'old-style-declaration', 'jump-misses-init',
    'deprecated-non-prototype', 'cast-function-type', 'discarded-qualifiers', 'switch',
    'comment', 'undef', 'address-of-packed-member', 'override-init',
    # not about the source: where the analyser stopped looking
    'analyzer-too-complex',
}


def sources(rebushido=False):
    files = sorted(f for f in os.listdir('src/main') if f.endswith('.c'))
    out = [('src/main/' + f, ['-Iinclude']) for f in files]
    if rebushido:
        # the port's headers for Sony's GTE macros go ahead of the others
        out = [(path, ['-I' + port_gte_macros.OUT_DIR] + includes) for path, includes in out]
        for folder in ('src/rebushido', 'src/port'):
            files = sorted(f for f in os.listdir(folder) if f.endswith('.c'))
            out += [('%s/%s' % (folder, f), ['-I' + port_gte_macros.OUT_DIR, '-Iinclude']) for f in files]
    files = sorted(f for f in os.listdir('movovl/src') if f.endswith('.c'))
    out += [('movovl/src/' + f, ['-Imovovl/include', '-Iinclude']) for f in files]
    return out


# ---- the rewritten headers ----

DEFINE_RE = re.compile(r'#define\s+(\w+)\(([^)]*)\)\s*(.*)', re.S)


def rewrite_gte_header(text, name):
    """Every macro whose body is assembly becomes a call of modern_gte."""
    logical = re.sub(r'\\\n', ' ', text).split('\n')
    out = ['/* Written by tools/modern/check.py from include/psxsdk/%s. */' % name]
    count = 0
    for line in logical:
        m = DEFINE_RE.match(line)
        if not m or '__asm__' not in m.group(3):
            if '__asm__' in line:
                sys.exit('check.py: %s: assembly outside a macro it knows: %s' % (name, line[:60]))
            out.append(line)
            continue
        macro, params, body = m.group(1), m.group(2).strip(), m.group(3).lstrip()
        args = ', '.join(['"%s"' % macro] + [p.strip() for p in params.split(',') if p.strip()])
        call = 'modern_gte(%s)' % args
        if body.startswith('{'):
            call = '{ %s; }' % call
        out.append('#define %s(%s) %s' % (macro, params, call))
        count += 1
    if count == 0:
        sys.exit('check.py: %s: no GTE macro found' % name)
    return '\n'.join(out) + '\n'


PRINTF_OLD = 'extern void printf();'
PRINTF_NEW = 'extern void printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));'


def write_headers():
    inc = OUT_DIR + '/include/psxsdk'
    os.makedirs(inc, exist_ok=True)
    for name in ('inline_c.h', 'inline_o.h'):
        with open('include/psxsdk/' + name, encoding='latin-1') as f:
            text = rewrite_gte_header(f.read(), name)
        with open(inc + '/' + name, 'w', encoding='latin-1', newline='\n') as f:
            f.write(text)
    with open('include/psxsdk/libc.h', encoding='utf-8') as f:
        text = f.read()
    if PRINTF_OLD not in text:
        sys.exit('check.py: include/psxsdk/libc.h no longer has `%s`' % PRINTF_OLD)
    with open(inc + '/libc.h', 'w', encoding='utf-8', newline='\n') as f:
        f.write(text.replace(PRINTF_OLD, PRINTF_NEW))


# ---- one file ----

# `((u16 *)cursor)++`: a cast that is assigned to (GCC 2.7.2 allows it).
CAST_LVALUE_RE = re.compile(r'\(\(([A-Za-z_]\w*(?: \w+)* ?\*+)\)(\w+)\)(\+\+|--)')


def modern_source(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    text = CAST_LVALUE_RE.sub(r'(*(\1*)&\2)\3', text)
    return '# 1 "%s"\n%s' % (path, text)


def compile_one(path, includes, analyzer, plain=True, defs=()):
    """The compiler's messages for one file. Two runs: optimised, for the
    warnings that need the optimiser (-Wmaybe-uninitialized, -Warray-bounds)
    and the analyser on what the optimiser left; and, for the analyser
    alone, not optimised: the optimiser takes a variable set on one path
    only for that value everywhere, and then neither it nor the analyser
    reports the path that does not set it."""
    runs = [['-O2'] + WARN_FLAGS + (ANALYZER_FLAGS if analyzer else [])]
    if analyzer:
        runs.append(['-O0'] + ANALYZER_FLAGS)
    out = ''
    for flags in runs:
        cmd = ['gcc'] + BASE_FLAGS + flags
        port = [i for i in includes if i == '-I' + port_gte_macros.OUT_DIR]
        cmd += port + ['-I' + OUT_DIR + '/include'] + [i for i in includes if i not in port]
        cmd += DEFS + list(defs) + ['-']
        if not plain:
            cmd.remove('-fdiagnostics-plain-output')
        env = dict(os.environ, LC_ALL='C')
        proc = subprocess.run(cmd, input=modern_source(path), capture_output=True, text=True,
                              env=env, encoding='utf-8', errors='replace')
        out += proc.stderr
        if proc.returncode != 0 and ' error: ' not in proc.stderr:
            out += 'gcc: error: exit %d compiling %s\n' % (proc.returncode, path)
    return out


FUNC_RE = re.compile(r"^(?:[^ ]+: )?In function '([^']+)'")
DRIVER_ERROR_RE = re.compile(r'^(gcc|cc1): (fatal )?error: ')
DIAG_RE = re.compile(r'^([^ :]+):(\d+):(?:(\d+):)? (warning|error|fatal error): (.*)$')
KIND_RE = re.compile(r' \[-W([\w=+-]+)\]$')


def parse(path, stderr):
    """(file, line, function, kind, message) for each warning; errors apart."""
    findings, errors = [], []
    function = '-'
    for line in stderr.split('\n'):
        m = FUNC_RE.match(line)
        if m:
            function = m.group(1).split('.')[0]
            continue
        if line.endswith('At top level:'):
            function = '-'
            continue
        if DRIVER_ERROR_RE.match(line):
            errors.append(line)
            continue
        m = DIAG_RE.match(line)
        if not m:
            continue
        where, lineno, _, level, message = m.groups()
        if level != 'warning':
            errors.append(line)
            continue
        k = KIND_RE.search(message)
        if k:
            kind = k.group(1).split('=')[0]
        else:
            # a warning with no option of its own: its first words
            kind = '-'.join(re.findall(r'[a-z]+', message.lower())[:2])
        message = KIND_RE.sub('', message)
        message = re.sub(r' \[CWE-\d+\]', '', message)
        if where == '<stdin>':
            where = path
        findings.append((where, int(lineno), function, kind, message))
    return findings, errors


# ---- verdicts and baseline ----

def read_tsv(path, columns):
    rows = []
    if not os.path.exists(path):
        return rows
    with open(path, encoding='utf-8') as f:
        for line in f:
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            parts = line.split('\t')
            if len(parts) != columns:
                sys.exit('check.py: %s: a line without %d columns: %s' % (path, columns, line))
            rows.append(parts)
    return rows


def is_serious(kind):
    return kind not in STYLE_KINDS


def count_key(finding):
    where, _, function, kind, _ = finding
    return (kind, where, function if is_serious(kind) else '*')


def gcc_release():
    """The host gcc's release as the baseline names it: `13.3`."""
    full = subprocess.run(['gcc', '-dumpfullversion'], capture_output=True, text=True).stdout.strip()
    return '.'.join(full.split('.')[:2])


def baseline_release():
    """The gcc release the baseline was written under, or None if it does not say."""
    if os.path.exists(BASELINE):
        with open(BASELINE, encoding='utf-8') as f:
            match = re.match(r'# gcc (\S+)', f.readline())
            if match:
                return match.group(1)
    return None


def main():
    os.chdir(ROOT)
    args = sys.argv[1:]
    analyzer = '--no-analyzer' not in args
    rebushido = '--rebushido' in args
    defs = REBUSHIDO_DEFS if rebushido else []
    report = REBUSHIDO_REPORT if rebushido else REPORT
    if rebushido and '--update' in args:
        sys.exit('check.py: the baseline is the matching source\'s: --update without --rebushido')
    write_headers()
    if rebushido:
        port_gte_macros.write_headers()
    srcs = sources(rebushido)

    if '--raw' in args:
        want = args[args.index('--raw') + 1].replace('\\', '/')
        for path, includes in srcs:
            if path == want or os.path.basename(path) == want:
                sys.stdout.write(compile_one(path, includes, analyzer, plain=False, defs=defs))
                return 0
        sys.exit('check.py: no such file among the game\'s: ' + want)

    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        outputs = list(pool.map(lambda s: compile_one(s[0], s[1], analyzer, defs=defs), srcs))

    seen, findings, errors = set(), [], []
    for (path, _), stderr in zip(srcs, outputs):
        found, errs = parse(path, stderr)
        errors += errs
        for finding in found:
            # a header's warning comes once for every file that includes it
            if finding not in seen:
                seen.add(finding)
                findings.append(finding)
    if errors:
        print('The modern compiler could not read the source:')
        for line in errors[:40]:
            print('  ' + line)
        return 2

    verdicts = {}
    for kind, where, function, text, verdict, reason in read_tsv(VERDICTS, 6):
        if verdict not in ('tool', 'original', 'ours'):
            sys.exit('check.py: %s: verdict `%s` is not tool, original or ours' % (VERDICTS, verdict))
        verdicts.setdefault((kind, where, function), []).append((text, verdict, reason))
    used = set()

    def verdict_of(finding):
        key = (finding[3], finding[0], finding[2])
        for text, verdict, reason in verdicts.get(key, []):
            if text == '*' or text in finding[4]:
                used.add(key + (text,))
                return (verdict, reason)
        return None

    set_aside = [f for f in findings if (verdict_of(f) or ('', ''))[0] == 'tool']
    findings = [f for f in findings if (verdict_of(f) or ('', ''))[0] != 'tool']
    findings.sort(key=lambda f: (not is_serious(f[3]), f[3], f[0], f[1]))

    counts = {}
    for finding in findings:
        key = count_key(finding)
        counts[key] = counts.get(key, 0) + 1
    by_kind = {}
    for (kind, _, _), n in counts.items():
        by_kind[kind] = by_kind.get(kind, 0) + n

    baseline = {(k, w, fn): int(n) for k, w, fn, n in read_tsv(BASELINE, 4)}
    over = sorted(key for key, n in counts.items()
                  if is_serious(key[0]) and n > baseline.get(key, 0))
    style_up = {}
    for key, n in counts.items():
        if not is_serious(key[0]) and n > baseline.get(key, 0):
            style_up[key[0]] = style_up.get(key[0], 0) + n - baseline.get(key, 0)
    gone = sorted(key for key, n in baseline.items() if counts.get(key, 0) < n)

    version = subprocess.run(['gcc', '--version'], capture_output=True, text=True).stdout.split('\n')[0]
    lines = ['The game\'s C through a modern compiler (tools/modern/check.py)%s.'
             % (', with ReBushido\'s master switch on' if rebushido else ''),
             version + (', with -fanalyzer' if analyzer else ', without -fanalyzer'),
             '%d files. %d findings: %d serious, %d style; %d set aside as the tool being wrong.' % (
                 len(srcs), len(findings), sum(1 for f in findings if is_serious(f[3])),
                 sum(1 for f in findings if not is_serious(f[3])), len(set_aside)),
             '', 'By kind:']
    for kind in sorted(by_kind, key=lambda k: (not is_serious(k), -by_kind[k], k)):
        lines.append('  %-8s %5d  %s' % ('serious' if is_serious(kind) else 'style', by_kind[kind], kind))
    lines.append('')
    kind = None
    for finding in findings:
        if finding[3] != kind:
            kind = finding[3]
            lines += ['', '== %s (%s, %d) ==' % (kind, 'serious' if is_serious(kind) else 'style',
                                                 by_kind[kind])]
        v = verdict_of(finding)
        note = '   {%s: %s}' % v if v else ''
        lines.append('%s:%d: %s: %s%s' % (finding[0], finding[1], finding[2], finding[4], note))
    if set_aside:
        lines += ['', '== set aside: the tool is wrong (%s) ==' % VERDICTS]
        for finding in sorted(set_aside, key=lambda f: (f[3], f[0], f[1])):
            lines.append('%s:%d: %s: [%s] %s   {%s}' % (finding[0], finding[1], finding[2], finding[3],
                                                      finding[4], verdict_of(finding)[1]))
    os.makedirs(os.path.dirname(report), exist_ok=True)
    with open(report, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(lines) + '\n')

    if '--list' in args:
        want = args[args.index('--list') + 1]
        for finding in findings:
            if finding[3] == want or (want == 'serious' and is_serious(finding[3])):
                v = verdict_of(finding)
                print('%s:%d: %s: [%s] %s%s' % (finding[0], finding[1], finding[2], finding[3],
                                               finding[4], '   {%s: %s}' % v if v else ''))
        return 0

    print(lines[1])
    print(lines[2])
    other_gcc = None
    if '--update' not in args and baseline_release() not in (None, gcc_release()):
        other_gcc = ('this is gcc %s and %s was written under gcc %s: a finding may differ for that alone'
                     % (gcc_release(), BASELINE, baseline_release()))
        print('note: ' + other_gcc)
    for kind in sorted(by_kind, key=lambda k: (-by_kind[k], k)):
        if is_serious(kind):
            print('  serious %5d  %s' % (by_kind[kind], kind))
    style = sorted((k for k in by_kind if not is_serious(k)), key=lambda k: (-by_kind[k], k))
    print('  style: ' + ', '.join('%s %d' % (k, by_kind[k]) for k in style))
    print('Every finding: ' + report)
    for key in sorted(verdicts):
        for text, _, _ in verdicts[key]:
            if key + (text,) not in used:
                print('note: %s has a verdict for a finding that is gone: %s' % (
                    VERDICTS, ' '.join(key + (text,))))

    if '--update' in args:
        with open(BASELINE, 'w', encoding='utf-8', newline='\n') as f:
            f.write('# gcc %s\n' % gcc_release())
            f.write('# tools/modern/check.py --update writes this. kind, file, function, count;\n'
                    '# a style kind is counted by file (function *).\n')
            for key in sorted(counts, key=lambda k: (not is_serious(k[0]), k)):
                f.write('%s\t%s\t%s\t%d\n' % (key + (counts[key],)))
        print('baseline written: ' + BASELINE)
        return 0

    for kind in sorted(style_up):
        print('note: %d more of the style kind %s than the baseline' % (style_up[kind], kind))
    if gone:
        print('note: %d baseline rows have fewer findings now; `--update` to record it' % len(gone))
    if over:
        print('')
        print('NEW findings of a serious kind (not in %s):' % BASELINE)
        for key in over:
            for finding in findings:
                if count_key(finding) == key:
                    print('  %s:%d: %s: [%s] %s' % (finding[0], finding[1], finding[2], finding[3],
                                                   finding[4]))
        print('Fix it; or, if it is the original\'s or the compiler is wrong, say so in')
        print('%s and run --update.' % VERDICTS)
        if other_gcc:
            print('But first: ' + other_gcc + '.')
            print('Run it under that gcc to see whether the change or the compiler brought these.')
        if '--check' in args:
            return 1
    elif '--check' in args:
        print('OK: no new serious finding')
    return 0


if __name__ == '__main__':
    sys.exit(main())
