# The fighters' models remade (ROADMAP 19)

The pipeline from the player's disc to a checked `remade` mod, and the
checks that hold a new model to the game's own. The why and the numbers
are in `docs/PORT.md`, "The fighters remade"; this is how to run it.

    python tools/port_models/build.py --fighter M101

runs every step for one fighter (`M101` is Tatsumi; `build.py`'s `ROSTER`
names the others). `--steps` takes some of them:

| step | what | where it writes (the main checkout's, git ignores them) |
|---|---|---|
| `reference` | the game's own model (`fighter_export.py`) and the hd mod's (`fighter_hd.py`) | `tmp/models/ref/` |
| `motions` | the game's motions, as the port poses them (`motions.py`, in WSL; `--gl-fighter-motion`) | `tmp/models/ref/motions/` |
| `prepare` | the game's own mesh with more polygons, each of its charts cut for its new texture layout (`prepare.py`) | `tmp/models/work/NAME/` |
| `unwrap` | the charts' new layouts, laid flat in Blender (`tools/blender/fighter_unwrap.py`) | the same |
| `assemble` | the game's texture cleaned into the new layout, the file, its levels of detail, its maps (`assemble.py`) | `tmp/mods/remade/` |
| `check` | the checks; the build fails with them (`check.py`) | `tmp/models/work/NAME/check/` |
| `renders` | the game's, the hd mod's and the new model side by side, standing, close and in motion (`tools/blender/fighter_scene.py`) | `tmp/models/renders/NAME/` |
| `port` | the same fight in the port's own renderer with each (headless, in WSL; `python3 tools/port_gl_build.py` first) | `tmp/models/renders/NAME/port/` |

`--mod` and `--work` put the mod and the working files elsewhere (to try
a change beside the last good model), `--also OLDER.glb` shows another
model in the renders, `--renders NAME` names their folder.

Blender is the portable 4.5 in the main checkout's `tmp/tools/`
(`tools/blender/README.md`), or `$BLENDER`; it is used for the layouts
and for the pictures, nothing else. Everything else is Python with numpy
and Pillow (Windows' Python, as `fighter_hd.py`): a fighter takes about
two minutes to assemble and four to check.

## What a remade fighter is

The owner's rule (2026-11-18): the game's own design, cleaned, not a new
one. A remade fighter must read as the PlayStation's model at a glance:
its outline, proportions, shapes, colours and texture design are the
game's; nothing is modelled or painted anew, and no fold or wrinkle is
added.

- **The mesh** (`body.py`, `meshes.py`): the game's own, each triangle
  made four, twice, as a smooth surface through the game's shape (Loop's
  subdivision, most of its smoothing taken: `smooth` of the recipe).
  Every vertex keeps its own bone (a new one, its edge's two ends'), so
  that it bends exactly as the game's mesh does; nothing is fitted or
  swollen (a smooth surface lies inside its coarse one: the hd mod,
  fitted through the game's vertices, swells, and made the chest bulge),
  and none is let far inside the game's surface (`inside_units`) or moved
  far (`most_units`): a corner is rounded, not lost. An open edge stays
  an open edge's curve with its sharp corners where they are; a piece of
  two dozen triangles or fewer (a strap, a plate on a shoulder) is cut in
  four and not smoothed. **A joint's triangles stay the game's own**: an
  edge between two bones gets no new vertex, because no vertex half way
  along it moves as the game's stretched triangle does, neither by the
  file's terms nor by the plain weights a tool without them draws by
  (Blender, the model viewer); the joints are to be remade with edge
  loops and weights of their own. The normals are worked out again.
- **The layout** (`fighter_unwrap.py`): a texture layout of its own, cut
  where the game's is cut, laid flat in Blender with every island at one
  scale and none over another (a fifth of the game's layout lies over
  itself, which no normal map can live with). The recipe's `charts` give
  some bones a rectangle of their own (the head: more texels to a unit),
  and `larger` makes a material's islands larger within one (the face's
  skin).
- **The colour** (`bake.Clean`): the game's texture, cleaned and sharp.
  Its colours are gathered into kinds (the tones of one cloth, of the
  skin); the picture of kinds is made eight times larger with its
  staircases cut across (three times Scale2x), so that a kind's edge is a
  clean line and the smallest thing painted (the light in an eye, one
  texel) is still there; within a kind the tones run into one another.
  What the game does not draw is a kind too, so a lock of hair's edge is
  clean. Every texel of the new layout reads it where it lay in the
  game's layout.
- **The face is the game's own texels** (the owner's rule for every
  fighter, 2026-11-19): the face stays pixelated, stylized and true to
  the game. It is not traced, cleaned or smoothed: the recipe's `kept`
  chart (the head's triangles whose texture is mostly skin) keeps the
  game's own layout a whole number of times larger (`body.kept_layout`,
  8 for Tatsumi), so that each of the game's texels is a square block of
  the new texture, read with no filtering between texels.
