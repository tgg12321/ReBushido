#!/usr/bin/env python3
"""Count what the checklist of docs/STYLE.md can have counted by a script.

    python tools/lint.py                    the totals, and the ten files with the most
    python tools/lint.py --by-file          a line for every file
    python tools/lint.py --file F [F...]    those files' counts, and every finding with its line
    python tools/lint.py --list CHECK       every finding of one check, tree-wide
    python tools/lint.py --check            exit 1 if a count is over the baseline's
    python tools/lint.py --update           write the baseline from the tree as it is

It reads the game's own files: src/main/*.c, include/*.h, movovl/src/*.c and
movovl/include/*.h, and ReBushido's, src/rebushido/*.c and src/port/*.c. Sony's library code
is not looked at. It needs no build.

A file is read with both sides of every `#if RB_NAME` block in it (the
switches of include/rebushido.h): the lines that ask the switch are taken
out and what is between them stays, so ReBushido's code is held to the
checklist like the rest, and a block's `#else` is not taken for the body of
the `if` above it. That works as long as each side of a block is whole
statements or declarations, which docs/STYLE.md asks.

The baseline is tools/lint_baseline.tsv (file, check, count; zeroes
left out). `--check` fails if a file has more of something than the baseline
says, or the tree has more in total; it says what went down too. After work
that brought counts down, or that moved code from one file to another, run
`--update` and commit the baseline with the work.

The checks (the rule each counts against is in docs/STYLE.md, "Checklist"):

  names         locals and parameters whose name says nothing: a single letter
                other than a loop counter (i j k), a coordinate or size
                (x y z w h), a texture coordinate (u with v) or a colour
                (r g b together); tmp, temp, val, ptr, ret, res, flag, var_N,
                spNN, argN, workN and the like. `work` and `unused` are not
                counted: they are the names for a local the bytes force.
  hex           hex literals inside function bodies that nothing explains.
                Not counted: 0xFF, 0xFFFF and 0xFFFFFFFF, a shift count, and a
                literal on a line that has a comment or in the statement that
                follows a comment.
  dec           decimal literals of 5 or more inside function bodies, the
                same way; and not the number that makes a part or a multiple
                of a unit (`3 * ONE / 4`, `ANGLE_TURN / 8`,
                `8 * FIGHTER_SPEED_ONE`), which is how rule 5 writes one,
                nor the 100 under a `..._PERCENT`.
  lower-hex     a hex literal with a lower-case digit, or an integer suffix
                in lower case.
  extern-c      `extern` in a .c file, other than the label of a listing
                (`extern u8 L[] __asm__(".L...")`).
  proto-c       a function declared, not defined, in a .c file.
  proto-plain   a prototype in a header without `extern`.
  empty-params  a function declared or defined with `()`.
  unnamed-param a prototype in a header with a parameter that has no name.
  unused-param  a parameter the function never uses, unless the comment above
                the function says so ("not used", "unused", "ignored").
  no-return     a function with a return type other than void and no
                `return` with a value.
  wrong-header  a header that declares something whose name has another
                header's prefix (`math_...` or `g_math_...` outside math.h).
  old-style     a function defined in the old style (`f(a) int a; {`).
  unk           members named unkNN or unk_NN, by declaration.
  c-types       int, short, long, unsigned or signed outside common.h.
  no-braces     an if, else, for or while whose body has no braces.
  top-comment   a file that does not start with a comment saying what it is
                (a header: straight after its guard).
  func-comment  a function of FUNC_LINES lines or more with no comment
                straight above it.
  guard         a header whose guard is not `#ifndef NAME_H`, `#define
                NAME_H`, ..., `#endif /* NAME_H */`.
  includes      a .c file whose includes are not in the order of STYLE.md,
                or that includes something bb2.h already does.
  spelling      identifiers with a part the tree spells another way (idx for
                index, cnt/num/nb for count, tbl for table, mtx for matrix,
                flg for flag, ang for angle; British spellings: colour,
                grey, centre, armour, cancelled, -ise), by distinct name per
                file.
  cxx-comment   a `//` comment.
  address       a RAM address (0x80xxxxxx) in a comment.
  todo          TODO, FIXME, XXX, HACK or ??? in a comment.
  rb-flag       an `#if` or `#elif` that asks an RB_ name include/rebushido.h
                does not define as a switch (a misspelt one is 0, and the
                change silently left out of every build), or that mixes a
                switch with anything but other switches, `!`, `&&` and `||`;
                and an `#ifdef` or `#ifndef` of a switch, which is defined
                whether it is on or off.
"""
import collections
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import count as C  # noqa: E402  (chdirs to the repository root)

