#!/usr/bin/env python3
"""What the PC package ships in prepare/ so that rebushido.exe can make the
mods of the player's own disc itself (tools/port_prepare.py; docs/MODS.md,
"Made from your disc"): a Python of its own with numpy and Pillow, taken
from Windows' own Python as it is installed here, and the developer tools
that make the mods, as they are in the repository.

    python3 tools/port_prepare_bundle.py [--python FOLDER] [--out FOLDER]

tools/port_release.py puts it into the package (files()); run by itself it
writes the folder (build/port-prepare/prepare/ unless --out says), which is
how tests/rebushido/port_prepare.py tries it without a package.

Why a Python and not C: the mods must be the bytes the developer tools
write, and those are numpy's and Pillow's arithmetic (Lanczos, bicubic,
unsharp masks, a PNG encoder, BLAS): the same libraries, the same
versions, give the same bytes, and no rewrite would. It is the Python of
the PC that packs it -- Windows' own, the one the developer tools' `build`
steps run on -- so the player's mods are made by the very files the
owner's are.

The Python is found as WSL finds Windows' own (python.exe, which must
import numpy and Pillow), or is the folder --python or RB_WINDOWS_PYTHON
names. Its parts:

    prepare/python/          python.exe, its DLLs, the standard library as
                             python312.zip (what is never imported left
                             out), the few compiled modules of DLLs/ that
                             are, and python312._pth, which makes it a
                             Python of this folder alone: no setting or
                             package of the PC's is seen
    prepare/python/site-packages/   numpy and Pillow, without their tests,
                             their type stubs and Pillow's codecs that no
                             tool here reads (AVIF, WebP, fonts, colour
                             management)
    prepare/tools/           tools/port_prepare.py and what it runs
    prepare/src/main/        the three files of the source fighter_export.py
                             reads its tables from
    prepare/upscaler/        realesrgan-ncnn-vulkan.exe, vcomp140.dll and
                             models/realesrgan-x4plus.*: the learned
                             upscaler the art's pictures are made larger
                             with (tools/port_art.py), its files held to
                             their SHA-256 (UPSCALER_FILES)
    prepare/licenses/        Python's, numpy's (and OpenBLAS's and the
                             others it bundles), Pillow's and the
                             upscaler's licences (tools/upscaler/)

tools/port_release.py writes prepare/plans/ beside it, the plans the
program plays, from the program's own .sym.
"""
import argparse
import io
import os
import re
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = 'build/port-prepare'
PYTHON_VERSION = '312'
PYTHON_FILES = ['python.exe', 'python3.dll', 'python%s.dll' % PYTHON_VERSION, 'vcruntime140.dll',
                'vcruntime140_1.dll']
# the compiled modules of DLLs/ the tools import: multiprocessing's (the
# stages' pages are made several at once) and ctypes, which numpy asks for
DLLS = ['_multiprocessing.pyd', '_overlapped.pyd', '_queue.pyd', 'select.pyd', '_socket.pyd', '_ctypes.pyd',
        'libffi-8.dll', 'unicodedata.pyd']
# folders and files of the standard library never imported here
STDLIB_LEFT_OUT = {'test', 'idlelib', 'tkinter', 'turtledemo', 'ensurepip', 'lib2to3', 'pydoc_data', 'venv',
                   'msilib', 'site-packages', 'curses', 'sqlite3', 'dbm', '__pycache__', 'turtle.py'}
# of numpy and Pillow
PACKAGE_LEFT_OUT = {'tests', '__pycache__', 'f2py', '_pyinstaller'}
PACKAGE_ENDINGS_LEFT_OUT = ('.pyi', '.pyc')
PILLOW_LEFT_OUT = ('_avif.', '_webp.', '_imagingft.', '_imagingcms.', '_imagingtk.')
PACKAGES = ['numpy', 'numpy.libs', 'PIL']
PTH = 'python%s.zip\nDLLs\nsite-packages\n..\\tools\n' % PYTHON_VERSION
# the repository's files the tools need, by their place under prepare/
TOOLS = ['tools/port_prepare.py', 'tools/port_art.py', 'tools/port_stage_hd.py', 'tools/fighter_export.py',
         'tools/build_disc.py', 'tools/port_models/geometry.py', 'tools/port_models/fighter_glb.py',
         'tools/port_models/meshes.py', 'tools/port_models/check.py', 'src/main/model.c', 'src/main/resource.c',
         'src/main/resource_data.c']
