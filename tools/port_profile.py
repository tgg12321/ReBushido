#!/usr/bin/env python3
"""Reads what the window's --profile wrote (docs/PORT.md, "Where the time
goes"): where each blank's time went, and when each picture was shown.

    python3 tools/port_profile.py FILE [--from BLANK] [--to BLANK]

For each part of a blank's time (include/port_profile.h): its mean, the
median, the 95th centile and the most, in milliseconds, over the blanks
in which the game drew a frame and over the others; and the same of the
card's time per pass where the run had one (its `g` lines,
src/port/gl/gl_bench.h; docs/PORT.md, "Measuring on the card"). Then the pictures:
how far apart they were shown, how evenly, and how far the phase of the
pictures between two frames moved against the time that passed -- which
is what a picture landing unevenly on the screen looks like in numbers.
"""
import argparse
import statistics
import sys


def centile(values, fraction):
    if not values:
        return 0.0
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def read_gpu(path):
    """The card's passes, and {blank: [microseconds per pass]} (the `g`
    lines, which come a few blanks after their blank's own line)."""
    gpu_parts = []
    gpu = {}
    with open(path, encoding='utf-8') as f:
        for line in f:
            words = line.split()
            if words and words[0] == 'gpu_parts':
                gpu_parts = words[1:]
            elif len(words) > 2 and words[0] == 'g':
                gpu[int(words[1])] = [int(w) for w in words[2:]]
    return gpu_parts, gpu


def read(path, first, last):
    parts = []
    blanks = []
    pictures = []
    blank = None
    with open(path, encoding='utf-8') as f:
        for line in f:
            words = line.split()
            if not words:
                continue
            if words[0] == 'parts':
                parts = words[1:]
            elif words[0] == 'b':
                blank = int(words[1])
                if first <= blank <= last:
                    blanks.append((blank, int(words[2]), [int(w) for w in words[3:]]))
            elif words[0] == 'p':
                # a picture belongs to the blank whose line comes after it
                at = (blank + 1) if blank is not None else 0
                if first <= at <= last:
                    pictures.append((int(words[1]), int(words[2])))
    return parts, blanks, pictures


def report_parts(parts, blanks, title, is_card=False):
    if not blanks:
        return
    print('%s: %d blanks' % (title, len(blanks)))
    print('  %-9s %8s %8s %8s %8s' % ('part', 'mean', 'median', '95%', 'most'))
    for i, name in enumerate(parts):
        values = [row[i] / 1000.0 for row in blanks]
        print('  %-9s %8.3f %8.3f %8.3f %8.3f' % (name, statistics.mean(values), statistics.median(values),
                                                 centile(values, 0.95), max(values)))
    busy = [sum(row[i] for i, name in enumerate(parts) if name != 'wait') / 1000.0 for row in blanks]
    print('  %-9s %8.3f %8.3f %8.3f %8.3f   (%s)' % ('busy', statistics.mean(busy), statistics.median(busy),
                                                  centile(busy, 0.95), max(busy),
                                                  'every pass' if is_card else 'all but wait'))


def report_pictures(pictures, rate):
    if len(pictures) < 3:
        return
    gaps = [(b[0] - a[0]) / 1000.0 for a, b in zip(pictures, pictures[1:])]
    print('pictures: %d, %.1f a second' % (len(pictures),
                                          (len(pictures) - 1) * 1000.0 / ((pictures[-1][0] - pictures[0][0]) / 1000.0)))
    print('  between two shown: mean %.3f ms, deviation %.3f, median %.3f, 95%% %.3f, most %.3f, least %.3f'
          % (statistics.mean(gaps), statistics.pstdev(gaps), statistics.median(gaps), centile(gaps, 0.95),
             max(gaps), min(gaps)))
    if rate:
        slot = 1000.0 / rate
        late = sum(1 for g in gaps if g > slot * 1.5)
        print('  over one and a half of a slot of %.2f ms: %d (%.1f%%)' % (slot, late, 100.0 * late / len(gaps)))
    # how evenly the scene moves: the phase moved per millisecond, of each
    # two pictures in a row between the same two frames
    speeds = []
    repeats = 0
    backwards = 0
    for a, b in zip(pictures, pictures[1:]):
        if a[1] >= 0 and b[1] >= 0 and b[0] > a[0]:
            if b[1] == a[1]:
                repeats += 1
            elif b[1] < a[1]:
                backwards += 1
            else:
                speeds.append((b[1] - a[1]) / ((b[0] - a[0]) / 1000.0))
    if speeds:
        print('  phase moved a millisecond between two pictures of one pair of frames: mean %.1f, deviation %.1f '
              '(%.0f%%); the same phase twice %d times, backwards %d times'
              % (statistics.mean(speeds), statistics.pstdev(speeds),
                 100.0 * statistics.pstdev(speeds) / statistics.mean(speeds), repeats, backwards))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n\n')[0])
    ap.add_argument('file')
    ap.add_argument('--from', dest='first', type=int, default=0)
    ap.add_argument('--to', dest='last', type=int, default=1 << 31)
    ap.add_argument('--rate', type=float, default=0.0, help='the pictures a second asked for')
    args = ap.parse_args()
    parts, blanks, pictures = read(args.file, args.first, args.last)
    if not parts:
        print('port-profile: %s has no line of parts: is it what --profile wrote?' % args.file)
        return 1
    report_parts(parts, [row[2] for row in blanks if row[1]], 'blanks in which the game drew a frame')
    report_parts(parts, [row[2] for row in blanks if not row[1]], 'blanks in which it did not')
    gpu_parts, gpu = read_gpu(args.file)
    report_parts(gpu_parts, [gpu[row[0]] for row in blanks if row[1] and row[0] in gpu],
                 "the card's time, blanks in which the game drew a frame", True)
    report_parts(gpu_parts, [gpu[row[0]] for row in blanks if not row[1] and row[0] in gpu],
                 "the card's time, blanks in which it did not", True)
    report_pictures(pictures, args.rate)
    return 0


if __name__ == '__main__':
    sys.exit(main())