- **The maps for the lighting**: each texel is a *material* (the
  recipe's, by the game's colours: cloth, braid, leather, skin, hair),
  which says how rough and how much a metal it is; the normal map has a
  braid's roundness and a fine grain, nothing else. `charts.py` puts the
  charts' pictures into the one texture; `bake.py` is the texels' side of
  it (which triangle each texel lies on, what is known there).
- **The levels of detail**: the same surface with a quarter and a
  sixteenth of the triangles (each four the one they were made of), in
  the same layout, so that one texture serves all three.

## The recipe

`fighters.json`: for each fighter what a script cannot know.

- `materials`: what the game's texture is made of, for the lighting, by
  seed colours (`RRGGBB`, or `RRGGBB@T` for colour table T alone) and a
  kind (`body.py`'s `KINDS`); `chart_materials` says which of them a
  chart has (a head has no leather, whatever its lashes' black is
  nearest). `assemble` says how many texels each takes.
- `charts`, `larger`, `smooth`, `small_piece`: the default's do for a
  fighter built as Tatsumi is.

## The checks

    python tools/port_models/check.py NEW.glb --reference REF.glb --motions DIR [--lod L1.glb --lod L2.glb] [--report DIR]

Exit 1 on any failure. The limits are `limits.json`:

- **skeleton**: the 21 joints and the game's bind pose (the inverse bind
  matrices) exactly;
- **weights**: every vertex wholly on the bones, every bone the game's
  mesh moves still carrying vertices;
- **proportions**: each bone's piece (the vertices it carries alone),
  measured in the bone's frame, no longer, wider or deeper than the
  game's by more than 30 units;
- **silhouette**: posed on the game's bones through every 12th frame of
  every motion recorded, from the front, the side and above, against the
  game's own model posed the same: what lies more than 24 units outside
  the game's outline, or what of it is left uncovered by as much, may be
  40 pixels (4 units each) or 0.4% of the outline at most in any view; the
  worst is drawn, and the strays are told by the bone nearest them;
- **self-intersection**: triangles of one connected surface passing
  through another of it, at the bind pose and the six most bent poses, no
  more than the game's own model has (0.2% at the least);
- **watertight**: no hole the game's model has not: every edge only one
  triangle has lies along such an edge of the game's (within 16 units: a
  hem, a cuff), and no more triangles are turned over against their
  neighbours than the game's own are (a viewer that draws one side of a
  face leaves a turned one out: fable-03's gaps at the chest, the elbows
  and the neck were the joints' triangles turned over);
- **budgets**: 40,000 triangles (12,000 and 4,000 at the lower levels),
  ten million texels in all the file's pictures, and four fighters at
  once 160,000 triangles and 216 MB of textures with their mipmaps.

`tests/rebushido/fighter_check.py` holds the checks to models made up of
boxes: the same model passes, and an arm made longer, a vertex half
weighted, a bone's bind place moved, a budget exceeded, a surface
pushed through itself, one with triangles missing and one with
triangles turned over each fail.

## The file

What the port reads is unchanged (`src/port/gl/gl_fighter_file.h`). The
material names a normal map (`normalTexture`, tangent space, OpenGL's
green) and an occlusion-roughness-metal map (`occlusionTexture` and
`metallicRoughnessTexture`, one picture: R occlusion, G roughness, B
metal), in the colour's layout, which the lighting reads (ROADMAP 14);
the normal map is half the colour's size and the other a quarter (they
say little), and the material's metal factor is nought (the port reads
the map's own blue; a viewer that fails to read the map draws the
colour, not a black metal). The colour tables' picture (`rb_palette`) is
in the same layout at a quarter of the size, each texel the entry the
game's texture had there, so that the fight's fade-in and the tints
reach it as they reach the game's own. The cloths (`rb_cloth`) keep the
game's picture of them, cleaned, in a rectangle of the new texture. The
lower levels of detail are files of their own,
`tmp/mods/remade/lod/NAME.lod1.glb` and `.lod2.glb`, which the port does
not read yet.

## The next fighter

`build.py --fighter NAME`, then his `materials` in `fighters.json` (the
colours of his texture: `work/NAME/atlas_game.png`) and again from
`assemble`. `tools/blender/fighter_look.py MODEL.glb OUT.png bone03 0.36
front,three,side,back,top 600 lit +x -z` shows a part upright in its own
bone's frame, whatever the bind pose does with it.

## What was tried and left

Two passes before this one are in the branch's history (`2dc5ef3a7`):
the body painted anew by materials with folds in the normal map, a head
modelled whole (a face painted as shapes, ears, hair in locks) and
modelled hands. The owner's note of 2026-11-18 set them aside: too far
from the game's design. Their makers (`head.py`, `hair.py`, `hands.py`,
`facepaint.py`) are there if a piece is ever wanted made anew.