# The learned upscaler the art mod is made with (tools/port_art.py,
# docs/MODS.md, "The art mod"): realesrgan-ncnn-vulkan's Windows release of
# 2022-04-24 (Real-ESRGAN v0.2.5.0) and its x4plus model, four of its files
# and no more, each held to its SHA-256 so that only that release is ever
# shipped. Not in the repository: the packager takes them from
# RB_UPSCALER, or from tmp/art-tools/realesrgan-ncnn-vulkan-20220424/ of
# this checkout or of the main one, where
# https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip
# (SHA-256 abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d)
# is unpacked.
UPSCALER_FOLDER = 'realesrgan-ncnn-vulkan-20220424'
UPSCALER_FILES = {
    'realesrgan-ncnn-vulkan.exe': '07e49f7cbb4ede01ae4dd4c399d3a7e5846e3d2085c3128eff881e55cb7b1a0c',
    # Microsoft's OpenMP runtime, which the program is built with; one of
    # Visual C++'s redistributable files, as vcruntime140.dll is
    'vcomp140.dll': '8f72ef2e483465444b2059fc6744d6cb22cd8d8a27f6fa56befd2a42dcd0f78b',
    'models/realesrgan-x4plus.bin': '713ee713b0353afaa27976f0563a64a5043bd70b9bd8936c2e26e25ebcdbcddf',
    'models/realesrgan-x4plus.param': '35330ececcea33b6c397a72548e788d5d53becee4734c50b7fada36e89f10a86',
}
# their licences, kept in the repository (tools/upscaler/)
UPSCALER_LICENCES = ['Real-ESRGAN-ncnn-vulkan-LICENSE.txt', 'Real-ESRGAN-LICENSE.txt', 'ncnn-LICENSE.txt']
LICENCE_NOTE = '''The Python in prepare/
====================

rebushido.exe runs prepare/python/python.exe once, the first time it
starts with a disc, to make the mods of the art, the stages and the
fighters from that disc (docs/MODS.md in the repository, "Made from your
disc"). It is the standard CPython %(python)s for Windows, as python.org
ships it, with:

- numpy %(numpy)s (https://numpy.org/): the BSD licence, with the licences
  of what its wheel bundles -- OpenBLAS (BSD), LAPACK (BSD), the GCC
  runtime's parts (GPL with the GCC Runtime Library Exception) and
  Microsoft's C++ runtime -- in licenses/numpy/.
- Pillow %(pillow)s (https://python-pillow.github.io/): the MIT-CMU licence,
  with what its wheel builds in (zlib, libjpeg, libpng and the others), in
  licenses/Pillow-LICENSE.txt.

Python itself is under the PSF licence, licenses/Python-LICENSE.txt, which
lists what it is built with. vcruntime140.dll and vcruntime140_1.dll are
Microsoft's Visual C++ runtime, which Python's own distributions carry
beside it and which may be given with a program built with it.

The upscaler in prepare/upscaler/
=================================

The art mod's pictures are made larger by a learned upscaler, where the
PC has a graphics card with Vulkan (without one, by the Python's own
arithmetic):

- realesrgan-ncnn-vulkan (https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan),
  of the release Real-ESRGAN v0.2.5.0: the MIT licence, with
  realsr-ncnn-vulkan's MIT licence, in
  licenses/Real-ESRGAN-ncnn-vulkan-LICENSE.txt. It is built with Tencent's
  ncnn (https://github.com/Tencent/ncnn): the BSD 3-Clause licence, with
  the licences of what ncnn builds in, in licenses/ncnn-LICENSE.txt.
- The model realesrgan-x4plus (models/), the weights of Real-ESRGAN
  (https://github.com/xinntao/Real-ESRGAN), published with it under its
  BSD 3-Clause licence, licenses/Real-ESRGAN-LICENSE.txt.
- vcomp140.dll, Microsoft's OpenMP runtime, which the program is built
  with and which may be given with it as vcruntime140.dll is.

These are separate programs that rebushido.exe starts; none of them is
built into it. The tools in prepare/tools/ are this repository's own
(LICENSE.txt).
'''


def fail(message):
    sys.exit('port_prepare_bundle.py: ' + message)


def read(path):
    with open(path, 'rb') as f:
        return f.read()


def to_here(path):
    """A Windows path as this Python opens it: itself on Windows, under
    /mnt/ in WSL."""
    if os.name == 'nt' or not re.match(r'^[A-Za-z]:\\', path):
        return path
    return '/mnt/%s/%s' % (path[0].lower(), path[3:].replace('\\', '/'))


def windows_python():
    """The folder of Windows' own Python that has numpy and Pillow."""
    named = os.environ.get('RB_WINDOWS_PYTHON')
    if named:
        return to_here(named)
    if os.name == 'nt':
        return sys.base_prefix
    probe = 'import sys, numpy, PIL; print(sys.base_prefix)'
    try:
        # from a folder of the Windows drive: Windows' programs cannot
        # start in one of WSL's
        proc = subprocess.run(['python.exe', '-c', probe], capture_output=True, text=True, cwd='/mnt/c')
    except OSError:
        return None
    if proc.returncode != 0:
        return None
    return to_here(proc.stdout.strip())


def deterministic_zip(files):
    """A zip of {name: bytes}, the same bytes for the same files."""
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for name in sorted(files):
            info = zipfile.ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, files[name])
    return out.getvalue()


def tree(top, left_out, endings_left_out=()):
    """{path under top: bytes} of a folder, what is left out not."""
    files = {}
    for folder, dirs, names in os.walk(top):
        dirs[:] = sorted(d for d in dirs if d not in left_out)
        for name in sorted(names):
            if name in left_out or name.endswith(endings_left_out):
                continue
            path = os.path.join(folder, name)
            files[os.path.relpath(path, top).replace(os.sep, '/')] = read(path)
    return files