BASELINE = 'tools/lint_baseline.tsv'
# the constants a fixed-point value is written as a part or a multiple of
UNIT = r'\b(?:ONE|ANGLE_TURN|[A-Z][A-Z0-9_]*_ONE)\b'
FUNC_LINES = 12

CHECKS = ['names', 'hex', 'dec', 'lower-hex', 'extern-c', 'proto-c', 'proto-plain', 'empty-params',
          'unnamed-param', 'unused-param', 'no-return', 'wrong-header', 'old-style', 'unk', 'c-types', 'no-braces', 'top-comment', 'func-comment',
          'guard', 'includes', 'spelling', 'cxx-comment', 'address', 'todo', 'rb-flag']

KEYWORDS = {'if', 'while', 'for', 'switch', 'return', 'sizeof', 'else', 'do', 'goto', 'case', 'break',
            'continue', 'default', 'typedef', 'extern', 'static', 'const', 'volatile', 'register',
            'struct', 'union', 'enum', 'inline', '__inline__', '__asm__', 'asm'}
BASIC_TYPES = {'s8', 'u8', 's16', 'u16', 's32', 'u32', 's64', 'u64', 'void', 'char', 'int', 'short',
               'long', 'unsigned', 'signed', 'u_char', 'u_short', 'u_long', 'u_int', 'va_list'}
QUALIFIERS = r'(?:(?:static|const|volatile|register|unsigned|signed|struct|union|enum)\s+)*'

CONTENTLESS = re.compile(
    r'^(?:[a-hl-tv]|[a-z]\d|tmp\w*|temp\w*|val\d*|ptr\d*|ret\d*|res\d*|rv|var\d*|var_\w+|new_var\d*|'
    r'sp[0-9A-F]{2,3}|sp_\w+|arg\d+|param\d*|work\d+|flag\d*|dummy\d*|foo|thing|stuff)$')
# single letters that are fine alone, and those fine in company
ALWAYS_FINE = set('ijkxyzwh')
FINE_WITH = {'r': 'g', 'g': 'b', 'b': 'g', 'u': 'v', 'v': 'u'}
# a digit after a letter that is a coordinate, a colour or a counter is one of several of them
NUMBERED_FINE = re.compile(r'^[ijkxyzwhuvrgb]\d$')

# a word of an identifier the tree spells another way, and the way it spells it
SPELLING = {'idx': 'index', 'cnt': 'count', 'num': 'count', 'nb': 'count', 'tbl': 'table', 'mtx': 'matrix',
            'flg': 'flag', 'ang': 'angle', 'colour': 'color', 'grey': 'gray', 'centre': 'center',
            'centred': 'centered'}
# British spellings, and the American ones rule 3 asks for
SPELLING.update({
    'colours': 'colors', 'coloured': 'colored', 'greys': 'grays', 'centres': 'centers',
    'armour': 'armor', 'armoured': 'armored', 'behaviour': 'behavior', 'neighbour': 'neighbor',
    'neighbours': 'neighbors', 'honour': 'honor', 'favour': 'favor', 'defence': 'defense',
    'offence': 'offense', 'metre': 'meter', 'metres': 'meters', 'millimetre': 'millimeter',
    'cancelled': 'canceled', 'cancelling': 'canceling', 'travelled': 'traveled',
    'modelled': 'modeled', 'labelled': 'labeled', 'levelled': 'leveled', 'analyse': 'analyze',
    'analysed': 'analyzed'})
