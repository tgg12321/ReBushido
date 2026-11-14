#!/usr/bin/env python3
"""Count what docs/STATUS.md reports, the same way every time.

    python tools/count.py            every count, as text
    python tools/count.py --notes    each FAKE note with its kind
    python tools/count.py --check    exit 1 if the source and docs/FAKES.md disagree

It reads the tree it stands in (copy it, with casts.py, into another
checkout's tools/ to count that one). It needs no build. How each thing is counted:

  areas     game:    src/main/*.c and include/*.h
            SDK:     src/main/psxsdk/ and include/psxsdk/
            overlay: movovl/src, movovl/include, movovl/sdk
  ReBushido Everything above is counted as the matching build compiles it:
            what is inside `#if RB_NAME` (or `#if REBUSHIDO`) is left out,
            and its `#else` kept, as with every switch of
            include/rebushido.h off. ReBushido's own files
            (src/rebushido/*.c, include/rebushido.h, include/rb_*.h) are in
            no area; they and the `#if` blocks are counted apart, at the
            end.
  FAKE      every `FAKE:` in a comment. Its kind is the row of docs/FAKES.md
            that names it ("Kind A, left", "Kind B, left", "Kind C, left",
            "Kept, ours" for the ones on a goto, "A listing"): the file and
            the first words of the note have to agree. A note with no row, or
            a row with no note, is reported.
  goto      `goto` statements, comments and strings taken out. The verdicts
            are the "Kept" and "Sony's" tables of docs/FAKES.md, "The `goto`s",
            checked file by file against the source.
  assembly  INCLUDE_ASM lines (a listing with no `glabel` is data), jumps
            into the BIOS written with the macros of include/bios.h,
            file-scope __asm__ blocks that are a whole function, and the
            __asm__ lines that only name a label of a listing.
  functions the lines of config/symbol_addrs.txt marked `type:func`, which
            tools/symbol_addrs.py writes from the linked ELF (a label
            inside a listing, `D_<address>`, is not a function).
  casts     tools/casts.py.
  bb2.ld    from main_SBSS_START on: lines that place one variable
            (`.gnu.linkonce.b.<name>`), `. += n` and `ALIGN` lines, the last
            apart for the files' .sbss list and for the lists of variables.
  files     src/main/*.c: assembly if it has an INCLUDE_ASM, data if it
            defines no function, code otherwise.
"""
import collections
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, '..'))
os.chdir(ROOT)


def files(*patterns):
    out = []
    for p in patterns:
        out += glob.glob(p, recursive=True)
    return sorted(f.replace('\\', '/') for f in out if '/build/' not in f.replace('\\', '/'))


# ReBushido's own files, the native port's among them (src/port/,
# include/port_*.h: docs/PORT.md): in no area, counted apart.
REBUSHIDO_FILES = files('src/rebushido/*.c', 'src/port/*.c', 'include/rebushido.h', 'include/rb_*.h',
                        'include/port_*.h')
GAME_HEADERS = [f for f in files('include/*.h') if f not in REBUSHIDO_FILES]

AREAS = collections.OrderedDict([
    ('game', files('src/main/*.c') + GAME_HEADERS),
    ('SDK', files('src/main/psxsdk/**/*.c', 'src/main/psxsdk/**/*.h', 'include/psxsdk/**/*.h')),
    ('overlay', files('movovl/src/*.c', 'movovl/include/*.h', 'movovl/sdk/**/*.c', 'movovl/sdk/**/*.h')),
])
# the overlay's own code, apart from the Sony modules only it has
OVERLAY_OWN = files('movovl/src/*.c', 'movovl/include/*.h')


def read_raw(f):
    return open(f, encoding='utf-8', errors='replace').read()


def read(f):
    """A file's text; C as the matching build compiles it (rb_matching)."""
    src = read_raw(f)
    return rb_matching(src) if f.endswith(('.c', '.h')) else src


# ------------------------------------------------- ReBushido's `#if` blocks