def dist_version(site, package):
    for entry in os.listdir(site):
        m = re.fullmatch(r'%s-([0-9][^-]*)\.dist-info' % re.escape(package), entry, re.I)
        if m:
            return m.group(1), os.path.join(site, entry)
    fail('%s has no %s (pip install %s, in Windows\' own Python)' % (site, package, package))


def main_checkout():
    """The checkout of which this one is a worktree (itself if none)."""
    proc = subprocess.run(['git', '-C', ROOT, 'rev-parse', '--path-format=absolute', '--git-common-dir'],
                          capture_output=True, text=True)
    common = proc.stdout.strip()
    return os.path.dirname(common) if proc.returncode == 0 and common else ROOT


def upscaler_files():
    """{path under prepare/: bytes} of the upscaler and its licences; the
    packager stops where a file is missing or is not the release's."""
    import hashlib

    places = [os.environ.get('RB_UPSCALER')] + [os.path.join(top, 'tmp', 'art-tools', UPSCALER_FOLDER)
                                                 for top in (ROOT, main_checkout())]
    folder = next((to_here(p) for p in places if p and os.path.isfile(os.path.join(to_here(p), 'models',
                                                                                    'realesrgan-x4plus.bin'))), None)
    if folder is None:
        fail('no upscaler: unpack %s into tmp/art-tools/%s/ (or RB_UPSCALER=FOLDER)' % (
            'realesrgan-ncnn-vulkan-20220424-windows.zip', UPSCALER_FOLDER))
    out = {}
    for rel, want in UPSCALER_FILES.items():
        path = os.path.join(folder, *rel.split('/'))
        if not os.path.isfile(path):
            fail('%s is not there' % path)
        data = read(path)
        if hashlib.sha256(data).hexdigest() != want:
            fail('%s is not the release\'s (its SHA-256 is not %s)' % (path, want))
        out['upscaler/' + rel] = data
    for name in UPSCALER_LICENCES:
        out['licenses/' + name] = read(os.path.join(ROOT, 'tools', 'upscaler', name))
    return out


def files(python=None):
    """{path under prepare/: bytes}, all of it but the plans."""
    python = python or windows_python()
    if not python or not os.path.isfile(os.path.join(python, 'python.exe')):
        fail('no Windows Python with numpy and Pillow (python.exe in WSL, or RB_WINDOWS_PYTHON=FOLDER)')
    lib = os.path.join(python, 'Lib')
    site = os.path.join(lib, 'site-packages')
    numpy_version, numpy_info = dist_version(site, 'numpy')
    pillow_version, pillow_info = dist_version(site, 'pillow')
    out = {}
    for name in PYTHON_FILES:
        out['python/' + name] = read(os.path.join(python, name))
    for name in DLLS:
        out['python/DLLs/' + name] = read(os.path.join(python, 'DLLs', name))
    stdlib = dict((k, v) for k, v in tree(lib, STDLIB_LEFT_OUT).items() if k.endswith('.py'))
    out['python/python%s.zip' % PYTHON_VERSION] = deterministic_zip(stdlib)
    out['python/python%s._pth' % PYTHON_VERSION] = PTH.encode('ascii')
    for package in PACKAGES:
        for rel, data in tree(os.path.join(site, package), PACKAGE_LEFT_OUT, PACKAGE_ENDINGS_LEFT_OUT).items():
            if package == 'PIL' and rel.startswith(PILLOW_LEFT_OUT):
                continue
            out['python/site-packages/%s/%s' % (package, rel)] = data
    for rel in TOOLS:
        out[rel] = read(os.path.join(ROOT, rel))
    out['licenses/Python-LICENSE.txt'] = read(os.path.join(python, 'LICENSE.txt'))
    for rel, data in tree(os.path.join(numpy_info, 'licenses'), set()).items():
        out['licenses/numpy/' + rel] = data
    out['licenses/Pillow-LICENSE.txt'] = read(os.path.join(pillow_info, 'licenses', 'LICENSE'))
    out.update(upscaler_files())
    version = re.search(rb'Python (3\.\d+\.\d+)', read(os.path.join(python, 'NEWS.txt'))) \
        if os.path.isfile(os.path.join(python, 'NEWS.txt')) else None
    out['licenses/README.txt'] = (LICENCE_NOTE % {
        'python': version.group(1).decode() if version else '3.' + PYTHON_VERSION[1:],
        'numpy': numpy_version, 'pillow': pillow_version}).replace('\n', '\r\n').encode('utf-8')
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--python', help="Windows' own Python's folder (found if not said)")
    ap.add_argument('--out', default=OUT, help='the folder prepare/ is written into')
    args = ap.parse_args()
    made = files(to_here(args.python) if args.python else None)
    top = os.path.join(args.out, 'prepare')
    for rel, data in made.items():
        path = os.path.join(top, *rel.split('/'))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, 'wb') as f:
            f.write(data)
    print('OK: %s: %d files, %d bytes' % (top, len(made), sum(len(d) for d in made.values())))
    return 0


if __name__ == '__main__':
    sys.exit(main())
