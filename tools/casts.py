#!/usr/bin/env python3
"""Count the casts of the game's files and the overlay's own, by kind
(tools/count.py prints the totals; docs/FAKES.md, "Types and casts").
usage: python tools/casts.py [--list KIND] [--by-file] [--by-type KIND]
Kinds:
  int-call     (s16)f(...)            an integer cast on a call's result
  int-member   (s16)p->m, (u8)a.b     an integer cast on a member
  int-other    (s32)x, (u8)(a + b)    an integer cast on anything else
  ptr-struct   (Fighter *)p           a cast to a pointer to a struct type
  ptr-scalar   (u8 *)p, (s32 *)p      a cast to a pointer to a scalar or void
  ptr-arith    (T *)((u8 *)p + n)     a member reached by arithmetic (counted once, the outer cast)
  ptr-literal  (T *)0x80012345        an integer literal made a pointer (not hardware/scratchpad)
  ptr-hw       (T *)0x1F80xxxx        hardware register or scratchpad
  fnptr        (void (*)(int))f       a cast to a function pointer
  struct-value (SVECTOR)x             (should not exist)
"""
import re, sys, glob, os, collections

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
os.chdir(ROOT)

import count  # noqa: E402  (its read: C as the matching build compiles it, ReBushido's `#if` blocks out)

GAME = sorted(glob.glob('src/main/*.c')) + count.GAME_HEADERS
OVL = sorted(glob.glob('movovl/src/*.c')) + sorted(glob.glob('movovl/include/*.h'))

SCALARS = {'s8', 'u8', 's16', 'u16', 's32', 'u32', 's64', 'u64', 'int', 'char', 'short', 'long', 'unsigned',
           'signed', 'u_char', 'u_short', 'u_long', 'u_int', 'size_t', 'void', 'float', 'double', 'bool'}


def strip(src):
    # comments and string/char literals out, newlines kept
    def rep(m):
        s = m.group(0)
        if s.startswith('/'):
            return re.sub(r'[^\n]', ' ', s)
        return s[0] + ' ' * (len(s) - 2) + s[0]
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', rep, src, flags=re.S)


def typedefs():
    names = set()
    for f in glob.glob('include/**/*.h', recursive=True) + glob.glob('src/main/**/*.c', recursive=True) + \
            glob.glob('movovl/**/*.[ch]', recursive=True):
        s = strip(count.read(f))
        for m in re.finditer(r'\}\s*(\w+)\s*;', s):
            names.add(m.group(1))
        for m in re.finditer(r'typedef\s+[^;{}]*?\b(\w+)\s*(?:\[[^\]]*\])?;', s):
            names.add(m.group(1))
        for m in re.finditer(r'typedef\s+[^;{}]*?\(\s*\*\s*(\w+)\s*\)', s):
            names.add(m.group(1))
    return names


TYPES = typedefs() | SCALARS
QUAL = r'(?:(?:const|volatile|struct|union|enum|unsigned|signed)\s+)*'
CAST = re.compile(r'\(\s*(' + QUAL + r'[A-Za-z_]\w*(?:\s+(?:int|char|short|long))*)\s*((?:\*\s*(?:const\s*)?)*)'
                  r'(\(\s*\*+\s*\)\s*\([^()]*\))?\s*\)\s*(?=[\w(*&~!\-+])')


def base_of(t):
    w = [x for x in t.split() if x not in ('const', 'volatile', 'struct', 'union', 'enum')]
    return w