RB_NAME = r'\b(?:REBUSHIDO|RB_[A-Z0-9_]+)\b'
RB_EXPRESSION = re.compile(r'(?:\s|!|&&|\|\||\(|\)|\b[01]\b|%s)+$' % RB_NAME)
DIRECTIVE = re.compile(r'[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b(.*)')


def rb_value(expression):
    """What an `#if` of ReBushido's switches comes to with every switch off:
    True or False; None if it is not made of those switches alone."""
    expression = re.sub(r'/\*.*?\*/|//.*', '', expression).strip()
    if not re.search(RB_NAME, expression) or not RB_EXPRESSION.match(expression):
        return None
    python = re.sub(RB_NAME, '0', expression).replace('&&', ' and ').replace('||', ' or ').replace('!', ' not ')
    try:
        return bool(eval(python, {}))
    except SyntaxError:
        return None


def rb_lines(src):
    """For each line of a C file: 'directive' if it is the #if, #elif, #else
    or #endif of a block that asks ReBushido's switches, 'off' if the
    matching build does not compile it, else 'on'."""
    out = []
    stack = []  # [is ReBushido's, this branch compiled, a branch was compiled]
    for line in src.split('\n'):
        m = DIRECTIVE.match(line)
        kind = 'on' if all(live for rb, live, _ in stack if rb) else 'off'
        if m:
            word, rest = m.group(1), m.group(2)
            if word in ('if', 'ifdef', 'ifndef'):
                value = rb_value(rest) if word == 'if' else None
                stack.append([value is not None, bool(value), bool(value)])
                if value is not None:
                    kind = 'directive'
            elif stack and stack[-1][0]:
                kind = 'directive'
                if word == 'endif':
                    stack.pop()
                else:
                    top = stack[-1]
                    top[1] = not top[2] and (word == 'else' or bool(rb_value(rest)))
                    top[2] = top[2] or top[1]
            elif stack and word == 'endif':
                stack.pop()
        out.append(kind)
    return out


def rb_blank(src, kinds):
    """The text with the lines of those kinds made empty, the others kept."""
    return '\n'.join('' if kind in kinds else line
                     for line, kind in zip(src.split('\n'), rb_lines(src)))


def rb_matching(src):
    """A C file as the matching build compiles it."""
    return rb_blank(src, ('directive', 'off'))


def rb_blocks(f):
    """How many blocks of a file ask ReBushido's switches."""
    src = read_raw(f)
    return sum(1 for line, kind in zip(src.split('\n'), rb_lines(src))
               if kind == 'directive' and re.match(r'[ \t]*#[ \t]*if\b', line))


def strip(src):
    """Comments and string/char literals blanked, newlines kept."""
    def rep(m):
        s = m.group(0)
        if s.startswith('/'):
            return re.sub(r'[^\n]', ' ', s)
        return s[0] + ' ' * (len(s) - 2) + s[0]
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', rep, src, flags=re.S)


def norm(s):
    return re.sub(r'\s+', ' ', s.replace('`', '').replace('\\|', '|')).strip()


def base(f):
    """The path as docs/FAKES.md writes it: from src/main/ on."""
    return f.replace('src/main/', '')


# ---------------------------------------------------------------- FAKE notes

def fake_notes():
    """[(area, file, line, text)] for every FAKE: in a comment."""
    out = []
    for area, fs in AREAS.items():
        for f in fs:
            src = read(f)
            for m in re.finditer(r'/\*.*?\*/|//[^\n]*', src, flags=re.S):
                c = m.group(0)
                for k in re.finditer(r'FAKE:', c):
                    text = c[k.end():]
                    text = re.sub(r'\*/$', '', text)
                    text = re.sub(r'\n\s*\*', ' ', text)
                    line = src.count('\n', 0, m.start() + k.start()) + 1
                    out.append((area, f, line, norm(text)))
    return out


def md_sections(md):
    """heading -> text, for the ## and ### headings of a markdown file."""
    out = collections.OrderedDict()
    cur = None
    for line in md.split('\n'):
        m = re.match(r'#{2,3} (.*)', line)
        if m:
            cur = m.group(1).strip()
            out[cur] = []
        elif cur is not None:
            out[cur].append(line)
    return out


