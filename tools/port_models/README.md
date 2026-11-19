# The fighters' models remade (ROADMAP 19)

The pipeline from the player's disc to a checked `remade` mod, and the
checks that hold a new model to the game's own. The why and the numbers
are in `docs/PORT.md`, "The fighters remade"; this is how to run it.

    python tools/port_models/build.py --fighter M101

runs every step for one fighter (`M101` is Tatsumi; `build.py`'s `ROSTER`
names the others). `--steps` takes some of them:

| step | what | where it writes (the main checkout's, git ignores them) |
|---|---|---|
| `reference` | the game's own model (`fighter_export.py`) and the hd mod's at two levels (`fighter_hd.py`) | `tmp/models/ref/` |
| `motions` | the game's motions, as the port poses them (`motions.py`, in WSL; `--gl-fighter-motion`) | `tmp/models/ref/motions/` |
| `prepare` | the pieces to remake, in their bones' frames, and the hands' colours (`prepare.py`) | `tmp/models/work/NAME/` |
| `parts` | the new pieces, modelled and baked in Blender (`tools/blender/fighter_parts.py`) | the same |
| `assemble` | the file, its levels of detail, its maps, the face's relief (`assemble.py`, `face.py`) | `tmp/mods/remade/` |
| `check` | the checks; the build fails with them (`check.py`) | `tmp/models/work/NAME/check/` |
| `renders` | the game's, the hd mod's and the new model side by side (`tools/blender/fighter_scene.py`) | `tmp/models/renders/NAME/` |

Blender is the portable 4.5 in the main checkout's `tmp/tools/`
(`tools/blender/README.md`), or `$BLENDER`. The Python steps need numpy
and Pillow (Windows' Python, as `fighter_hd.py`); `motions.py` runs the
headless port in WSL (`python3 tools/port_gl_build.py` first).

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
- **budgets**: 40,000 triangles (12,000 and 4,000 at the lower levels),
  ten million texels in all the file's pictures, and four fighters at
  once 160,000 triangles and 216 MB of textures with their mipmaps.

`tests/rebushido/fighter_check.py` holds the checks to models made up of
boxes: the same model passes, and an arm made longer, a vertex half
weighted, a bone's bind place moved, a budget exceeded and a surface
pushed through itself each fail.

## The recipe

`fighters.json`: for each fighter the pieces made anew (`replace`), the
hands' colours where the game's texture is not to be read for them, and
the face's features as texels of the game's atlas (where its painting
has the eyes, the nose's tip, the mouth and the chin).

## The file

What the port reads is unchanged (`src/port/gl/gl_fighter_file.h`). The
material also names a normal map (`normalTexture`, tangent space, OpenGL's
green) and an occlusion-roughness-metal map (`occlusionTexture` and
`metallicRoughnessTexture`, one picture: R occlusion, G roughness, B
metal, half the colour's size), in the colour's layout: the port does not
read them yet. The lower levels of detail are files of their own,
`tmp/mods/remade/lod/NAME.lod1.glb` and `.lod2.glb`, which the port does
not read yet either.
