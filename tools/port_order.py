#!/usr/bin/env python3
"""Where the native port's compiler may evaluate an expression in another
order than the PlayStation's did (docs/PORT.md, "What the PlayStation's
compiler decided").

    python3 tools/port_order.py             the places, and how many
    python3 tools/port_order.py --check     the same, held to the list of
                                            config/port_order.txt: exits 1 if
                                            a place is not in it
    python3 tools/port_order.py --update    writes that list again
    python3 tools/port_order.py --all       also the places where the order
                                            differs and it cannot matter, in
                                            brackets: for checking this tool
    --bits N                                of the 32-bit program or the
                                            64-bit one (PORT_BITS): the two
                                            are two compilers' work, and
                                            `make port` asks about both

C leaves open the order in which the operands of an operator and the
arguments of a call are evaluated. The game was compiled by GCC 2.7.2 for
MIPS, which (measured on the compiler itself, tools/gcc-2.7.2):

  - makes the calls of one expression in the order they are written;
  - reads a variable or a member that is an operand or an argument as late
    as it can: after every call of the expression that it is not itself an
    argument of.

The port is compiled by a modern GCC for x86, which evaluates a call's
arguments from the last to the first and reads memory as it comes to it.
So in

    fighter_StartMove(i, motion_GetMove(f, id, &f->is_common), f->is_common);

the PlayStation passes what motion_GetMove has just written, and a PC what
was there before. Where the call writes what the other operand reads, the
two programs differ, and nothing warns of it.

This finds every such place. It compiles each file of the port as
tools/port_build.py does, asking the compiler for the statements it made of
the source in the order it will run them (-fdump-tree-gimple-lineno), and
reports, within one statement of the source:

  read    a variable or member is read before a call that it is not an
          argument of, and the call (or anything it calls) may write it:
          the PlayStation reads it after the call
  calls   two calls in two arguments of a third are made in the other
          order than they are written, and one may write what the other
          reads or writes
  late    a variable or member in an earlier argument is read after a
          call in a later argument, which may write it. The PlayStation
          does the same where the earlier argument is that variable and
          nothing more, and reads it first where the argument is worked
          out from it: to be looked at

  short   a call passes fewer arguments than the function it calls is
          defined with, which C allows where the function has no
          prototype (`extern void f();`, as the original declares some):
          the PlayStation passes what its registers happened to hold, a
          PC something else, and two PCs need not agree. Never taken
          from the list: the port passes the console's value under RB_PORT
          (docs/PORT.md, "An argument that was not passed")

"May write" is worked out from the same statements, for every function and
what it calls: which variables it assigns, which members (by the member's
name, whatever the structure's address), and whether it writes through a
bare pointer, which can reach any variable or member whose address is
taken somewhere. A call through a pointer to a function may write anything.

A place that is reported is put right in the source under RB_PORT (the
call first, into a variable of its own), which takes it off the list, or
is looked at and written into config/port_order.txt with why it does not
matter.
"""
import concurrent.futures
import os
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))

import port_build  # noqa: E402

OUT_DIR = port_build.OUT_DIR + '/order'
ACCEPTED = 'config/port_order.txt'

LOCATION = re.compile(r'\[([^\]\s]+):(\d+):(\d+)\] ')
TEMP = re.compile(r'^(?:_\d+|D\.\d+|[A-Za-z_]\w*\.\d+(?:_\d+)?|iftmp\.\d+)$')
NAME = re.compile(r'[A-Za-z_][\w.]*')
CALL = re.compile(r'^([A-Za-z_][\w.]*) \((.*)\)$')
HEADER = re.compile(r'^[A-Za-z_][\w \*]*?\b(\w+) \((.*)\)$')
DECLARATION = re.compile(r'^\s+(?:static |const |volatile |register |extern )*[A-Za-z_][\w \*]*?\b(\w+)'
                         r'(?:\[[^\]]*\])*(?: = .*)?;$')
