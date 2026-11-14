#!/usr/bin/env python3
"""Sony's GTE macros as C, for the native port (docs/PORT.md).

    python3 tools/port_gte_macros.py          write build/port/include/psxsdk/inline_c.h
                                              and inline_o.h; print how many macros
    python3 tools/port_gte_macros.py --list   and the name of each one left out

The game reaches the GTE through the macros of include/psxsdk/inline_c.h and
inline_o.h (and gtemac.h, which is made of those). Each is a few MIPS
instructions in an `__asm__`: loads and stores, moves to and from the
coprocessor's registers, and the word of an operation. A PC has no such
coprocessor, so for the port each macro is written again here as C that
does what its instructions do, one statement an instruction, on the GTE of
src/port/gte.c:

    lwc2 $n, off(base)    port_GteWriteData(n, the word at base + off)
    swc2 $n, off(base)    the word at base + off = port_GteReadData(n)
    mtc2 / mfc2           port_GteWriteData / port_GteReadData
    ctc2 / cfc2           port_GteWriteControl / port_GteReadControl
    .word 0x4A180001      port_GteCommand(0x4A180001)
    lw, sw, sll, or ...   the same on the macro's own temporaries

Sony's two files are not changed and no copy of them is kept: the port's
headers are written from them into build/port/include/, which a build of
the port (the host tests and `make rebushido-modern`) puts ahead of
include/. tools/modern/check.py and tests/run.py call write_headers().

A macro whose operation is still the placeholder Sony's own tool (DMPSX)
would have replaced, and so has no instruction word in the header, is left
out: the game uses none of those, and code that did would not compile.
An instruction this tool does not know stops it.
"""
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
HEADERS = ('inline_c.h', 'inline_o.h')
SOURCE_DIR = 'include/psxsdk'
OUT_DIR = 'build/port/include'

DEFINE_RE = re.compile(r'#define\s+(\w+)\(([^)]*)\)\s*(.*)', re.S)
# The first word of a real operation: the coprocessor's opcode.
COP2_OPCODE = 0x4A000000
COP2_MASK = 0xFE000000


class Unknown(Exception):
    """An instruction or a macro's shape this tool has no C for."""


def statements(body):
    """The `__asm__ volatile (...)` statements of a macro's body: for each,
    its instructions and the macro parameters that are its operands."""
    found = []
    at = 0
    while True:
        start = body.find('__asm__', at)
        if start < 0:
            break
        open_at = body.index('(', start)
        depth, in_string, i = 0, False, open_at
        while True:
            ch = body[i]
            if in_string:
                in_string = ch != '"'
            elif ch == '"':
                in_string = True
            elif ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
                if depth == 0:
                    break
            i += 1
        found.append(body[open_at + 1:i])
        at = i + 1
    out = []
    for text in found:
        # the template, the outputs, the inputs, what is clobbered
        parts, depth, in_string, current = [], 0, False, ''
        for ch in text:
            if in_string:
                in_string = ch != '"'
            elif ch == '"':
                in_string = True
            elif ch == '(':
                depth += 1
            elif ch == ')':
                depth -= 1
            elif ch == ':' and depth == 0:
                parts.append(current)
                current = ''
                continue
            current += ch
        parts.append(current)
        template = ''.join(re.findall(r'"([^"]*)"', parts[0]))
        if len(parts) > 1 and parts[1].strip():
            raise Unknown('an output operand')
        operands = []
        if len(parts) > 2:
            operands = re.findall(r'"r"\s*\(\s*([^()]*?)\s*\)', parts[2])
            if len(operands) != parts[2].count('"r"'):
                raise Unknown('an operand that is not a plain name')
        instructions = [i.strip() for i in re.split(r'[;\n]', template) if i.strip()]
        out.append((instructions, operands))
    return out