for _stem in ('initiali', 'normali', 'penali', 'organi', 'reali', 'recogni', 'synchroni', 'seriali',
              'finali', 'optimi', 'minimi', 'maximi', 'neutrali', 'equali', 'randomi', 'summari',
              'visuali', 'memori', 'prioriti', 'centrali', 'speciali', 'utili', 'apologi'):
    for _end in ('se', 'sed', 'ses', 'sing', 'ser', 'sation'):
        SPELLING[_stem + _end] = _stem + 'z' + _end[1:]
WORD = re.compile(r'[A-Z]+(?![a-z])|[A-Z]?[a-z]+')
_SONY = None


def sony_names():
    """Every identifier of Sony's headers: those keep Sony's spelling."""
    global _SONY
    if _SONY is None:
        _SONY = set()
        for f in C.files('include/psxsdk/**/*.h', 'movovl/sdk/**/*.h'):
            _SONY.update(re.findall(r'\b[A-Za-z_]\w*\b', C.read(f)))
    return _SONY


def misspelt(name):
    for w in WORD.findall(name):
        if w.lower() in SPELLING:
            return SPELLING[w.lower()]
    return None


FUNC_DEF = C.FUNC_DEF
PROTO = re.compile(
    r'^(?!typedef|struct|union|enum|#|\s|return)((?:extern\s+)?[A-Za-z_][\w \t\*]*?)\b(\w+)\s*'
    r'\(((?:[^;{}()]|\([^;{}()]*\))*)\)\s*;', re.M)
BII = set(['bb2.h'])


def game_files():
    return C.files('src/main/*.c', 'src/rebushido/*.c', 'src/port/*.c', 'include/*.h', 'movovl/src/*.c',
                   'movovl/include/*.h')


def read(f):
    """A file with the lines that ask ReBushido's switches taken out and both
    sides of each such block kept."""
    return C.rb_blank(C.read_raw(f), ('directive',))


SWITCHES_HEADER = 'include/rebushido.h'
_SWITCHES = None


def switches():
    """The names include/rebushido.h defines as switches, and the master one."""
    global _SWITCHES
    if _SWITCHES is None:
        _SWITCHES = set(['REBUSHIDO'])
        if os.path.exists(SWITCHES_HEADER):
            _SWITCHES.update(re.findall(r'^#define (RB_\w+) \(REBUSHIDO && (?:[01]|RB_\w+_BUILD)\)', C.read_raw(SWITCHES_HEADER), re.M))
    return _SWITCHES


def switch_findings(f):
    """[(line, text)] the lines of a file that ask a switch wrongly."""
    out = []
    for number, line in enumerate(C.strip(C.read_raw(f)).split('\n'), 1):
        m = C.DIRECTIVE.match(line)
        if not m or m.group(1) in ('else', 'endif'):
            continue
        word, rest = m.group(1), m.group(2)
        names = re.findall(C.RB_NAME, rest)
        if word in ('ifdef', 'ifndef'):
            # the master switch's own default is the one place a switch is asked this way
            if any(n in switches() for n in names) and f != SWITCHES_HEADER:
                out.append((number, '#%s of a switch: %s' % (word, line.strip())))
            continue
        unknown = [n for n in names if n not in switches()]
        if unknown:
            out.append((number, '%s is not a switch of %s' % (', '.join(unknown), SWITCHES_HEADER)))
        elif names and C.rb_value(rest) is None:
            out.append((number, 'a switch mixed with something else: %s' % line.strip()))
    return out


def line_of(s, pos):
    return s.count('\n', 0, pos) + 1