def md_rows(lines):
    rows = []
    for line in lines:
        if line.startswith('|') and not re.match(r'\|\s*-', line):
            cells = [c.strip() for c in re.split(r'(?<!\\)\|', line.strip().strip('|'))]
            rows.append(cells)
    return rows[1:]  # the header out


def fakes_md_rows():
    """[(kind, file, note)] the rows of docs/FAKES.md that stand for a note left in the source."""
    if not os.path.exists('docs/FAKES.md'):
        return None
    sec = md_sections(read('docs/FAKES.md'))
    out = []
    for head, lines in sec.items():
        kind = None
        if head.startswith('Kind A, left'):
            kind = 'A'
        elif head.startswith('Kind B, left'):
            kind = 'B'
        elif head.startswith('Kind C, left'):
            kind = 'C'
        if kind:
            for r in md_rows(lines):
                out.append((kind, r[0].strip('`'), norm(r[2])))
        elif head.startswith('Kept, ours'):
            for r in md_rows(lines):
                out.append(('goto', r[0].split('`')[1], norm(r[2])))
        elif head.startswith('A listing'):
            m = re.search(r'`([^`]+)`', '\n'.join(lines))
            if m:
                out.append(('listing', m.group(1), ''))
    return out


def same_note(a, b):
    """The first words agree (a row may shorten the note, or the note may run on)."""
    a, b = a.rstrip('.'), b.rstrip('.')
    n = min(len(a), len(b))
    return n >= 1 and a[:n] == b[:n]


def sort_fakes(notes, rows):
    """-> [(note, kind or None)], the rows no note used."""
    left = list(rows)
    out = []
    for note in notes:
        area, f, line, text = note
        kind = None
        # the longest agreeing row first, so that "as tmp1" does not take "as tmp1 ..."
        cands = [r for r in left if r[1] == base(f) and (r[0] == 'listing' or same_note(r[2], text))]
        cands.sort(key=lambda r: -len(r[2]))
        if cands:
            kind = cands[0][0]
            left.remove(cands[0])
        out.append((note, kind))
    return out, left


# ---------------------------------------------------------------------- goto

def gotos():
    """{area: {file: n}}"""
    out = collections.OrderedDict()
    for area, fs in AREAS.items():
        out[area] = collections.OrderedDict()
        for f in fs:
            n = len(re.findall(r'\bgoto\b', strip(read(f))))
            if n:
                out[area][f] = n
    return out


GOTO_VERDICTS = [
    ('Kept: a jump the authors could well have written', "the authors', a jump one would write"),
    ('Kept: the bytes have one copy and a jump', "probably the authors': one copy in the bytes"),
    ('Kept, ours', 'ours: no other form matches'),
    ("Sony's", "Sony's"),
]


def goto_md():
    """{verdict: {file: n}} from docs/FAKES.md, or None."""
    if not os.path.exists('docs/FAKES.md'):
        return None
    sec = md_sections(read('docs/FAKES.md'))
    out = collections.OrderedDict()
    for head, verdict in GOTO_VERDICTS:
        per = collections.Counter()
        for h, lines in sec.items():
            if h.startswith(head):
                for r in md_rows(lines):
                    m = re.match(r'\d+', r[1])
                    if m:
                        per[r[0].split('`')[1]] += int(m.group(0))
        out[verdict] = per
    return out


# ------------------------------------------------------------------ assembly

def assembly():
    out = collections.OrderedDict()
    for area, fs in AREAS.items():
        inc, data, bios, blocks, labels = [], [], 0, 0, 0
        for f in fs:
            if not f.endswith('.c'):
                continue
            s = strip(read(f))
            for m in re.finditer(r'^\s*INCLUDE_ASM\(\s*[^,]*,\s*(\w+)\s*\)', s, flags=re.M):
                name = m.group(1)
                cands = ['asm/funcs/%s.s' % name, 'movovl/asm/funcs/%s.s' % name]
                lst = next((c for c in cands if os.path.exists(c)), None)
                if lst and 'glabel' not in read(lst):
                    data.append((f, name))
                else:
                    inc.append((f, name))
            bios += len(re.findall(r'^\s*BIOS_[ABC]_FUNCTION\(', s, flags=re.M))
            blocks += len(re.findall(r'^__asm__\(', s, flags=re.M))
            labels += len(re.findall(r'^extern\b[^;\n]*__asm__\(', s, flags=re.M))
        out[area] = dict(inc=inc, data=data, bios=bios, blocks=blocks, labels=labels)
    return out


