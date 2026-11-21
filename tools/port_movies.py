#!/usr/bin/env python3
"""Builds the "movies-hd" mod of the native port from the player's own disc:
the opening movie, every frame made four times larger by a learned upscaler,
for the window's `movies = enhanced` (docs/MODS.md, "The movie made larger";
docs/PORT.md, "The movie made better").

    python tools/port_movies.py dump    [--image CUE] [--frames tmp/movies/frames]
                                        [--program PROGRAM]
    python tools/port_movies.py upscale [--upscaler PATH/realesrgan-ncnn-vulkan(.exe)]
                                        [--gpu N] [--frames ...] [--larger tmp/movies/larger]
    python tools/port_movies.py build   [--larger ...] [--out tmp/mods/movies-hd]
                                        [--quality 85]
    python tools/port_movies.py all     --upscaler PATH ... (the three)

`dump` runs the port with no window to the movie's end with `--dump-movie`,
which writes each frame as it is first shown, movie-NNNN.png by its number in
the stream (src/port/movie_shown.c): the PlayStation's own decode, 320 by 240.

`upscale` runs Real-ESRGAN's ncnn program, realesrgan-ncnn-vulkan (MIT, Xintao
Wang; https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan), on any Vulkan
graphics card, with its model realesr-animevideov3 at four times (BSD-3-Clause,
the Real-ESRGAN repository's: https://github.com/xinntao/Real-ESRGAN). Neither
is the project's or shipped by it: the player downloads the release
(realesrgan-ncnn-vulkan-20220424, which has the model in its models/) and
gives its program's path, or the program is the one the PC package keeps in
prepare/upscaler/ beside rebushido.exe, where its models/ has the model. The
program is run with paths as its arguments and nothing else; `--gpu N` is its
`-g`, the card it runs on (it picks one itself otherwise). With no Vulkan it
writes nothing, which is said.

`build` writes each larger frame as a baseline JPEG (Pillow; the port reads
it with stb_image) into one file, movies/opening.rbmv: "RBMOVIE1", six words
(width, height, frames, the console's width and height, 0), each frame's
place and size, then the frames (src/port/gl/gl_movie.h). A frame the dump
missed is the one before it again. The port shows frame N of the file when
the player shows frame N, so the sound, which is the disc's, is in step.

What is written is the game's own movie made larger: it goes into a folder
git ignores (tmp/mods/movies-hd) and is never committed. Its manifest gives
it the order -200, as the art mod's.
"""
import argparse
import io
import os
import shutil
import struct
import subprocess
import sys
import time
from concurrent.futures import ProcessPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

IMAGE = 'Bushido Blade 2 (USA).cue'
FRAMES = 'tmp/movies/frames'
LARGER = 'tmp/movies/larger'
OUT = 'tmp/mods/movies-hd'
PROGRAMS = {'nt': 'build/port-sdl/windows/rebushido-headless.exe',
            'posix': 'build/port/rebushido'}
# The movie is over, its frames all shown, by blank 5,900 (docs/PORT.md,
# "The movie": 244 to 6,871 with the title picture held after).
BLANKS = 6200
MODEL = 'realesr-animevideov3'
SCALE = 4
QUALITY = 85
MAGIC = b'RBMOVIE1'
KEY = 'opening'
ORDER = -200
CONSOLE_W = 320
CONSOLE_H = 240


def frame_files(folder):
    """{number: path} of movie-NNNN.png in `folder`."""
    found = {}
    for name in os.listdir(folder):
        if name.startswith('movie-') and name.endswith('.png'):
            try:
                found[int(name[6:-4])] = os.path.join(folder, name)
            except ValueError:
                pass
    return found


def fresh_folder(path):
    """`path` made empty: only ever a folder this tool writes."""
    path = os.path.abspath(path)
    if os.path.isdir(path):
        for name in os.listdir(path):
            if not name.startswith('movie-'):
                raise SystemExit('port_movies: %s holds %s, which this tool did not write; '
                                 'give another folder' % (path, name))
        shutil.rmtree(path)
    os.makedirs(path)
    return path


def dump(args):
    program = args.program or PROGRAMS.get(os.name)
    if not os.path.isfile(os.path.join(ROOT, program)):
        raise SystemExit('port_movies: no %s: build it first (wsl bash tools/build.sh %s)'
                         % (program, 'port-sdl-windows' if os.name == 'nt' else 'port'))
    if not os.path.isfile(args.image):
        raise SystemExit('port_movies: no disc image %s (--image)' % args.image)
    folder = fresh_folder(args.frames)
    started = time.time()
    run = subprocess.run([os.path.join(ROOT, program), '--image', os.path.abspath(args.image),
                          '--menu', 'rebushido', '--no-cards', '--mods', 'off',
                          '--frames', str(BLANKS), '--dump-movie', folder],
                         cwd=ROOT, capture_output=True, text=True)
    if run.returncode != 0:
        sys.stdout.write(run.stdout[-2000:])
        sys.stderr.write(run.stderr[-2000:])
        raise SystemExit('port_movies: the port ended with %d' % run.returncode)
    found = frame_files(folder)
    if not found:
        raise SystemExit('port_movies: the port showed no frame of the movie')
    print('port_movies: dump: %d frames, 1 to %d, in %.0f s: %s'
          % (len(found), max(found), time.time() - started, folder))