KEYWORDS = {'if', 'else', 'goto', 'return', 'switch', 'case', 'default', 'try', 'finally', 'catch'}
ANYTHING = ('*', '*')


class Statement:
    def __init__(self, where, text):
        self.where = where      # (file, line, column)
        self.text = text
        self.defines = None     # the temporary it sets
        self.callee = None      # the function it calls; '*' through a pointer
        self.reads = []         # the memory it reads: (variable, member)
        self.writes = []
        self.uses = set()       # the names in it
        self.arguments = []     # a call's: the names in each argument


class Function:
    def __init__(self, name, path):
        self.name = name
        self.path = path
        self.locals = set()
        self.groups = []        # the statements of each statement of the source
        self.reads = set()
        self.writes = set()
        self.calls = set()
        self.parameters = 0     # how many it is defined with


def split_arguments(text):
    """The arguments of a call, cut at the commas that are not inside
    brackets or a string."""
    parts, depth, start, quoted = [], 0, 0, False
    for i, c in enumerate(text):
        if c == '"' and (i == 0 or text[i - 1] != '\\'):
            quoted = not quoted
        elif quoted:
            continue
        elif c in '([<':
            depth += 1
        elif c in ')]>':
            depth -= 1
        elif c == ',' and depth == 0:
            parts.append(text[start:i].strip())
            start = i + 1
    last = text[start:].strip()
    if last:
        parts.append(last)
    return parts


def place_of(text, function, exposed):
    """What memory an operand is: (variable, member) with None for either,
    or None if it is no memory (a number, a local, a temporary). An
    operand that begins with & takes an address: it reads nothing, and
    the variable or member is written down in `exposed`."""
    text = text.strip()
    address = text.startswith('&')
    if address:
        text = text[1:].strip()
    if not text or text[0] in '"\'' or text[0].isdigit() or text[0] == '-':
        return None
    pointer = text.startswith('*') or text.startswith('MEM') or '->' in text
    # a member: the last name after a . or ->, whatever is indexed after it
    member = None
    m = re.search(r'(?:\.|->)([A-Za-z_]\w*)(?:\[[^\]]*\])*$', text)
    if m:
        member = m.group(1)
    names = [n for n in NAME.findall(re.sub(r'\([^()]*\)', ' ', text)) if n not in ('MEM', 'B')]
    if not names:
        return None
    base = names[0].split('.')[0] if not TEMP.match(names[0]) else names[0]
    is_local = TEMP.match(base) or base in function.locals
    if is_local and not pointer:
        return None     # a local or a temporary itself
    place = (None if is_local else base, member)
    if address:
        exposed.add(place)
        return None
    return place


