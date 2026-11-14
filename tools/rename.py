"""Whole-word rename across everything the build reads.

Usage: rename.py [--members] [--allow=A,B] map.tsv   (map: `old new` per line, # comments)
Refuses a new name that already exists, unless --members: struct member names
may repeat in other structs, so only use that with old names that are unique.
--allow lists new names that may already exist (merging onto an existing type,
or a word that so far only appears in a comment or string).

The overlay's link script and assembly (movovl/) are covered too: it links
the main build's Sony library modules.

A function's listing follows its name: asm/funcs/<old>.s is renamed (git mv)
to <new>.s. After a build, tools/symbol_addrs.py writes the symbol table again.
"""
import re,sys,glob,os,subprocess
members='--members' in sys.argv
allow={n for a in sys.argv if a.startswith('--allow=') for n in a[8:].split(',')}
pairs=[l.split() for l in open(sys.argv[-1]) if l.strip() and not l.startswith('#')]
m=dict(pairs); assert len(m)==len(pairs)
rx=re.compile(r'(?<![A-Za-z0-9_])('+'|'.join(map(re.escape,sorted(m,key=len,reverse=True)))+r')(?![A-Za-z0-9_])')
files=[f for pat in ('src/**/*.c','src/**/*.h','include/**/*.h','include/**/*.inc','asm/**/*.s','config/*.txt','*.ld','movovl/*.ld','movovl/asm/**/*.s') for f in glob.glob(pat,recursive=True)]
# collisions: a new name must not already exist anywhere
blob={f:open(f,'rb').read().decode('utf-8','surrogateescape') for f in files}
alltext='\n'.join(blob.values())
for old,new in m.items():
    if not members and new not in m and new not in allow and re.search(r'(?<![A-Za-z0-9_])'+re.escape(new)+r'(?![A-Za-z0-9_])',alltext): sys.exit(f'collision: {new}')
n=0
for f,t in blob.items():
    t2,k=rx.subn(lambda x:m[x.group(1)],t)
    if k: open(f,'wb').write(t2.encode('utf-8','surrogateescape')); n+=k
moved=0
for old,new in m.items():
    for d in ('asm/funcs',):
        a,b=f'{d}/{old}.s',f'{d}/{new}.s'
        if os.path.exists(a) and not os.path.exists(b):
            if subprocess.run(['git','mv',a,b]).returncode: os.rename(a,b)
            moved+=1
print('replacements',n,'listings renamed',moved)