def find_upscaler(given):
    """The upscaler's program: the one given, or the PC package's."""
    if given:
        return given
    for folder in (os.path.join(ROOT, 'prepare', 'upscaler'), os.path.join(ROOT, 'upscaler')):
        for name in ('realesrgan-ncnn-vulkan.exe', 'realesrgan-ncnn-vulkan'):
            if os.path.isfile(os.path.join(folder, name)):
                return os.path.join(folder, name)
    return None


def upscale(args):
    args.upscaler = find_upscaler(args.upscaler)
    if not args.upscaler or not os.path.isfile(args.upscaler):
        raise SystemExit('port_movies: --upscaler names realesrgan-ncnn-vulkan, its program')
    models = os.path.join(os.path.dirname(os.path.abspath(args.upscaler)), 'models')
    if not os.path.isfile(os.path.join(models, '%s-x%d.param' % (MODEL, SCALE))):
        raise SystemExit('port_movies: no %s-x%d in %s' % (MODEL, SCALE, models))
    frames = os.path.abspath(args.frames)
    if not frame_files(frames):
        raise SystemExit('port_movies: no frames in %s: run dump first' % frames)
    larger = fresh_folder(args.larger)
    started = time.time()
    command = [os.path.abspath(args.upscaler), '-i', frames, '-o', larger,
               '-n', MODEL, '-s', str(SCALE), '-m', models, '-f', 'png']
    if args.gpu is not None:
        command += ['-g', str(args.gpu)]
    run = subprocess.run(command, capture_output=True, text=True)
    if run.returncode != 0 or len(frame_files(larger)) != len(frame_files(frames)):
        sys.stderr.write(run.stderr[-2000:])
        raise SystemExit('port_movies: the upscaler ended with %d and wrote %d of %d frames '
                         '(no Vulkan says "invalid gpu device")'
                         % (run.returncode, len(frame_files(larger)), len(frame_files(frames))))
    print('port_movies: upscale: %d frames in %.0f s: %s'
          % (len(frame_files(larger)), time.time() - started, larger))


def encode(job):
    """One frame as a baseline JPEG's bytes, and its size."""
    path, quality = job
    from PIL import Image
    picture = Image.open(path).convert('RGB')
    out = io.BytesIO()
    picture.save(out, 'JPEG', quality=quality, optimize=True, progressive=False)
    return out.getvalue(), picture.size


def build(args):
    larger = os.path.abspath(args.larger)
    found = frame_files(larger)
    if not found:
        raise SystemExit('port_movies: no frames in %s: run upscale first' % larger)
    count = max(found)
    started = time.time()
    with ProcessPoolExecutor() as pool:
        encoded = dict(zip(sorted(found), pool.map(
            encode, [(found[n], args.quality) for n in sorted(found)], chunksize=16)))
    sizes = {size for _, size in encoded.values()}
    if len(sizes) != 1:
        raise SystemExit('port_movies: the frames are not all one size: %s' % sorted(sizes))
    width, height = sizes.pop()
    out = os.path.abspath(args.out)
    if os.path.isdir(out):
        if not os.path.isfile(os.path.join(out, 'made-by-port-movies.txt')):
            raise SystemExit('port_movies: %s is not this tool\'s; give another --out' % out)
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, 'movies'))
    index = []
    body = []
    place = len(MAGIC) + 6 * 4 + count * 8
    last = None
    for number in range(1, count + 1):
        if number in encoded:
            last = encoded[number][0]
        data = last if last is not None else encoded[min(encoded)][0]
        index.append(struct.pack('<II', place, len(data)))
        body.append(data)
        place += len(data)
    path = os.path.join(out, 'movies', KEY + '.rbmv')
    with open(path, 'wb') as f:
        f.write(MAGIC + struct.pack('<6I', width, height, count, CONSOLE_W, CONSOLE_H, 0))
        f.write(b''.join(index))
        f.write(b''.join(body))
    with open(os.path.join(out, 'mod.txt'), 'w', newline='\n') as f:
        f.write('name = The opening movie made larger\n'
                'version = 1\n'
                'order = %d\n'
                'about = made from your disc by tools/port_movies.py (%s, %dx)\n'
                % (ORDER, MODEL, SCALE))
    with open(os.path.join(out, 'made-by-port-movies.txt'), 'w', newline='\n') as f:
        f.write('%d frames, %dx%d, JPEG quality %d\n' % (count, width, height, args.quality))
    print('port_movies: build: %d frames of %dx%d, %.1f MB, in %.0f s: %s'
          % (count, width, height, os.path.getsize(path) / 1e6, time.time() - started, path))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('step', choices=['dump', 'upscale', 'build', 'all'])
    ap.add_argument('--image', default=os.path.join(ROOT, IMAGE))
    ap.add_argument('--program', help='the port without a window (default %s)' % PROGRAMS.get(os.name))
    ap.add_argument('--upscaler', help='realesrgan-ncnn-vulkan\'s program, its models/ beside it')
    ap.add_argument('--gpu', type=int, help="the upscaler's -g: which card (0 the first)")
    ap.add_argument('--frames', default=os.path.join(ROOT, FRAMES))
    ap.add_argument('--larger', default=os.path.join(ROOT, LARGER))
    ap.add_argument('--out', default=os.path.join(ROOT, OUT))
    ap.add_argument('--quality', type=int, default=QUALITY)
    args = ap.parse_args()
    if args.step in ('dump', 'all'):
        dump(args)
    if args.step in ('upscale', 'all'):
        upscale(args)
    if args.step in ('build', 'all'):
        build(args)


if __name__ == '__main__':
    main()
