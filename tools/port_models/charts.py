"""A remade fighter's one texture, made of charts: a chart is a piece of
the model with a rectangle of the texture to itself and the pictures of
that rectangle (the colour, the normal, how rough, how much a metal, how
occluded, and which entry of the game's colour tables each texel answers
to). tools/port_models/README.md, "The maps".
"""
import numpy as np

import bake


# the level of detail being made: 0 the whole model, 1 and 2 lighter ones
# (the pieces' own makers read it)
LEVEL = 0


class Chart:
    """mesh: the piece (its texture points 0..1 across the chart's own
    rectangle), or None; rect: (x, y, w, h) texels of the atlas; on (h, w):
    the texels drawn on; color (h, w, 3), 0..255; normal (h, w, 3), a unit
    tangent-space normal; rough, metal, occlusion (h, w), 0..1; entry
    (h, w, 2) and entry_on (h, w): the colour tables' entry and table."""

    def __init__(self, name, rect, mesh=None):
        self.name = name
        self.rect = rect
        self.mesh = mesh
        _, _, w, h = rect
        self.on = np.zeros((h, w), bool)
        self.color = np.zeros((h, w, 3), np.float32)
        self.normal = np.zeros((h, w, 3), np.float32)
        self.normal[..., 2] = 1
        self.rough = np.full((h, w), 0.8, np.float32)
        self.metal = np.zeros((h, w), np.float32)
        self.occlusion = np.ones((h, w), np.float32)
        self.entry = np.zeros((h, w, 2), np.uint8)
        self.entry_on = np.zeros((h, w), bool)

    def to_atlas(self, uv, size):
        x, y, w, h = self.rect
        return (np.asarray(uv) * np.array([w, h]) + np.array([x, y])) / float(size)


def compose(charts, size, spread=10):
    """The atlas's pictures: colour (RGBA), normal (RGBA), the occlusion,
    roughness and metal at half the size (RGBA), and the entries at a
    quarter (RGBA: the entry, the table, nought, 255 where there is
    one)."""
    color = np.zeros((size, size, 4), np.uint8)
    normal = np.zeros((size, size, 4), np.uint8)
    normal[...] = (128, 128, 255, 255)
    orm = np.zeros((size, size, 3), np.float32)
    orm[...] = (1.0, 0.8, 0.0)
    entry = np.zeros((size, size, 4), np.uint8)
    for c in charts:
        x, y, w, h = c.rect
        on = c.on
        col = bake.spread(c.color, on, spread)
        reach = bake.distance(on, most=spread) < spread     # drawn, or spread into
        color[y:y + h, x:x + w, :3] = np.clip(col + 0.5, 0, 255).astype(np.uint8)
        drawn = getattr(c, 'alpha', None)
        if drawn is not None:
            # what the game does not draw of its texture (a lock's edge) is not drawn
            reach &= bake.spread(drawn, on, spread) >= 0.5
        color[y:y + h, x:x + w, 3] = np.where(reach, 255, 0)
        n = bake.spread(c.normal, on, spread)
        n /= np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)
        n[~reach] = (0, 0, 1)
        normal[y:y + h, x:x + w] = bake.pack_normal(n)
        packed = np.stack([c.occlusion, c.rough, c.metal], -1)
        packed = bake.spread(packed, on, spread)
        packed[~reach] = (1.0, 0.8, 0.0)
        orm[y:y + h, x:x + w] = packed
        if c.entry_on.any():
            e = bake.spread(c.entry.astype(np.float32), c.entry_on, 1)
            entry[y:y + h, x:x + w, :2] = np.where(c.entry_on[..., None], c.entry, e.astype(np.uint8))
            entry[y:y + h, x:x + w, 3] = np.where(c.entry_on, 255, 0)
    from PIL import Image
    half = np.array(Image.fromarray((np.clip(orm, 0, 1) * 255 + 0.5).astype(np.uint8)).resize(
        (size // 2, size // 2), Image.BOX))
    half = np.concatenate([half, np.full(half.shape[:2] + (1,), 255, np.uint8)], -1)
    # an entry is not a colour: the nearest texel's, never a mean
    quarter = entry[2::4, 2::4].copy()
    return color, normal, half, quarter