def parse(path, text, exposed):
    """The functions of one file's dump."""
    functions = []
    function, group = None, None
    lines = text.split('\n')
    for number, raw in enumerate(lines):
        if raw and not raw[0].isspace() and raw[0] not in '[{}<':
            m = HEADER.match(raw)
            if m and number + 1 < len(lines) and lines[number + 1].rstrip().endswith('{'):
                function = Function(m.group(1), path)
                parameters = split_arguments(m.group(2))
                function.parameters = 0 if parameters in ([], ['void']) else len(parameters)
                for parameter in parameters:
                    names = NAME.findall(parameter)
                    if names:
                        function.locals.add(names[-1])
                functions.append(function)
                group = None
            continue
        if function is None:
            continue
        where = LOCATION.search(raw)
        text = LOCATION.sub('', raw).strip()
        if not text or text in '{}' or text.endswith(':') or text.startswith('<'):
            continue
        if text == '# DEBUG BEGIN_STMT':
            group = []
            function.groups.append(group)
            continue
        if where is None:
            m = DECLARATION.match(raw)
            if m and not raw.strip().startswith('static '):
                function.locals.add(m.group(1))
            continue
        if group is None:
            group = []
            function.groups.append(group)
        statement = Statement((where.group(1), int(where.group(2)), int(where.group(3))), text)
        group.append(statement)
        body = text[:-1] if text.endswith(';') else text
        left, right = None, body
        m = re.match(r'^(.*?) = (.*)$', body)
        if m and not body.startswith('if '):
            left, right = m.group(1), m.group(2)
        call = CALL.match(right)
        if call and call.group(1) not in KEYWORDS:
            name = call.group(1)
            statement.callee = '*' if TEMP.match(name) or name in function.locals else name
            operands = split_arguments(call.group(2))
            statement.arguments = [set(NAME.findall(operand)) for operand in operands]
        elif left is not None:
            operands = [right] if re.match(r'^(?:&|\*|MEM|VIEW_CONVERT|BIT_FIELD|[A-Za-z_])', right) and \
                not re.search(r' (?:[-+*/%&|^<>]|<<|>>|==|!=|<=|>=) ', re.sub(r'\[[^\]]*\]|<[^>]*>', '', right)) \
                else []
        else:
            operands = []
        for operand in operands:
            place = place_of(operand, function, exposed)
            if place is not None:
                statement.reads.append(place)
        if left is not None:
            if TEMP.match(left):
                statement.defines = left
            else:
                place = place_of(left, function, set())
                if place is not None:
                    statement.writes.append(place)
        statement.uses = set(NAME.findall(right if left is not None else body))
        function.reads.update(statement.reads)
        function.writes.update(statement.writes)
        if statement.callee:
            function.calls.add(statement.callee)
    return functions


def conflicts(write, read, exposed_variables, exposed_members):
    """Whether writing one place can change what reading another gives."""
    if write == ANYTHING or read == ANYTHING:
        return True
    (w_variable, w_member), (r_variable, r_member) = write, read
    if w_variable and r_variable:
        return w_variable == r_variable and (w_member is None or r_member is None or w_member == r_member)
    if w_variable:      # a variable written, read through a pointer
        if r_member is not None:
            return w_member == r_member
        return w_variable in exposed_variables
    if r_variable:      # written through a pointer, a variable read
        if w_member is not None:
            return w_member == r_member
        return r_variable in exposed_variables or r_member in exposed_members
    if w_member is not None and r_member is not None:
        return w_member == r_member
    if w_member is None and r_member is None:
        return True
    return (w_member or r_member) in exposed_members


def dump_one(job):
    path, flags, stamp = job
    out = OUT_DIR + '/' + path.replace('/', '_') + '.gimple'
    deps = out + '.d'
    # the dump of the last run, where no input of it has changed since
    if not port_build.made_since(out, deps, stamp, path):
        for old in (out, deps):
            if os.path.exists(old):
                os.remove(old)
        cmd = ['gcc'] + flags + ['-MD', '-MF', deps, '-fdump-tree-gimple-lineno=' + out, '-x', 'c', '-S', '-',
                                 '-o', '/dev/null']
        proc = subprocess.run(cmd, input=port_build.modern.modern_source(path), capture_output=True, text=True,
                              env=port_build.environment(), encoding='utf-8', errors='replace')
        if proc.returncode != 0:
            for old in (out, deps):
                if os.path.exists(old):
                    os.remove(old)
            return path, None, proc.stderr
        if not os.path.exists(out):
            # a file with no function: an empty dump, so that it is not
            # compiled again
            open(out, 'w').close()
    if not os.path.exists(out):
        return path, '', ''     # a file with no function
    with open(out, encoding='utf-8', errors='replace') as f:
        return path, f.read(), ''