def comment_lines(src):
    """The set of lines a comment touches, and of those it ends on."""
    touched, ends = set(), set()
    for m in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', src, flags=re.S):
        if not m.group(0).startswith('/'):
            continue
        a, b = line_of(src, m.start()), line_of(src, m.end())
        touched.update(range(a, b + 1))
        ends.add(b)
    return touched, ends


def bodies(s):
    """[(name, params, old_style_decls, head_start, body_start, body_end, return_type)] of each
    function defined."""
    out = []
    for m in FUNC_DEF.finditer(s):
        if m.group(2) in KEYWORDS:
            continue
        depth, i = 0, m.end() - 1
        while i < len(s):
            if s[i] == '{':
                depth += 1
            elif s[i] == '}':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out.append((m.group(2), m.group(3), m.group(4), m.start(), m.end() - 1, i, m.group(1)))
    return out


def squash(text):
    """Brackets of every kind taken out with what is in them."""
    prev = None
    while prev != text:
        prev = text
        text = re.sub(r'\([^()]*\)|\[[^\[\]]*\]|\{[^{}]*\}', ' ', text)
    return text


def declared(body):
    """[(name, offset)] the locals a function body declares."""
    out = []
    for m in re.finditer(r'(?<=[;{}])\s*' + QUALIFIERS + r'([A-Za-z_]\w*)\b[ \t]*((?:\*|[ \t])[^;{}]*);',
                         body):
        typ = m.group(1)
        if typ in KEYWORDS or not (typ in BASIC_TYPES or typ[0].isupper()):
            continue
        rest = m.group(2)
        if not re.match(r'[\s\*]*[A-Za-z_]', rest) or re.match(r'\s*[\*\s]*\w+\s*\(', rest):
            continue
        for part in squash(rest).split(','):
            part = part.split('=')[0]
            names = re.findall(r'[A-Za-z_]\w*', part)
            if len(names) == 1 and not re.search(r'[^\w\s\*]', part):
                out.append((names[0], m.start(2)))
    return out


def param_names(params, old_decls):
    out = []
    for part in squash(params).split(','):
        names = [n for n in re.findall(r'[A-Za-z_]\w*', part) if n not in KEYWORDS]
        if len(names) >= 2 or (len(names) == 1 and old_decls.strip()):
            out.append(names[-1])
    return out


def contentless(name, all_names):
    if name in ALWAYS_FINE or NUMBERED_FINE.match(name):
        return False
    if name in FINE_WITH and FINE_WITH[name] in all_names:
        return False
    return bool(CONTENTLESS.match(name))


def explained_lines(src, s, first, last):
    """Lines of first..last whose numbers a comment explains: a line with a comment on it, and the
    statement that follows a comment."""
    touched, ends = comment_lines(src)
    code = s.split('\n')
    out = set()
    carry = False
    for ln in range(first, last + 1):
        text = code[ln - 1].strip()
        if ln in touched:
            out.add(ln)
            if not text:
                carry = ln in ends or carry
                continue
            carry = False
            continue
        if not text:
            continue
        if carry:
            out.add(ln)
            if text[-1] in ';{}:':
                carry = False
    return out