def classify(s, m):
    t, stars, fn = m.group(1), m.group(2).replace(' ', '').replace('const', ''), m.group(3)
    words = base_of(t)
    if not words or not all(w in TYPES for w in words):
        return None
    # sizeof(T) and declarations `f(void)` are not casts
    before = s[:m.start()].rstrip()
    if re.search(r'(sizeof|\w)$', before) and not re.search(r'\b(return|case|else)$', before):
        return None
    rest = s[m.end():m.end() + 200]
    scalar = all(w in SCALARS for w in words)
    if fn:
        return 'fnptr'
    if not stars:
        if not scalar:
            # a typedef'd integer type or function-pointer typedef
            return 'int-other' if True else None
        if re.match(r'[A-Za-z_]\w*\s*\(', rest):
            return 'int-call'
        op = re.match(r'(?:\(*[*&]?\s*)?[\w\[\]]+(?:\s*(?:->|\.)\s*[\w\[\]]+)*', rest)
        if op and re.search(r'->|\.', op.group(0)) and not re.match(r'[\d]', rest):
            return 'int-member'
        return 'int-other'
    lit = re.match(r'\(?\s*(0x[0-9A-Fa-f]+|\d+)\b', rest)
    if lit:
        v = int(lit.group(1), 0)
        if v == 0:
            return 'ptr-scalar' if scalar else 'ptr-struct'
        return 'ptr-hw' if (v >> 16) in (0x1F80, 0xBF80, 0x9F80) else 'ptr-literal'
    if re.match(r'\(\s*\(\s*' + QUAL + r'\w+\s*\*\s*\)[^;()]*(?:\([^()]*\))?[^;()]*[+\-]', rest):
        return 'ptr-arith'
    return 'ptr-scalar' if scalar else 'ptr-struct'


def scan(files):
    out = []
    for f in files:
        s = strip(count.read(f))
        for m in CAST.finditer(s):
            k = classify(s, m)
            if k:
                line = s.count('\n', 0, m.start()) + 1
                out.append((k, f.replace('\\', '/'), line, re.sub(r'\s+', ' ', m.group(0)), s))
    return out


def main():
    a = sys.argv[1:]
    res = {'game': scan(GAME), 'overlay': scan(OVL)}
    if '--list' in a:
        k = a[a.index('--list') + 1]
        for who in res:
            for kind, f, line, c, s in res[who]:
                if kind == k:
                    print('%s:%d: %s' % (f, line, s.split('\n')[line - 1].strip()[:150]))
        return
    if '--by-type' in a:
        k = a[a.index('--by-type') + 1]
        c = collections.Counter(x[3] for x in res['game'] + res['overlay'] if x[0] == k)
        for t, n in c.most_common(60):
            print('%5d %s' % (n, t))
        return
    if '--by-file' in a:
        c = collections.Counter((x[1], x[0]) for x in res['game'] + res['overlay'])
        files = sorted(set(f for f, _ in c))
        kinds = sorted(set(k for _, k in c))
        print('%-28s' % 'file' + ''.join('%13s' % k for k in kinds))
        for f in files:
            print('%-28s' % os.path.basename(f) + ''.join('%13d' % c[(f, k)] for k in kinds))
        return
    kinds = ['int-call', 'int-member', 'int-other', 'ptr-struct', 'ptr-scalar', 'ptr-arith', 'ptr-literal', 'ptr-hw',
             'fnptr']
    print('%-12s %7s %8s' % ('kind', 'game', 'overlay'))
    for k in kinds:
        print('%-12s %7d %8d' % (k, sum(1 for x in res['game'] if x[0] == k), sum(1 for x in res['overlay'] if x[0] == k)))
    print('%-12s %7d %8d' % ('total', len(res['game']), len(res['overlay'])))
    # the other things section 35 counts
    for who, files in (('game', GAME), ('overlay', OVL)):
        txt = '\n'.join(strip(count.read(f)) for f in files)
        print(who, 'unions:', len(re.findall(r'\bunion\b', txt)),
              ' unkNN uses:', len(re.findall(r'(?:->|\.)\s*unk[0-9A-Fa-f]+\b', txt)),
              ' unkNN members declared:', len(re.findall(r'\bunk[0-9A-Fa-f]+\b(?:\[[^\]]*\])*\s*;', txt)),
              ' D_ names:', len(set(re.findall(r'\bD_[0-9A-Fa-f]{8}\b', txt))),
              ' D_ uses:', len(re.findall(r'\bD_[0-9A-Fa-f]{8}\b', txt)))


if __name__ == '__main__':
    main()