def find(everything=False):
    """The places, as (file, line, kind, callee, what), sorted."""
    os.makedirs(OUT_DIR, exist_ok=True)
    port_build.host_tests.OUT_DIR = port_build.OUT_DIR
    port_build.host_tests.write_headers(True)
    flags = (port_build.GAME_FLAGS + port_build.host_tests.include_flags(True) + port_build.modern.DEFS
             + port_build.host_tests.REBUSHIDO_DEFS + port_build.MOVOVL_INCLUDES
             + ['-DRB_GIT_HASH="order"'])
    stamp = max(port_build.build_stamp(), os.path.getmtime(__file__))
    jobs = [(path, flags + mine, stamp) for path, own, mine in port_build.sources()]
    functions, exposed = [], set()
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for path, text, problem in pool.map(dump_one, jobs):
            if text is None:
                sys.exit('port_order.py: %s does not compile:\n%s' % (path, problem[:2000]))
            functions += parse(path, text, exposed)
    return analyse(functions, exposed, everything)


def analyse(functions, exposed, everything=False):
    """The places in what `parse` made of the files."""
    exposed_variables = {variable for variable, member in exposed if variable}
    exposed_members = {member for variable, member in exposed if member}

    # what each function may write and read, with everything it calls; two
    # functions of one name (statics of two files) are taken together
    by_name = {}
    for function in functions:
        by_name.setdefault(function.name, []).append(function)
    writes, reads = {}, {}
    for name, same in by_name.items():
        writes[name] = set().union(*(f.writes for f in same))
        reads[name] = set().union(*(f.reads for f in same))
    writes['*'], reads['*'] = {ANYTHING}, {ANYTHING}
    changed = True
    while changed:
        changed = False
        for name, same in by_name.items():
            for callee in set().union(*(f.calls for f in same)):
                # a function with no body here writes and reads nothing
                # the game's variables can see
                for sets in (writes, reads):
                    more = sets.get(callee, set()) - sets[name]
                    if more:
                        sets[name] |= more
                        changed = True

    def may_write(callee, place):
        return any(conflicts(w, place, exposed_variables, exposed_members) for w in writes.get(callee, ()))

    def interfere(first, second):
        w1, w2 = writes.get(first, set()), writes.get(second, set())
        r1, r2 = reads.get(first, set()), reads.get(second, set())
        return any(conflicts(w, x, exposed_variables, exposed_members) for w in w1 for x in r2 | w2) or \
            any(conflicts(w, x, exposed_variables, exposed_members) for w in w2 for x in r1)

    found = set()
    # a call that passes fewer arguments than the function is defined with,
    # which C allows where the function has no prototype: the parameters
    # it leaves out are whatever their registers held, which is not what
    # the PlayStation's held
    parameters = {name: min(f.parameters for f in same) for name, same in by_name.items()}
    for function in functions:
        for group in function.groups:
            for statement in group:
                wanted = parameters.get(statement.callee)
                if wanted is not None and len(statement.arguments) < wanted:
                    found.add((statement.where[0], statement.where[1], 'short', statement.callee,
                               '%d of its %d arguments' % (len(statement.arguments), wanted)))
    for function in functions:
        for group in function.groups:
            calls = [i for i, s in enumerate(group) if s.callee]
            if not calls:
                continue
            # what each statement is worked out from, through temporaries
            made_by, needs, made_at = {}, [], []
            for i, statement in enumerate(group):
                mine = set()
                for name in statement.uses:
                    if name in made_by:
                        mine |= {made_by[name]} | needs[made_by[name]]
                needs.append(mine)
                made_at.append(dict(made_by))
                if statement.defines:
                    made_by[statement.defines] = i
            for i, statement in enumerate(group):
                if statement.callee or not statement.defines or not statement.reads:
                    continue
                for k in calls:
                    call = group[k]
                    if i in needs[k] or k in needs[i]:
                        continue    # one is worked out from the other: their order is fixed
                    place = statement.reads[0]
                    if not may_write(call.callee, place):
                        if everything and i < k and any(i in needs[j] for j in range(k + 1, len(group))):
                            found.add((call.where[0], call.where[1], '(read)', call.callee, statement.text))
                        continue
                    if i < k:
                        # read first here; after the call there. Only if it
                        # is used after the call at all
                        if any(i in needs[j] for j in range(k + 1, len(group))):
                            found.add((call.where[0], call.where[1], 'read', call.callee, statement.text))
            # the arguments of one call: what is in an earlier argument
            # against a call in a later one that was made before it
            for c in calls:
                feeds = []
                for names in group[c].arguments:
                    mine = set()
                    for name in names:
                        if name in made_at[c]:
                            mine |= {made_at[c][name]} | needs[made_at[c][name]]
                    feeds.append(mine)
                for a in range(len(feeds)):
                    for b in range(a + 1, len(feeds)):
                        for i in feeds[a] - feeds[b]:
                            for k in feeds[b] - feeds[a]:
                                earlier, later = group[i], group[k]
                                if not later.callee or k > i:
                                    continue
                                if earlier.callee:
                                    matters = interfere(earlier.callee, later.callee)
                                    if matters or everything:
                                        found.add((later.where[0], later.where[1],
                                                   'calls' if matters else '(calls)', later.callee,
                                                   'before %s' % earlier.callee))
                                elif earlier.defines and earlier.reads and                                         may_write(later.callee, earlier.reads[0]):
                                    found.add((later.where[0], later.where[1], 'late', later.callee,
                                               earlier.text))
    return sorted(found)


