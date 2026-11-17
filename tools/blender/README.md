# Blender, for the fighters' models

The portable Blender (4.5 LTS) is in the main checkout's gitignored
`tmp/tools/blender-4.5.16-windows-x64/`, from blender.org (SHA-256 checked
against the release's list). Every script here runs it with nothing shown:

    blender --background --factory-startup --python tools/blender/<script>.py -- ARGS

- `fighter_view.py IN.glb OUT.png`: the model from the front and the side.
- `fighter_rebuild.py IN.glb OUT.glb [--levels 2] [--crease 50] [--texture BIG.png]`:
  a fighter at a higher definition from the game's own (docs/PORT.md,
  "The fighters' own models").

The whole of one fighter, from the player's image to a mod's file:

    python tools/fighter_export.py --models 3 --pose M100.pose.json --out OUT
    python tools/fighter_texture.py OUT/M100.png OUT/M100_x4.png
    blender ... fighter_rebuild.py -- OUT/M100.glb OUT/M100_blender.glb --texture OUT/M100_x4.png
    python tools/fighter_conform.py OUT/M100_blender.glb mods/hd/models/M100.glb --original OUT/M100.glb

Nothing these write goes into the repository: it is made from the
player's disc.