def translate(macro, body):
    """The C of one macro's body, or None for one that is left out."""
    used = []     # temporaries, in the order they first appear
    operands = [] # macro parameters taken as operands
    lines = []

    def register(text, statement_operands):
        text = text.strip()
        m = re.fullmatch(r'%(\d+)', text)
        if m:
            name = statement_operands[int(m.group(1))]
            if name not in operands:
                operands.append(name)
            return 'port_%s' % name
        m = re.fullmatch(r'\$(\d+)', text)
        if not m:
            raise Unknown('the register `%s`' % text)
        name = 'port_t%s' % m.group(1)
        if name not in used:
            used.append(name)
        return name

    def number(text):
        m = re.fullmatch(r'\$(\d+)', text.strip())
        if not m:
            raise Unknown('the coprocessor register `%s`' % text)
        return m.group(1)

    def address(text, statement_operands):
        m = re.fullmatch(r'(-?\w*)\s*\(\s*([^()]+?)\s*\)', text.strip())
        if not m:
            raise Unknown('the address `%s`' % text)
        base = register(m.group(2), statement_operands)
        offset = int(m.group(1) or '0', 0)
        return '%s + %d' % (base, offset) if offset else base

    def temporary(text, statement_operands):
        name = register(text, statement_operands)
        if not name.startswith('port_t'):
            raise Unknown('a result in the operand `%s`' % text)
        return name

    for instructions, ops in statements(body):
        for instruction in instructions:
            m = re.fullmatch(r'(\.?\w+)\s*(.*)', instruction)
            op, args = m.group(1), [a for a in re.split(r'\s*,\s*', m.group(2).strip()) if a]
            if op == 'nop':
                continue
            if op == '.word':
                word = int(args[0], 0)
                if word & COP2_MASK != COP2_OPCODE:
                    return None
                lines.append('port_GteCommand(0x%08XU);' % word)
            elif op == 'lwc2':
                lines.append('port_GteWriteData(%s, PORT_GTE_U32_AT(%s));' % (number(args[0]), address(args[1], ops)))
            elif op == 'swc2':
                lines.append('PORT_GTE_U32_AT(%s) = port_GteReadData(%s);' % (address(args[1], ops), number(args[0])))
            elif op == 'mtc2':
                lines.append('port_GteWriteData(%s, (u32)%s);' % (number(args[1]), register(args[0], ops)))
            elif op == 'ctc2':
                lines.append('port_GteWriteControl(%s, (u32)%s);' % (number(args[1]), register(args[0], ops)))
            elif op == 'mfc2':
                lines.append('%s = port_GteReadData(%s);' % (temporary(args[0], ops), number(args[1])))
            elif op == 'cfc2':
                lines.append('%s = port_GteReadControl(%s);' % (temporary(args[0], ops), number(args[1])))
            elif op in ('lw', 'lh', 'lhu', 'lbu'):
                kind = {'lw': 'PORT_GTE_U32_AT(%s)', 'lh': '(u32)(s32)PORT_GTE_S16_AT(%s)',
                        'lhu': 'PORT_GTE_U16_AT(%s)', 'lbu': 'PORT_GTE_U8_AT(%s)'}[op]
                lines.append('%s = %s;' % (temporary(args[0], ops), kind % address(args[1], ops)))
            elif op in ('sw', 'sh', 'sb'):
                kind = {'sw': ('PORT_GTE_U32_AT', 'u32'), 'sh': ('PORT_GTE_U16_AT', 'u16'),
                        'sb': ('PORT_GTE_U8_AT', 'u8')}[op]
                lines.append('%s(%s) = (%s)%s;' % (kind[0], address(args[1], ops), kind[1], register(args[0], ops)))
            elif op == 'move':
                lines.append('%s = %s;' % (temporary(args[0], ops), register(args[1], ops)))
            elif op in ('sll', 'srl', 'sra'):
                source = register(args[1], ops)
                amount = int(args[2], 0)
                value = {'sll': '(u32)%s << %d', 'srl': '(u32)%s >> %d',
                         'sra': '(u32)((s32)(u32)%s >> %d)'}[op] % (source, amount)
                lines.append('%s = %s;' % (temporary(args[0], ops), value))
            elif op in ('or', 'and', 'addu', 'subu'):
                sign = {'or': '|', 'and': '&', 'addu': '+', 'subu': '-'}[op]
                lines.append('%s = (u32)%s %s (u32)%s;' % (temporary(args[0], ops), register(args[1], ops), sign,
                                                         register(args[2], ops)))
            elif op == 'addi':
                lines.append('%s = (u32)%s + (u32)(%d);' % (temporary(args[0], ops), register(args[1], ops),
                                                          int(args[2], 0)))
            else:
                raise Unknown('the instruction `%s`' % instruction)
    declared = ['PortWord port_%s = (PortWord)(%s);' % (name, name) for name in operands]
    if used:
        declared.append('PortWord %s;' % ', '.join(used))
    return '{ %s }' % ' '.join(declared + lines)


def rewrite(text, name):
    """The port's header for one of Sony's two, and the macros left out."""
    logical = re.sub(r'\\\n', ' ', text).split('\n')
    out = ['/* Written by tools/port_gte_macros.py from include/psxsdk/%s: each macro',
           ' * of Sony\'s as C on the port\'s GTE (include/port_gte.h). */',
           '#include "port_gte.h"', '']
    out[0] = out[0] % name
    count, left_out = 0, []
    for line in logical:
        m = DEFINE_RE.match(line)
        if not m or '__asm__' not in m.group(3):
            if '__asm__' in line:
                sys.exit('port_gte_macros.py: %s: assembly outside a macro: %s' % (name, line[:60]))
            out.append(line)
            continue
        macro, params, body = m.group(1), m.group(2), m.group(3)
        try:
            code = translate(macro, body)
        except Unknown as e:
            sys.exit('port_gte_macros.py: %s: %s has %s, which this tool has no C for' % (name, macro, e))
        if code is None:
            left_out.append(macro)
            continue
        out.append('#define %s(%s) %s' % (macro, params, code))
        count += 1
    if count == 0:
        sys.exit('port_gte_macros.py: %s: no GTE macro found' % name)
    if left_out:
        out += ['', '/* Left out, having no instruction word in Sony\'s header: %s. */'
                % ', '.join(left_out)]
    return '\n'.join(out) + '\n', count, left_out


def write_headers():
    """Writes the port's two headers; returns {header: (macros, left out)}."""
    done = {}
    folder = os.path.join(OUT_DIR, 'psxsdk')
    os.makedirs(folder, exist_ok=True)
    for name in HEADERS:
        with open(os.path.join(SOURCE_DIR, name), encoding='latin-1') as f:
            text, count, left_out = rewrite(f.read(), name)
        with open(os.path.join(folder, name), 'w', encoding='latin-1', newline='\n') as f:
            f.write(text)
        done[name] = (count, left_out)
    return done


def main():
    os.chdir(ROOT)
    for name, (count, left_out) in write_headers().items():
        print('%s/psxsdk/%s: %d macros as C, %d left out' % (OUT_DIR, name, count, len(left_out)))
        if '--list' in sys.argv[1:]:
            for macro in left_out:
                print('    ' + macro)
    return 0


if __name__ == '__main__':
    sys.exit(main())
