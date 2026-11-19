# Blender, for the fighters' models

The portable Blender (4.5 LTS) is in the main checkout's gitignored
`tmp/tools/blender-4.5.16-windows-x64/`, from blender.org (SHA-256 checked
against the release's list). Every script here runs it with nothing shown:

    blender --background --factory-startup --python tools/blender/<script>.py -- ARGS

- `fighter_view.py IN.glb OUT.png`: the model from the front and the side.
- `fighter_scene.py --model A.glb [--model B.glb ...] [--motion M.motion.txt]`:
  models side by side on the game's skeleton, played through the game's
  own motions, rendered from several views (`tools/port_models/`).
- `fighter_unwrap.py IN.json OUT.json`: a surface cut along seams and
  laid flat into a rectangle, every island at one scale (the remade
  fighters' texture layout, `tools/port_models/README.md`).
- `fighter_look.py MODEL.glb OUT.png BONE ...`: close pictures of one
  part, upright in its own bone's frame.
- `fighter_rebuild.py IN.glb OUT.glb [--levels 2] [--crease 50] [--texture BIG.png]`:
  a fighter at a higher definition from the game's own, the first way it
  was done (docs/PORT.md, "The fighters' own models").

## The hd mod: no Blender

The whole hd mod, every fighter and every weapon, is one command, and
needs no Blender:

    python tools/fighter_hd.py --image "Bushido Blade 2 (USA).cue" [--out tmp/mods/hd]

Its recipe, the numbers for each model, is `tools/fighter_hd.json`. It
does in Python what `fighter_rebuild.py` does in Blender (weld, crease,
subdivide, texture four times larger), and more that Blender's glTF
cannot carry: each vertex keeps the game's own sum of bones (its terms),
so the rebuilt fighter bends at a joint exactly as the game's mesh does,
subdivided; the surface is fitted to pass through the game's vertices;
and the colour tables' picture is kept, so the game's tints and fades
reach the model.

## By hand in Blender

For a model made or changed by hand, Blender is the tool, and the file
goes back through `fighter_conform.py`:

    python tools/fighter_export.py --models 3 --out OUT        (or a file of the hd mod)
    blender ... (edit OUT/M100.glb, keep the armature and its bone names, export glTF)
    python tools/fighter_conform.py EDITED.glb mods/mine/models/M100.glb --original OUT/M100.glb

A conformed file is drawn by the linear blend of its weights (Blender has
no way to keep the terms), keeps the original's colour tables' picture,
and so takes the game's tints as long as the texture keeps the atlas's
layout. `fighter_texture.py` makes an atlas larger for such a model.
Weapons are `fighter_export.py --weapons N` (`W00` to `W57`), every vertex
on bone 0.

Nothing these write goes into the repository: it is made from the
player's disc.