def key_of(place):
    """A place as the list holds it: without its line, which moves."""
    path, line, kind, callee, what = place
    # nor the numbers the compiler gives what it works out on the way
    what = re.sub(r'\b_\d+\b', '_', re.sub(r'\.\d+_\d+\b', '', what))
    return '%s %s %s: %s' % (path, kind, callee, what)


def main():
    global OUT_DIR
    os.chdir(ROOT)
    args = sys.argv[1:]
    bits = port_build.bits_of(args)
    if bits is None:
        return 1
    port_build.set_bits(bits)
    OUT_DIR = port_build.OUT_DIR + '/order'
    problem = port_build.check_host()
    if problem:
        print('port_order: skipped: %s' % problem)
        return 0
    places = find('--all' in args)
    accepted = {}
    if os.path.exists(ACCEPTED):
        with open(ACCEPTED, encoding='utf-8') as f:
            for line in f.read().split('\n'):
                if line and not line.startswith('#'):
                    key, _, why = line.partition('  # ')
                    accepted[key] = why
    if '--update' in args:
        with open(ACCEPTED, 'w', encoding='utf-8', newline='\n') as f:
            f.write('# Places where the order of evaluation is the compiler\'s choice and the port\'s\n'
                    '# compiler may choose otherwise than the PlayStation\'s (tools/port_order.py),\n'
                    '# each looked at, with why it makes no difference. Written by --update, which\n'
                    '# keeps the reasons; a place with none has not been looked at.\n')
            for key in sorted({key_of(place) for place in places if place[2] != 'short'}):
                f.write(key + ('  # ' + accepted[key] if accepted.get(key) else '') + '\n')
        print('port_order: %s: %d places' % (ACCEPTED, len({key_of(place) for place in places})))
        return 0
    new = [place for place in places if key_of(place) not in accepted or place[2] == 'short']
    shown = new if '--check' in args else places
    for path, line, kind, callee, what in shown:
        print('%s:%d: %-5s %s: %s' % (path, line, kind, callee, what))
    print('port_order: %d-bit: %d places where the order of evaluation is the compiler\'s and may matter; '
          '%d not in %s' % (port_build.BITS, len(places), len(new), ACCEPTED))
    if '--check' in args and new:
        if any(place[2] == 'short' for place in new):
            print("FAILED: port_order: a call leaves arguments out: under RB_PORT pass what the PlayStation's "
                  'registers hold there, read from the listing')
        if any(place[2] != 'short' for place in new):
            print('FAILED: port_order: put the call first under RB_PORT, or look at the place and add it to the '
                  'list (--update, then its reason)')
        return 1
    if '--check' in args:
        print('OK: port_order')
    return 0


if __name__ == '__main__':
    sys.exit(main())
