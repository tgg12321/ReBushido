#!/usr/bin/env python3
"""A fighter's texture picture made larger for the rebuilt model
(tools/blender/fighter_rebuild.py): the same atlas, `--scale` times each
way, its colours smoothed between texels and its edges kept.

    python tools/fighter_texture.py IN.png OUT.png [--scale 4]

The colours are resampled (Lanczos) over the texels that are drawn only,
so that the colour of nothing does not bleed into an edge; where a texel
is nothing (alpha 0, which the game does not draw) stays nothing, by the
nearest texel, so that a cut-out keeps its shape. Needs Pillow. Nothing of
the game's is in it: the picture is the one the exporter wrote from the
player's disc.
"""
import argparse

from PIL import Image, ImageFilter


def main():
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('src')
    ap.add_argument('out')
    ap.add_argument('--scale', type=int, default=4)
    args = ap.parse_args()
    src = Image.open(args.src).convert('RGBA')
    w, h = src.width * args.scale, src.height * args.scale
    # colours premultiplied by what is drawn, so that nothing does not
    # darken an edge; divided again after
    alpha = src.getchannel('A').point(lambda a: 255 if a >= 128 else 0)
    rgb = Image.composite(src.convert('RGB'), Image.new('RGB', src.size), alpha)
    big_rgb = rgb.resize((w, h), Image.LANCZOS)
    big_weight = alpha.resize((w, h), Image.LANCZOS)
    big_alpha = alpha.resize((w, h), Image.NEAREST)
    px = big_rgb.load()
    wt = big_weight.load()
    for y in range(h):
        for x in range(w):
            a = wt[x, y]
            if 0 < a < 255:
                r, g, b = px[x, y]
                px[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a))
    big_rgb = big_rgb.filter(ImageFilter.UnsharpMask(radius=2, percent=60, threshold=2))
    out = big_rgb.convert('RGBA')
    out.putalpha(big_alpha)
    out.save(args.out)
    print('fighter_texture: %dx%d -> %dx%d' % (src.width, src.height, w, h))


if __name__ == '__main__':
    main()