def listings():
    def n(pat):
        fs = files(pat)
        return len(fs), sum(1 for f in fs if 'glabel' in read(f))
    return collections.OrderedDict([
        ('asm/funcs', n('asm/funcs/*.s')),
        ('movovl/asm/funcs', n('movovl/asm/funcs/*.s')),
    ])


# -------------------------------------------------------------- linker script

def ld_counts(path, start):
    """Counts from the line holding `start` on. An ALIGN line ahead of the first
    variable's line is in the list of the files' own .sbss."""
    if not os.path.exists(path):
        return None
    lines = read(path).split('\n')
    i0 = next((i for i, l in enumerate(lines) if start in l), None)
    if i0 is None:
        return None
    c = collections.Counter()
    for line in lines[i0:]:
        s = strip(line).strip()
        if re.search(r'\.gnu\.linkonce\.b\.', s):
            c['var'] += 1
        elif re.match(r'\.\s*\+=', s):
            c['gap'] += 1
            c['gap_bytes'] += int(re.search(r'\+=\s*(\w+)', s).group(1), 0)
        elif re.match(r'\.\s*=\s*ALIGN', s):
            c['align_vars' if c['var'] else 'align_sbss'] += 1
        elif re.search(r'\.o\(\.s?bss\)', s):
            c['file_bss'] += 1
    return c


# --------------------------------------------------------------------- files

FUNC_DEF = re.compile(
    r'^(?!typedef|extern|struct|union|enum|#|\s)([A-Za-z_][\w \t\*]*?)\b(\w+)\s*\(((?:[^;{}()]|\([^;{}()]*\))*)\)'
    r'((?:\s*[A-Za-z_][\w \t\*,\[\]]*;)*)\s*\{', re.M)


def functions_defined(f):
    """[(name, is_static, is_inline, is_old_style)] the function definitions of a C file or header.
    Old style is `f(a, b) int a; int b; {`."""
    s = strip(read(f))
    out = []
    for m in FUNC_DEF.finditer(s):
        head = m.group(1)
        if m.group(2) in ('if', 'while', 'for', 'switch', 'return'):
            continue
        out.append((m.group(2), bool(re.search(r'\bstatic\b', head)), bool(re.search(r'\binline\b', head)),
                    bool(m.group(4).strip())))
    return out


def game_files():
    kinds = collections.OrderedDict([('code', []), ('assembly', []), ('data', [])])
    for f in files('src/main/*.c'):
        s = strip(read(f))
        if re.search(r'^\s*INCLUDE_ASM\(', s, flags=re.M):
            kinds['assembly'].append(f)
        elif functions_defined(f):
            kinds['code'].append(f)
        else:
            kinds['data'].append(f)
    return kinds


# ------------------------------------------------------------- small counts

def count_re(fs, pattern, flags=0):
    return sum(len(re.findall(pattern, strip(read(f)), flags)) for f in fs)


def names_re(fs, pattern):
    c = collections.Counter()
    for f in fs:
        c.update(re.findall(pattern, strip(read(f))))
    return c


def unused_locals(fs):
    out = []
    for f in fs:
        s = strip(read(f))
        for m in re.finditer(r'^[ \t]+[\w \t\*]+\b(unused_?\d*)\s*(\[[^\]]*\])*\s*;', s, flags=re.M):
            out.append((f, s.count('\n', 0, m.start()) + 1, m.group(0).strip()))
    return out


SYMBOL_TABLE = 'config/symbol_addrs.txt'


def table_functions():
    """The names of the main executable's functions, from the symbol table."""
    names = re.findall(r'^(\w+) = 0x[0-9A-F]{8}; // type:func$', read(SYMBOL_TABLE), flags=re.M)
    return set(n for n in names if not re.match(r'D_[0-9A-F]{8}$', n))