def lint(f):
    """{check: [(line, text)]} for one file."""
    src = read(f)
    s = C.strip(src)
    is_c = f.endswith('.c')
    name = os.path.basename(f)
    found = collections.OrderedDict((c, []) for c in CHECKS)
    raw_lines = src.split('\n')

    def add(check, pos_or_line, text=None, is_line=False):
        ln = pos_or_line if is_line else line_of(s, pos_or_line)
        found[check].append((ln, text if text is not None else raw_lines[ln - 1].strip()))

    fns = bodies(s)

    # names, hex, dec, func-comment, old-style
    touched, ends = comment_lines(src)
    for fname, params, old, head, b0, b1, rtype in fns:
        body = s[b0:b1 + 1]
        locs = declared(body)
        pars = param_names(params, old)
        every = set(n for n, _ in locs) | set(pars)
        for p in pars:
            if contentless(p, every):
                add('names', head, '%s: parameter %s' % (fname, p))
        for n, off in locs:
            if contentless(n, every):
                add('names', b0 + off, '%s: local %s' % (fname, n))
        first, last = line_of(s, b0), line_of(s, b1)
        ok = explained_lines(src, s, first, last)
        for m in re.finditer(r'(?<![\w.])(0[xX][0-9A-Fa-f]+|\d+)[uUlL]*\b', body):
            ln = line_of(s, b0 + m.start())
            if ln in ok:
                continue
            lit = m.group(1)
            before = body[max(0, m.start() - 80):m.start()]
            if re.search(r'(<<|>>)=?\s*$', before):
                continue
            # a part or a multiple of a unit, as rule 5 writes a fixed-point value:
            # `3 * ONE / 4`, `ANGLE_TURN / 8`, `8 * FIGHTER_SPEED_ONE`
            if lit[:2].lower() != '0x' and (
                    re.search(UNIT + r'\s*[*/]\s*\(?\s*$', before) or
                    re.search(r'\b\d+\s*\*\s*' + UNIT + r'\s*/\s*$', before) or
                    re.match(r'\s*\*\s*' + UNIT, body[m.end():m.end() + 60])):
                continue
            # a hundredth of something named a percentage
            if lit == '100' and re.search(r'_PERCENT\)*\s*/\s*\(?\s*$', before):
                continue
            if lit[:2].lower() == '0x':
                if int(lit, 16) in (0xFF, 0xFFFF, 0xFFFFFFFF):
                    continue
                add('hex', ln, '%s: %s' % (fname, lit), True)
            elif int(lit) >= 5:
                add('dec', ln, '%s: %s' % (fname, lit), True)
        if old.strip():
            add('old-style', head, fname)
        # the comment above the function, if there is one
        ln = line_of(s, head) - 1
        above = []
        while ln >= 1 and ln in touched and not s.split('\n')[ln - 1].strip():
            above.append(raw_lines[ln - 1])
            ln -= 1
        above = ' '.join(reversed(above))
        if not re.search(r'not used|unused|ignored|nothing uses|takes no', above, re.I):
            for p in pars:
                if not re.search(r'\b%s\b' % re.escape(p), body):
                    add('unused-param', head, '%s: %s' % (fname, p))
        rtype = re.sub(r'\b(static|inline|__inline__|extern|const)\b', '', rtype).strip()
        if rtype and rtype != 'void' and not re.search(r'\breturn\b\s*[^;\s]', body):
            add('no-return', head, '%s %s' % (rtype, fname))
        if last - first + 1 >= FUNC_LINES:
            ln = line_of(s, head) - 1
            while ln >= 1 and not raw_lines[ln - 1].strip():
                ln -= 1
            if ln < 1 or ln not in ends or s.split('\n')[ln - 1].strip():
                add('func-comment', head, '%s (%d lines)' % (fname, last - first + 1))

    # literals' case
    for m in re.finditer(r'\b0[xX][0-9A-Fa-f]+[uUlL]*\b|\b\d+[uUlL]+\b', s):
        t = m.group(0)
        digits = re.sub(r'[uUlL]+$', '', t)
        if t.startswith('0X') or re.search(r'[a-f]', digits[2:] if digits[:2].lower() == '0x' else '') or \
                re.search(r'[ul]+$', t):
            add('lower-hex', m.start(), t)

    # declarations
    inside = [(f[4], f[5]) for f in fns]

    def at_file_scope(pos):
        return not any(a <= pos <= b for a, b in inside)

    if is_c:
        for m in re.finditer(r'^[ \t]*extern\b[^;]*;', s, re.M):
            if '__asm__' in m.group(0) and re.search(r'__asm__\s*\(\s*"\.L', src[m.start():m.end()]):
                continue
            add('extern-c', m.start() + len(m.group(0)) - len(m.group(0).lstrip()))
    for m in PROTO.finditer(s):
        if m.group(2) in KEYWORDS or not at_file_scope(m.start()):
            continue
        head = m.group(1)
        if '=' in head or '(' in head:
            continue
        if is_c:
            if not head.startswith('extern') and not re.search(r'\bstatic\b', head):
                add('proto-c', m.start())
        else:
            if not head.startswith('extern') and 'inline' not in head:
                add('proto-plain', m.start())
            params = m.group(3).strip()
            if params and params != 'void':
                for part in squash(params).split(','):
                    part = part.strip()
                    if part == '...':
                        continue
                    names = [n for n in re.findall(r'[A-Za-z_]\w*', part)
                             if n not in ('const', 'volatile', 'struct', 'union', 'enum', 'unsigned', 'signed')]
                    if len(names) < 2 and '(' not in m.group(3):
                        add('unnamed-param', m.start(), '%s(%s)' % (m.group(2), re.sub(r'\s+', ' ', params)))
                        break
        if not m.group(3).strip():
            add('empty-params', m.start())
    for fname, params, old, head, b0, b1, rtype in fns:
        if not params.strip() and not old.strip():
            add('empty-params', head, fname + '()')

    if not is_c and '/' not in f.replace('include/', '', 1):
        mine = name[:-2]
        for m in re.finditer(r'^extern\b[^;]*;', s, re.M):
            d = re.sub(r'__asm__\s*\([^)]*\)', '', m.group(0))
            pm = PROTO.match(d)
            if pm and '(' not in pm.group(1):
                n = pm.group(2)
            else:
                pm = re.search(r'\(\s*\*\s*(\w+)', d) or re.search(r'(\w+)\s*(?:\[[^\]]*\]\s*)*;\s*$', d)
                n = pm.group(1) if pm else ''
            parts = n.split('_')
            owner = parts[1] if parts[0] == 'g' and len(parts) > 2 else parts[0]
            if len(parts) > 1 and owner != mine and owner in header_names():
                add('wrong-header', m.start(), '%s (%s.h)' % (n, owner))

    for m in re.finditer(r'\b(unk_?[0-9A-Fa-f]+)\b\s*(?:\[[^\]]*\]\s*)*(?::\s*\d+\s*)?[;,]', s):
        add('unk', m.start(), m.group(1))

    if name != 'common.h':
        for m in re.finditer(r'\b(int|short|long|unsigned|signed)\b', s):
            add('c-types', m.start())

    for m in re.finditer(r'^[ \t]*(?:\}[ \t]*)?(?:(?:else[ \t]+)?(?:if|for|while)\b|else\b)', s, re.M):
        kw_end = m.end()
        i = kw_end
        word = m.group(0).split()[-1]
        if word != 'else':
            # skip the parenthesised head
            while i < len(s) and s[i] != '(':
                i += 1
            depth = 0
            while i < len(s):
                if s[i] == '(':
                    depth += 1
                elif s[i] == ')':
                    depth -= 1
                    if depth == 0:
                        i += 1
                        break
                i += 1
        nxt = re.match(r'\s*(\S+)', s[i:i + 200])
        if not nxt:
            continue
        tok = nxt.group(1)
        if tok.startswith('{') or tok.startswith(';'):
            continue
        if word == 'else' and tok.startswith('if'):
            continue
        if word == 'while' and re.match(r'\s*;', s[i:]):
            continue
        # `} while (...);` of a do loop
        if word == 'while' and m.group(0).lstrip().startswith('}'):
            continue
        add('no-braces', m.start() + len(m.group(0)) - len(m.group(0).lstrip()))

    # the file as a whole
    lead = src.lstrip()
    if is_c:
        if not lead.startswith('/*'):
            add('top-comment', 1, 'no comment at the top', True)
        incs = re.findall(r'^#include\s+(\S+)', src, re.M)
        want = wanted_includes(f, incs)
        if incs != want:
            add('includes', 1, '%s, wanted %s' % (' '.join(incs), ' '.join(want)), True)
    else:
        g = re.match(r'(?:/\*.*?\*/\s*)?#ifndef\s+(\w+)\s*\n#define\s+(\w+)[ \t]*\n(\s*)(\S\S)', src, re.S)
        guard = re.sub(r'\W', '_', name).upper()
        if not g or g.group(1) != guard or g.group(2) != guard or src.lstrip().startswith('/*') or \
                not src.rstrip().endswith('#endif /* %s */' % guard):
            add('guard', 1, 'guard is not %s, first and last' % guard, True)
        if not g or g.group(4) != '/*':
            add('top-comment', 1, 'no comment after the guard', True)

    seen = set()
    for m in re.finditer(r'\b[A-Za-z_]\w*\b', s):
        w = m.group(0)
        if w in seen:
            continue
        seen.add(w)
        better = misspelt(w)
        if better and w not in KEYWORDS and w not in sony_names():
            add('spelling', m.start(), '%s (%s)' % (w, better))

    for m in re.finditer(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', src, flags=re.S):
        t = m.group(0)
        if not t.startswith('/'):
            continue
        ln = line_of(src, m.start())
        if t.startswith('//'):
            found['cxx-comment'].append((ln, t.strip()))
        a = re.search(r'\b(?:0x)?80[01][0-9A-Fa-f]{5}\b', t)
        if a:
            found['address'].append((line_of(src, m.start() + a.start()), a.group(0)))
        a = re.search(r'\b(?:TODO|FIXME|XXX|HACK)\b|\?\?\?', t)
        if a:
            found['todo'].append((line_of(src, m.start() + a.start()), a.group(0)))
    found['rb-flag'] = switch_findings(f)
    return found


# What bb2.h includes already; a .c file names only what it does not.
def bb2_includes():
    out = set()
    for inc in re.findall(r'^#include\s+(\S+)', read('include/bb2.h'), re.M):
        out.add(inc)
    return out


_BB2 = None
_HEADERS = None


def header_names():
    global _HEADERS
    if _HEADERS is None:
        _HEADERS = set(os.path.basename(h)[:-2] for h in C.files('include/*.h'))
    return _HEADERS


def wanted_includes(f, incs):
    """The includes of a .c file in the order of STYLE.md: common.h, include_asm.h if the file has
    it, Sony's headers that bb2.h does not include (the GTE macros have to be ahead of math.h),
    then bb2.h and nothing after it. The overlay has no bb2.h: common.h first is all that is asked."""
    global _BB2
    if _BB2 is None:
        _BB2 = bb2_includes()
    if f.startswith('movovl/'):
        return ['"common.h"'] + [i for i in incs if i != '"common.h"']
    rest = []
    for inc in incs:
        if inc in ('"common.h"', '"bb2.h"') or inc in rest or inc in _BB2:
            continue
        rest.append(inc)
    asm = [i for i in rest if i == '"include_asm.h"']
    other = [i for i in rest if i != '"include_asm.h"']
    return ['"common.h"'] + asm + other + ['"bb2.h"']


def counts_of(found):
    return collections.OrderedDict((c, len(v)) for c, v in found.items())


def read_baseline():
    base = collections.defaultdict(dict)
    if os.path.exists(BASELINE):
        for line in open(BASELINE, encoding='utf-8'):
            line = line.rstrip('\n')
            if not line or line.startswith('#'):
                continue
            f, c, n = line.split('\t')
            base[f][c] = int(n)
    return base


def write_baseline(table):
    with open(BASELINE, 'w', encoding='utf-8', newline='') as out:
        out.write('# tools/lint.py --update writes this. file, check, count; zeroes left out.\n')
        for f in sorted(table):
            for c in CHECKS:
                if table[f][c]:
                    out.write('%s\t%s\t%d\n' % (f, c, table[f][c]))


def print_table(rows, heads):
    widths = [max(len(str(r[i])) for r in rows + [heads]) for i in range(len(heads))]
    for r in [heads] + rows:
        print('  '.join(str(v).ljust(w) if i == 0 else str(v).rjust(w) for i, (v, w) in enumerate(zip(r, widths))))


def main():
    args = sys.argv[1:]
    if '--file' in args:
        fs = [a.replace('\\', '/') for a in args[args.index('--file') + 1:] if not a.startswith('--')]
        base = read_baseline()
        for f in fs:
            found = lint(f)
            n = counts_of(found)
            print('%s: %d' % (f, sum(n.values())))
            for c in CHECKS:
                was = base.get(f, {}).get(c, 0)
                if n[c] or was:
                    print('  %-14s %5d   (baseline %d)' % (c, n[c], was))
            for c in CHECKS:
                for ln, text in found[c]:
                    print('%s:%d: %s: %s' % (f, ln, c, text))
        return 0

    table = collections.OrderedDict()
    findings = {}
    for f in game_files():
        findings[f] = lint(f)
        table[f] = counts_of(findings[f])
    totals = collections.OrderedDict((c, sum(table[f][c] for f in table)) for c in CHECKS)

    if '--list' in args:
        check = args[args.index('--list') + 1]
        for f in table:
            for ln, text in findings[f][check]:
                print('%s:%d: %s' % (f, ln, text))
        print('%d' % totals[check])
        return 0

    if '--update' in args:
        write_baseline(table)
        print('wrote %s: %d findings in %d files' % (BASELINE, sum(totals.values()), len(table)))
        return 0

    if '--check' in args:
        base = read_baseline()
        bad = 0
        down = 0
        for c in CHECKS:
            was = sum(base[f].get(c, 0) for f in base)
            if totals[c] > was:
                print('UP    %-14s %d, baseline %d' % (c, totals[c], was))
                bad += 1
            elif totals[c] < was:
                print('down  %-14s %d, baseline %d' % (c, totals[c], was))
                down += 1
        for f in table:
            for c in CHECKS:
                was = base.get(f, {}).get(c, 0)
                if table[f][c] > was:
                    print('UP    %s: %s %d, baseline %d' % (f, c, table[f][c], was))
                    bad += 1
        if bad:
            print('lint: %d counts over the baseline (python tools/lint.py --file <file> lists them)' % bad)
            return 1
        print('lint: nothing over the baseline' + (' (%d totals under it: --update)' % down if down else ''))
        return 0

    if '--by-file' in args:
        used = [c for c in CHECKS if totals[c]]
        rows = [[f] + [table[f][c] or '' for c in used] + [sum(table[f].values())] for f in table
                if sum(table[f].values())]
        print_table(rows, ['file'] + used + ['all'])
        return 0

    print('lint of %d files (docs/STYLE.md, "Checklist")' % len(table))
    for c in CHECKS:
        nfiles = sum(1 for f in table if table[f][c])
        print('  %-14s %6d   in %d files' % (c, totals[c], nfiles))
    print('  %-14s %6d' % ('all', sum(totals.values())))
    print('the ten files with the most:')
    worst = sorted(table, key=lambda f: -sum(table[f].values()))[:10]
    rows = [[f, sum(table[f].values())] +
            ['%s %d' % (c, table[f][c]) for c in sorted(CHECKS, key=lambda c: -table[f][c])[:4] if table[f][c]]
            for f in worst]
    for r in rows:
        print('  %-28s %6d   %s' % (r[0], r[1], ', '.join(r[2:])))
    return 0


if __name__ == '__main__':
    sys.exit(main())