def casts():
    p = os.path.join(HERE, 'casts.py')
    if not os.path.exists(p):
        return None
    r = subprocess.run([sys.executable, p], capture_output=True, text=True)
    return r.stdout.rstrip() if r.returncode == 0 else None


# ---------------------------------------------------------------------- main

def main():
    args = sys.argv[1:]
    problems = []
    notes = fake_notes()
    rows = fakes_md_rows()
    sorted_notes, unused_rows = sort_fakes(notes, rows or [])

    if '--notes' in args:
        for (area, f, line, text), kind in sorted_notes:
            print('%-8s %-8s %s:%d: %s' % (kind or '?', area, f, line, text[:110]))
        return 0

    def title(t):
        print('\n== %s' % t)

    title('FAKE notes')
    by_area = collections.Counter(n[0][0] for n in sorted_notes)
    print('all %d: %s' % (len(notes), ', '.join('%s %d' % (a, by_area[a]) for a in AREAS)))
    if rows is not None:
        kinds = ['A', 'B', 'C', 'goto', 'listing', None]
        label = {'A': 'A  says something untrue', 'B': 'B  only steers the compiler',
                 'C': 'C  control flow shaped to match', 'goto': 'on a goto that is ours',
                 'listing': 'a listing that would not match', None: 'no row in docs/FAKES.md'}
        print('%-34s %5s %5s %8s %5s' % ('kind', 'game', 'SDK', 'overlay', 'all'))
        for k in kinds:
            c = collections.Counter(n[0][0] for n in sorted_notes if n[1] == k)
            if k is None and not sum(c.values()):
                continue
            print('%-34s %5d %5d %8d %5d' % (label[k], c['game'], c['SDK'], c['overlay'], sum(c.values())))
        for (area, f, line, text), kind in sorted_notes:
            if kind is None:
                problems.append('FAKE note with no row in docs/FAKES.md: %s:%d: %s' % (f, line, text[:80]))
        for kind, f, text in unused_rows:
            problems.append('docs/FAKES.md row with no note in the source: kind %s, %s: %s' % (kind, f, text[:80]))
        per_file = collections.Counter(base(n[0][1]) for n in sorted_notes)
        print('most in one file: ' + ', '.join('%s %d' % x for x in per_file.most_common(6)))

    title('goto statements')
    g = gotos()
    print(', '.join('%s %d' % (a, sum(g[a].values())) for a in AREAS))
    gm = goto_md()
    if gm is not None:
        src = collections.Counter()
        for a in g:
            for f, n in g[a].items():
                src[base(f)] += n
        doc = collections.Counter()
        for verdict, per in gm.items():
            print('  %-46s %3d' % (verdict, sum(per.values())))
            doc.update(per)
        for f in sorted(set(src) | set(doc)):
            if src[f] != doc[f]:
                problems.append('goto: %s has %d in the source, %d in docs/FAKES.md' % (f, src[f], doc[f]))

    title('Assembly')
    asm = assembly()
    print('%-52s %5s %5s %8s' % ('', 'game', 'SDK', 'overlay'))
    for key, what in (('inc', 'INCLUDE_ASM, a function'), ('data', 'INCLUDE_ASM, data (no glabel)'),
                      ('bios', 'BIOS jumps by the macro of bios.h'),
                      ('blocks', 'whole-function __asm__ blocks'),
                      ('labels', '__asm__ lines that name a label of a listing')):
        v = [asm[a][key] for a in AREAS]
        v = [len(x) if isinstance(x, list) else x for x in v]
        print('%-52s %5d %5d %8d' % (what, v[0], v[1], v[2]))
    for a in AREAS:
        per = collections.Counter(f for f, _ in asm[a]['inc'] + asm[a]['data'])
        if a == 'game' and per:
            print('  game, by file: ' + ', '.join('%s %d' % (base(f), n) for f, n in sorted(per.items())))
        if a == 'SDK' and per:
            lib = collections.Counter(f.split('/')[3] for f in per.elements())
            print('  SDK, by library: ' + ', '.join('%s %d' % x for x in sorted(lib.items())) +
                  ' (in %d modules)' % len(per))
    for h in ('include/psxsdk/inline_c.h', 'include/psxsdk/inline_o.h'):
        if os.path.exists(h):
            print('  __asm__ lines in %s: %d' % (h, len(re.findall(r'__asm__', read(h)))))
    print('overlay assembly files: ' + ', '.join(files('movovl/asm/*.s', 'movovl/asm/data/*.s', 'movovl/asm/funcs/*.s')))
    title('Listings and functions')
    ls = listings()
    for d, (n, withfn) in ls.items():
        print('%-18s %5d listings, %d of them a function' % (d, n, withfn))
    in_table = table_functions()
    total = len(in_table)
    in_asm = len(asm['game']['inc']) + len(asm['SDK']['inc']) + asm['game']['bios'] + asm['SDK']['bios'] + \
        asm['game']['blocks'] + asm['SDK']['blocks']
    print('functions of the main executable: %d; in assembly %d (listings %d, BIOS jumps %d, __asm__ blocks %d); '
          'in C %d' % (total, in_asm, len(asm['game']['inc']) + len(asm['SDK']['inc']),
                       asm['game']['bios'] + asm['SDK']['bios'], asm['game']['blocks'] + asm['SDK']['blocks'],
                       total - in_asm))
    built = set(n for a in ('game', 'SDK') for _, n in asm[a]['inc'] + asm[a]['data'])
    on_disk = set(os.path.basename(f)[:-2] for f in files('asm/funcs/*.s'))
    if built != on_disk:
        problems.append('asm/funcs and the INCLUDE_ASM lines disagree: %s' % sorted(built ^ on_disk)[:8])

    title('Casts (tools/casts.py)')
    print(casts() or 'casts.py not here')

    title('Linker scripts: the zeroed variables')
    for path, start in (('bb2.ld', 'main_SBSS_START = .'), ('movovl/movovl.ld', '.gnu.linkonce.b.')):
        c = ld_counts(path, start)
        if c is None:
            print('%s: not counted' % path)
            continue
        print('%-18s %4d lines place one variable; %d `. += n` (0x%X bytes); ALIGN: %d in the files\' .sbss '
              'list, %d among the variables; %d files\' own .sbss/.bss lines'
              % (path, c['var'], c['gap'], c['gap_bytes'], c['align_sbss'], c['align_vars'], c['file_bss']))

    title('Files')
    gf = game_files()
    print('src/main/*.c: %d (code %d, assembly %d, data %d)' % (
        sum(len(v) for v in gf.values()), len(gf['code']), len(gf['assembly']), len(gf['data'])))
    print('  data: ' + ', '.join(os.path.basename(f) for f in gf['data']))
    print('include/*.h: %d' % len(GAME_HEADERS))
    sdk = files('src/main/psxsdk/*/*.c')
    lib = collections.Counter(f.split('/')[3] for f in sdk)
    print('src/main/psxsdk: %d modules in %d libraries (%s)' % (
        len(sdk), len(lib), ', '.join('%s %d' % x for x in sorted(lib.items()))))
    print('movovl: %d C files of its own, %d C modules in sdk/, %d assembly files' % (
        len(files('movovl/src/*.c')), len(files('movovl/sdk/*/*.c')),
        len(files('movovl/asm/*.s', 'movovl/asm/funcs/*.s', 'movovl/asm/data/*.s'))))
    odd = [f for f in files('src/main/*.c') if re.match(r'(d_)?[0-9A-F]{4,}\.c$', os.path.basename(f))]
    print('offset-named files: %d' % len(odd))

    title('Functions defined in C (found by a pattern; the totals above are by the symbol table)')
    ref = in_table - on_disk
    for area, fs in (('game', files('src/main/*.c') + GAME_HEADERS), ('SDK', files('src/main/psxsdk/*/*.c'))):
        defs = [(f, d) for f in fs for d in functions_defined(f)]
        listed = [d for _, d in defs if d[0] in ref]
        print('%-5s %4d in the symbol table (%d `static`, %d defined in the old style, `f(a) int a; {`); '
              '%d more that are `inline` and have none' % (
                  area, len(listed), sum(1 for d in listed if d[1]), sum(1 for d in listed if d[3]),
                  sum(1 for _, d in defs if d[0] not in ref and d[2])))
        other = [d[0] for _, d in defs if d[0] not in ref and not d[2]]
        if other:
            print('      neither: ' + ', '.join(other))
        if area == 'game':
            print('      names over 45 characters: %d; func_8xxxxxxx: %d' % (
                sum(1 for _, d in defs if len(d[0]) > 45), sum(1 for _, d in defs if re.match(r'func_8', d[0]))))

    title('Names and members not understood')
    for area, fs in (('game', AREAS['game']), ('SDK', AREAS['SDK']), ('overlay', AREAS['overlay'])):
        d = names_re(fs, r'\bD_[0-9A-Fa-f]{8}\b')
        print('%-8s D_ names %3d (%d uses); members unkNN: %3d declared, %3d uses with . or ->; '
              'members unk_NN: %3d declared, %3d uses; Unk types %d' % (
                  area, len(d), sum(d.values()),
                  count_re(fs, r'\bunk[0-9A-Fa-f]+\b(?:\[[^\]]*\])*\s*;'),
                  count_re(fs, r'(?:->|\.)\s*unk[0-9A-Fa-f]+\b'),
                  count_re(fs, r'\bunk_[0-9A-Fa-f]+\b(?:\[[^\]]*\])*\s*;'),
                  count_re(fs, r'(?:->|\.)\s*unk_[0-9A-Fa-f]+\b'),
                  len(names_re(fs, r'\bUnk\w*'))))
    un = unused_locals([f for a in AREAS for f in AREAS[a] if f.endswith('.c')])
    print('locals named `unused` in .c files: %d in %d files' % (len(un), len(set(u[0] for u in un))))
    for f, line, text in un:
        print('  %s:%d: %s' % (base(f), line, text))

    title('Other counts')
    gc = [f for f in AREAS['game'] if f.endswith('.c')]
    sc = [f for f in AREAS['SDK'] if f.endswith('.c')]
    print('extern lines in .c files: game %d, SDK %d' % (count_re(gc, r'^extern\b', re.M), count_re(sc, r'^extern\b', re.M)))
    print('hex literals: game %d, SDK %d' % (count_re(AREAS['game'], r'\b0[xX][0-9A-Fa-f]+'),
                                             count_re(AREAS['SDK'], r'\b0[xX][0-9A-Fa-f]+')))
    print('volatile: game %d, SDK .c files %d, overlay %d' % (
        count_re(AREAS['game'], r'\bvolatile\b'), count_re(sc, r'\bvolatile\b'),
        count_re(AREAS['overlay'], r'\bvolatile\b')))
    print('unions: game %d' % count_re(AREAS['game'], r'\bunion\b'))

    title('ReBushido (in none of the counts above)')
    print('its own files: %d (%s)' % (len(REBUSHIDO_FILES), ', '.join(REBUSHIDO_FILES)))
    switches = re.findall(r'^#define (RB_\w+) \(REBUSHIDO && (?:[01]|RB_\w+_BUILD)\)', read_raw('include/rebushido.h'), re.M) \
        if os.path.exists('include/rebushido.h') else []
    print('switches in include/rebushido.h: %d%s' % (len(switches), ' (%s)' % ', '.join(switches) if switches else ''))
    blocks = [(f, rb_blocks(f)) for a in AREAS for f in AREAS[a]]
    blocks = [(f, n) for f, n in blocks if n]
    print('`#if` blocks that ask a switch: %d in %d files%s' % (
        sum(n for _, n in blocks), len(blocks),
        ' (%s)' % ', '.join('%s %d' % (base(f), n) for f, n in blocks) if blocks else ''))

    if problems:
        title('The source and the documents disagree')
        for p in problems:
            print('  ' + p)
    return 1 if (problems and '--check' in args) else 0


if __name__ == '__main__':
    sys.exit(main())
