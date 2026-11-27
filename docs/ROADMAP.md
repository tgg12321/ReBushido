# ReBushido roadmap

What is planned for [ReBushido](REBUSHIDO.md), in the order it will be done.
No dates.

1. **The foundation.** Done. The branch, the switch for each change, the
   ReBushido build beside the matching one, the place the settings live, the
   version text, a disc to play, and the checks.
2. **The options page and the settings' save.** Done. A page at boot
   where the player sets "Bugfixes" and the picture, and a file of
   ReBushido's own on the memory card that keeps the choices
   ([`REBUSHIDO.md`](REBUSHIDO.md), "The options page"). The game's save
   is never touched. "Bugfixes" is read by item 4; nothing reads
   "Graphics" until item 7.
3. **Scripted play tests.** Done, so that everything after it can be
   tried. The game run in an emulator with no window, by
   script, on the developer's machine only (`make playtest`,
   [`tests/play/README.md`](../tests/play/README.md)); and a build that
   boots straight into a chosen fight, for people and for agents
   (`make quickboot-disc`, [`REBUSHIDO.md`](REBUSHIDO.md)).
4. **The bug fixes.** Done for the bugs that are plainly mistakes and
   whose fix changes nothing for a player who never met them: fifteen,
   all behind the "Bugfixes" setting ([`REBUSHIDO.md`](REBUSHIDO.md),
   "The fixes"). The list they come from is in [`STATUS.md`](STATUS.md),
   "Suspected bugs and oddities of the original". Left to decide, one by
   one: the bugs whose fix changes how a fight plays, what the CPU does
   or what everybody has seen and heard (the same section, "Left as the
   original has it"); one of those that is fixed gets a setting of its
   own if it changes how the game plays by much.
5. **Release tooling.** Done but for the release itself: a patch file
   against the retail disc, and the tool that makes it and applies it
   (`make rebushido-patch`; [`REBUSHIDO.md`](REBUSHIDO.md), "Making a
   release" and "Applying the patch"). The first release, 0.1, is still
   to be made.
6. **The native port.** A build of the same source for a PC, beside the
   PlayStation one ([`PORT.md`](PORT.md) has what stands in the way,
   counted, and the stages). First a C equivalent beside each piece of
   assembly, checked by host tests and against the emulator; the
   PlayStation build keeps the assembly, and `decomp` is not touched.
   That first stage is done: the GTE is in C, with every instruction of
   the game's that reaches it, and so are Sony's libgte, the game's
   maths routines and the renderer, 119 of the 138 functions that are
   assembly (the other 19 need no C). Then Sony's libraries (graphics, sound, disc, pad, memory card)
   answered by PC code: the menu in a window, then a fight, then sound.
   The second and third stages are done too: the port is a program
   (`make port`) that reads the player's disc image and runs the game
   through its menus and whole fights, the emulator's game value for
   value. The fourth is done, with a window that nobody has seen yet
   (`make port-sdl`: SDL 2, for Linux and Windows, 64-bit programs, with
   sound and the PC's keys and controllers; [`PLAYING.md`](PLAYING.md)):
   the port draws, by the PlayStation's GPU written in C, into video
   memory of its own, and its picture is the emulator's to the pixel on
   the flat screens and but for three pixels in a thousand in a fight.
   The sixth is done too: the sound chip and Sony's sequencer in C, the
   chip's memory and registers the emulator's through a fight, and the
   same samples to a sound card in the window. Its memory card is a
   standard card image, and the card it writes for the options page is
   byte for byte the one the emulator's BIOS wrote. The opening movie
   plays, decoded in the port, with the sound the console has. Every row
   of the top menu has been driven into its mode and through a fight or
   two, in both sizes of the program, which found and put right the
   three last places where the original reads through a pointer it never
   set (PORT.md, "Memory"). The game's code stays C.
7. **The picture.** What "Graphics: Remastered" means on the port
   ([`PORT.md`](PORT.md), "Remastered"): the scene drawn again by the
   graphics card from the geometry the PlayStation threw away, with
   "Graphics: Original" still the console's picture pixel for pixel. In
   the owner's order:
   1. **The foundation and high resolution.** Done. The side channel
      that carries each corner's exact position and depth beside the
      PlayStation primitive; the decoder that hands the GPU's own
      command stream to a backend; OpenGL 3.3 core, drawing the scene
      at N times the console's resolution with sub-pixel corners,
      textures in perspective, no dither, true-colour mixing and the 2D
      layer crisp; proved with no window against the software GPU,
      pixel for pixel, at scale 1. It is in the window the player runs:
      the window takes a 3.3 core context for it and F2 changes
      picture while the game runs ("Remastered in the window").
   2. **Anti-aliasing.** Done, by drawing the scene two or four times
      over each way and averaging it down (`antialiasing`): exact with
      the game's order of painting, where multisampling would mix a
      half-transparent primitive's coverage twice along an edge, and it
      leaves the 2D layer exactly as crisp as it was ("Edges").
   3. **No pop-in, and more to see.** Done, by the stage's cells
      (`draw_distance`): the console draws a disc of 26,000 units and
      places its fog to end there, and `max` draws the stage to its own
      far edge with the fog pushed out four times as far, its colours
      untouched ("How far the stage is drawn"). The port hands the game
      primitive buffers of its own for it, and `max` is what the window
      starts with: it is the only setting that draws the same cells
      whichever way the camera looks, so nothing of the stage comes and
      goes as the camera turns. And the ground is drawn from **one mesh
      of the whole stage** built when the stage loads (`stage_mesh`, on
      with nothing said), so its detail never steps, its fog runs on
      instead of stepping in bands, a piece of ground between the camera
      and a fighter veils him again (by the console's own order, so that
      a terrace's face that runs back past him does not), and a picture between
      two frames has
      only to move the camera: all 38 stages are measured against the
      console's own tiles ("The ground as one mesh"). What stands on
      the ground -- railings, fences, walls, roofs -- is drawn from the
      same mesh (`stage_props_mesh`, on with nothing said), so it stands
      still between frames ("The stage's pieces as meshes"). Left of it: a
      stage's geometry was never meant to be seen whole, so its far edge
      shows where the ground stops.
   4. **The wide picture.** Done (`aspect`, and `16:9` is what the
      window shows with nothing said): the game never culls to the right
      or the bottom, so a wider view needs nothing of the projection to
      the right, and at the left only that the game stop culling a face
      at the console's own edge. The fight's counters, clocks and points
      are drawn at the picture's own edges, each group saying which edge
      it belongs to while it builds its primitives, while the menus, the
      select screens and the loading pictures stay four by three in the
      middle ("A wider picture").
   5. **A high frame rate**, 144 a second. Done (`frame_rate`): the
      game steps at the console's rate and computes what it always did,
      and the renderer draws the frame on show again with the frame
      after it — which the game has already worked out, since the
      console puts a frame up a blank after it is drawn — mixed in, each
      corner moved as the projection would move it ("More pictures a
      second"). Every primitive of the scene says which face of which
      thing it is a drawing of, so the two frames' primitives are
      paired by name and not by likeness, and every picture between
      them holds every primitive the frame on show held. It costs no
      input lag. A face one frame has and the other not is moved by
      its points, which its neighbours share. Left of it: `original`
      is still the default, and a camera cut shows the game's own
      picture.
   6. **Lighting and shadows**: the normals the mesh has and the
      primitive does not. Done for what is drawn from meshes
      (`smooth_light`, off until the owner has looked): the game's own
      lights worked out at each pixel of the ground's mesh, its normals
      smoothed below a crease, and of the fighters' models. The effects
      are placed where the projection put them and moved between frames
      (`effects`; [`PORT.md`](PORT.md), "Lighting", "The effects").

   On the PlayStation build only what is cheap: about 17 KB is left
   there before a fixed buffer has to move.

## After the picture: the owner's longer view (2026-11-16)

The port is the game in C on a PC, so larger changes to how the game
works are open where they serve these goals. Two limits hold throughout:
it must still play and feel like the original (the hitboxes, motions and
timing are the balance), and nothing is reinvented where a known way
exists. As the one ground mesh replaced the tiles, each part of the
picture is looked at again in that light: the camera, lighting and
shadows, the 3D terrain (structures that see through when they block the
camera), particle effects, skyboxes on the stages that were left dark.
Two of them are in: a night sky over the 28 stages open to it (`sky`), and
the stage's walls thinned where they stand between the camera and a
fighter (`see_through`), both on in the window and both the owner's to
confirm ([`PORT.md`](PORT.md), "The sky", "Seeing the fighters through
the stage").

8. **Online play with rollback netcode**, soon after the picture. What it
   asks of the port, and what work before it should keep in mind:
   - a game step that is deterministic and repeatable from its inputs
     alone (the port already runs the emulator's game value for value);
   - the whole game state saved and restored in well under a frame (the
     game's state lives in fixed memory, which makes a snapshot cheap);
   - the game step separated from drawing and sound, so a step run again
     draws and plays nothing;
   - the inputs read in one place per step.

   How ready the port is, measured, and the design: [`NETPLAY.md`](NETPLAY.md).
   Built: the save, load and hash of the state, with a self-test that
   rolls a whole game back and runs it again (`make port-rollback`); the
   game as a step the host calls; the state's files apart from the
   picture's, with a check; a step run again with nothing drawn or
   heard; the game's own link-cable mode between two ports over UDP; and
   rollback netplay of a whole session, GGPO's design in C, with desync
   detection by the state's hash; and netplay in the window: GGPO's time
   sync, loads as points both wait at, the sound kept out of the
   rollback by the sound card's queue, a line on the screen with the
   ping, the rollback and the delay, and sessions set up by `--host` and
   `--join` or the settings menu's Play online page (the build and the
   disc checked, the host's cards and a seed sent, both taken to versus
   select, no pause, rematch, the one who stays playing on alone), proved
   between two window programs on one PC with latency and lost datagrams
   (`make port-net`; NETPLAY.md, sections 7 to 13; PLAYING.md, "Playing
   online"). Left: play between two real PCs and on Windows, and the
   owner's calls of NETPLAY.md, section 13: the address a host listens
   at, NAT (port forwarding, hole punching or a relay), the draw distance
   online.
9. **Input remapping and a modern control overhaul.** Remapping is in:
   every button of each player from `rebushido.cfg`, several buttons on
   one key, and layouts to start from (`controls = original`, the only
   one so far; [`PLAYING.md`](PLAYING.md), "Remapping the keys and
   buttons"), on the port's one input point (`NETPLAY.md`, section 4).
   A modern layout is one more table in `src/port/window/keymap.c`, and
   waits on the owner.
10. **Mod support**: one framework for custom stages, textures, models,
    music and so on, loaded from folders beside the program. The
    replacement textures and fighter models planned for the picture are
    built as its first users, not as one-off features. Later perhaps new
    weapon types and motions, which need the hitbox and weapon tables
    read as data.
    The framework is in, with replacement textures of the 2D layer as
    its first user ([`MODS.md`](MODS.md)): `mods/` beside the program,
    a manifest and a load order per mod, one lookup by kind and key,
    textures keyed by what they look like, dumped and replaced at any
    size in the remastered picture. Since: the fighters' and weapons'
    models, music (a mod's `.ogg` or `.wav` in place of a piece of the
    disc's, following the game's starts, fades and stops, outside the
    rollback) and a stage's files (a mod's file read in place of the
    disc's sectors of it), and the ground for new weapons: a weapon's
    numbers from a mod's `weapons/NAME.txt` (its points, speed, power,
    models, first stance and the CPU's reach), standing in for one of
    the eight or a new slot on one (`--weapon 1=NAME`), a mod's motion
    file in place of the disc's, and the session's set-up holding both
    players to the same gameplay mods (`MODS.md`, "Weapons and
    motions"). Then (2026-11-25): **music by stage** (`music/stage07.ogg`,
    stage 7's own, under the stage select's row of music, heard online
    only where both machines have the same: "A stage's own piece"); **a
    fighter's textures by whole page** as the stage's are ("A fighter's
    pages"); the stage editor's next steps (arena textures up to 128 and
    20 of them, repeating; the arenas' page turned past five; an arena
    held to the stage prover by its own verdict: "What a stage editor
    needs next"); and **new weapons on the weapon select** (L1 and R1 on
    a weapon go round the mods' weapons made on it), each with a model of
    its own in the remastered picture and a motion file of its own, the
    pick in the game's state, the session refused where the mods differ,
    and the kanabo shipped as the example, its model made by a script
    ("A mod's weapon on the select screens"). Then (2026-11-26): **a
    weapon's sounds and dropped weapon** of another weapon's
    (`sounds =`, `dropped =`, format 3), its own model on a weapon lying
    loose, and **a move joined to another file's motion**
    (`tools/bbm_join.py`, lossless: the disc's streams packed again come
    back byte for byte), the kanabo given the broadsword's sounds and
    dropped weapon, and from the player's disc the broadsword's two first
    blows (`tools/weapon_kanabo.py --motions`). Left: a weapon's place of
    its own on the weapon scroll (room for one, and its art: the owner's
    look), whether a `weapon_id` of its own is wanted at all, a weapon's
    own recorded sounds; larger arenas and pieces; the weapons' pages
    ("What is not done yet", "What a stage editor needs next", "What is
    next").
11. **Menus and UI**: the menus redrawn (the Japanese options of the
    title screen in English), and the settings menu holding all of
    ReBushido's own settings.
    Both are in, on the port. The top menu is in English by a mod the
    program ships, drawn by the remastered picture with the US disc's
    own names of the modes ([`MODS.md`](MODS.md), "The English menu").
    The settings menu (F1, a controller's guide button, or Select on the
    options page, the top menu and the pause) holds every setting of
    `rebushido.cfg` and both players' buttons, stops the game at a
    frame's boundary while it is open, applies what it can at once, and
    writes the file keeping everything else in it
    ([`PLAYING.md`](PLAYING.md), "The settings menu"). The rest of the
    menus' 2D screens: the pause menu's, the menu after a fight's, the
    top menu's captions', the select screens' titles' and the group
    battle's lettering drawn again in a font by a second mod the program
    ships (`ui`), and every 2D texture of the screens made six times
    larger from the player's own disc by `tools/port_art.py` (the art
    mod, never committed); a texture or picture faded through its colour
    table or rewritten is drawn from its replacement, tinted
    ([`MODS.md`](MODS.md), "Fades"). The rest of the lettering is in a
    font too (now the port's one typeface, Shippori Mincho: PORT.md,
    "The port's lettering"): the narration, the
    training page, the memory card's messages, the names on the select
    screens and the group battle's, the stage card, the results, and
    ReBushido's options page's characters; and the title picture after
    the opening movie is the art mod's, at the window's resolution
    ([`MODS.md`](MODS.md), "The sharp menus", "The title picture").
    The screens no dump had reached are toured now (2026-11-22): the
    options' pages, the slash mode won and the ten endings
    (`tools/port_art.py`), so the art has them, and their lettering is
    in the font -- the options, the memory card's page and the key
    configuration, the slash mode's page and records, the memory card's
    question, the staff roll (its italic leant), REMATCH, the fight's
    digits 5 to 9; the story's stage card shows the art's map in the
    window (its pictures read at once); and `tools/port_coverage.py`
    lists what no mod replaces of what the tours draw (MODS.md, "What is
    covered"). Left: the screens no tour reaches (a group battle's
    result, a run against the CPU or a story played to its end, the
    memory card's pages with a card in), and lettering that stays the
    art's: the endings' pictures and the Japanese titles (the port's cut
    of the face has no Japanese), the best times' digits (the tours draw
    only some of the ten).
12. **Optimisation**, for a smooth and seamless game.
    Begun on the port's window ([`PORT.md`](PORT.md), "Where the time
    goes"): `--profile` says where each blank's time goes; the video
    memory a frame writes is sent to the card in one go; the card is no
    longer waited for at each picture; each picture between two frames
    is of the moment it is shown; the loads hurried through and the HD
    fighters' models read during them (`quick_loading`, "Quick
    loading"). Left: the players' build at `-O2` (the owner's call; its
    traces are the same), and whether 85 Hz is smooth on the owner's
    screen.
13. **Loading as short as possible, or none** (the owner, 2026-11-17):
    every loading screen of the window build cut to what the PC needs,
    if not taken out. The port reads the player's disc image from a PC's
    drive, so the console's waits on its CD drive need not be kept; what
    holds them is that the scripted play tests and the provers compare
    the port's frames with the emulator's, and that netplay's two
    players must load in the same frames. So quick loading is the
    window's (a setting, on), with the play tests' runs as they are.
    Done: `quick_loading` (item 12; PORT.md, "Quick loading").
14. **Shadow maps and lights of a stage's own** (the owner, 2026-11-17):
    the cast shadow is the game's one sun flattening the fighters onto
    the ground; lights that cast shadows of their own kinds (torches,
    lanterns, a flash where blades meet), on walls, steps and the
    fighters themselves, need each light's depth of the scene: a
    shadow map for the sun first, then point lights from a list per
    stage that a mod can add to. The owner (2026-11-18): the cast
    shadow's remaining jitter at rest is not fixed on its own but left
    to this item, whose shadow maps replace the flattened shadow; check
    the jitter is gone when it lands (the ground the shadow falls on,
    the in-between pictures' camera, the darkening's bounding box were
    the suspects). The owner (2026-11-18): a total overhaul as a modern
    game would have it -- several lights, shadows of everything on
    everything, reflections, the finish -- still Bushido Blade 2.
    **Done:** a, the sun's cascaded shadow maps (`shadows = map`, built
    in), the jitter gone with the mask; b, the light list's lights per
    pixel on the ground, the pieces and the fighters, the strongest four
    with cube shadows (`lights = on`), and a remade model's normal and
    material maps; c, reflections on the screen on the stages whose floor
    is polished, lacquered, glazed or wet, the sky the picture shows as
    the fallback (`reflections`); d, the tonemap that rolls off only the
    light the frame's lights add, so a stage keeps its own look
    (`tonemap`), ambient occlusion of the ground (`ambient_occlusion`) and
    the lights' bloom (`bloom`), all four on (PORT.md, "The lighting
    overhaul"). **Left:** the light files' placing and strength, and the
    normal map's bitangent on a remade model in a fight (PORT.md, "What is
    left of it").
15. **Input lag cut** (the owner, 2026-11-17): the press to the picture
    measured part by part, then cut where it is safe: the devices read
    as late as the game's step allows, run-ahead on the rollback's save
    and load, and the fewest pictures queued to the display.
    Done (2026-11-18): measured part by part (`port_bench.py --paced`,
    `--rate`; PORT.md, "The press to the screen, part by part"), the
    made-up display held to the card, and the keys read again just in
    time for the refresh a frame's first picture makes (`late_input`, on;
    "The input read late"); the bench's budget holds the read to the
    screen at 144 a second. Run-ahead already uses the rollback's save
    and load for its second frame, and a third would show guesses; none
    is queued with `low_latency`. Left: the card's share of the first
    picture (the record's own frame drawn under it), and what a real
    screen and Windows' compositor add, which only the owner's screen
    shows.
16. **Three kinds of stage at the select screen** (the owner,
    2026-11-18): **Classic**, the disc's stages with only what the engine
    gives (resolution, the wide picture, draw distance, frame rate, and
    the new lighting while the picture is Remastered: F2 to Original
    gives the console's picture and the console's light); **Remastered
    Classic**, the same stages, the same collision, dressed (lights,
    textures, grass, skies, water, particles); and **Custom arenas**, any
    stage a mod brings. The tier is part of the stage chosen: online,
    both players play the same stage in the same tier (no mixing). The
    stage select keeps the original grid with a Classic/Remastered
    switch per stage and a page of its own for the custom arenas.
    Done ([`PORT.md`](PORT.md), "The stage select's kinds of stage";
    [`PLAYING.md`](PLAYING.md), "Kinds of stage"): Square on a stage
    page switches the stage under the cursor, Select opens the arenas'
    page (each arena's name and picture: its file's, or one the compiler
    draws of the scene), and the choice is the game's state, rolled back
    with the stage and the same on both machines of a session; the
    stages start as `stage_kind` says (the host's in a session), and the
    modes with no stage page fight in it. What a kind switches is one
    question, `port_StageIsDressed()`: Classic has none of the stage's
    own lights, none of the stages-hd pages and no sky; everything the
    engine and the renderer improve is in both. The window lays the
    line and the page over the picture, so the console's screen is
    untouched (the play tests). Left: item 17's dressing reads the same
    question; the owner's look at the page.
17. **Remastered Classic stages**: a mod kind for dressing nobody
    touches (foliage drawn by instancing and stirred by wind, the
    fighters and the slashes; water; dust, leaves, embers; scenery
    meshes that keep the original silhouette), with a check that
    nothing that looks solid stands where the collision grid has none;
    then a pass over all 38 stages. Builds on item 14's lights.
    **Done** (the owner, 2026-11-21: "go ahead"; [`PORT.md`](PORT.md),
    "The dressing"; [`MODS.md`](MODS.md), "Dressing"): the mod kind
    `dressing/` -- grass, flowers, reeds and bushes by instancing, swayed
    by the stage's wind, pushed by the fighters and by a blade swept near
    the ground; water reflecting the sky and rippling where a fighter
    wades; dust from steps, leaves, petals, snow, embers from the flames,
    fireflies; OBJ scenery meshes, alone or in a piece's place within its
    box -- grown from the ground's own mesh and coloured by the stage's
    own textures, lit by the stage's light, the sun's maps, the lights and
    the time of day, the picture's alone (`dressing`, on); the check
    (`tools/port_dressing.py check`, a test) that nothing solid-looking
    stands where the collision grid lets a fighter walk; 27 stages
    dressed by the program's own mod, procedural, nothing downloaded or
    taken from the disc; the bench's `ra1-dressed`. The second pass (the
    owner, 2026-11-25: "work whatever items you can without my
    intervention"; PORT.md, "The dressing", "The second pass"): all 38
    stages dressed -- rain with splashes and puddles that mirror the stage
    and the fighters (29, 30), candle smoke (2), shafts of light through
    the halls' doors and windows with motes in them, from the stages' own
    lights (0, 1, 32, 34; motes on 35, 36), sea spray (21), mist; water
    with its depth's colour, shore foam, the post's reflections and a
    stronger Fresnel (5, 11, 22), and the stream under the lost bridge
    painted across the ground as flowing water (14); the particles drawn
    in the ground's depth, which they had failed, and larger, with a glow
    on the embers; three gravestones in the cemetery's pieces' place;
    stage 37, link mode's castle rooftop, reached by `--fight 37` and
    dressed; the bench's `ra1-rain`. The third pass (2026-11-25; PORT.md,
    "The dressing", "The third pass"): the water's reflections seen at
    last -- a window's tapped picture was the game's own frame, which
    with pictures between frames is never shown and had none of the
    post's passes; the passes lost the view of the half on show (a
    player's picture at a cut too) -- and the water given a plane of its
    own to mirror the fighters in; the shafts held by the fighters'
    depth; the foliage casting into the sun's nearest map; gulls over the
    ship; the ferry dock's bollards and the orange trees' trunks as
    meshes. The fourth pass (2026-11-26; PORT.md, "The dressing", "The
    fourth pass"): the grass, flowers, reeds and bushes casting into the
    lights' cubes as into the sun's map, swaying there; a mist drawn as a
    volume the fight camera looks through, lying on the ground under each
    place in banks that drift with the wind, held by the ground's and the
    fighters' depth, lit by the time of day (5, 6, 12, 14, 15, 31); the
    bamboo thicket's seventeen culms as round culms with nodes, each set
    at its piece's own column found from the piece's faces, the piece's
    leaves kept, gone when the game cuts the culm down; the shafts' hold by
    the fighters' depth, which overshot by the tiles' scale, mended. The
    skies (the
    owner, 2026-11-25: "I want striking skyboxes. With beautiful
    volumetric clouds, sunsets and moon shots etc"; PORT.md, "The sky"):
    the air worked out as sunlight scattered through it, with each time's
    palette laid over it (the second pass, for the few degrees of sky a
    fight's camera sees): a sunset's gold and flame under the sun, magenta
    and violet above, the Earth's shadow opposite; dawn's peach, rose and
    lilac; the day's deep blue, the far clouds thinned to a clear band
    over the horizon; heaped clouds marched through as a volume, lit
    through themselves by the sun or the moon, silver-lined, lit hot from
    beneath by a low sun, drifting with the stage's wind, taller by day,
    a high veil above them catching the sunset, a ceiling of cloud where
    it rains; the sun's disc low over a sunset with its bloom and rays
    fanning out, and a large moon with its craters and corona; stars and
    the Milky Way; sharp ranges low on the horizon, their ridges caught by
    the light on its side, and on the five stages by the water the sea
    beyond, the sky in it and the sun's or the moon's path glinting on
    it, running into the ground's fog; each time's own, a mod's picture of
    a sky still its stage's; procedural, 0.67 ms of the card's frame (a
    quarter of the clouds marched each picture, the rest the last
    picture's).
18. **Custom arenas**: a stage authored in Blender as a render mesh and
    a collision mesh with each face's surface (firm, sand, wet,
    barrier), compiled into the game's ground (`MODS.md`, "Stages");
    the program's stage tables (starts, fog, music, light, sounds) as a
    mod's data; a validator (no hole to fall through, nothing out of
    bounds, a headless fight of the CPU on it); netplay's set-up holds
    both players to the same arena. The game's own limits (32 by 32
    cells of 2,000 units) first.
    Done for the game's limits ([`MODS.md`](MODS.md), "Custom arenas"):
    the ground file read and written back byte for byte (all 38,
    [`formats/GROUND.md`](formats/GROUND.md)); `tools/arena_compile.py`
    from a glTF/OBJ scene (render mesh, collision mesh with each face's
    surface by its material, starts and posts by name) and `arena.txt`
    (starts, light, fog, music, sounds, the CPU's chains, the remastered
    light's key) into one `.arena`; the port's `--arena NAME` puts it
    over a host stage (25 unless it says), the game asking the port
    where it reads each table; `tools/arena_validate.py` (limits, a walk
    of every reachable point for a floor, the starts, what is met
    against what is drawn, and `--fight`: CPU against CPU on both sizes,
    the same trace); netplay's set-up turns away a guest whose arena
    differs (`make port-net`). The sample, a stone courtyard made in
    Blender, passes. The stage select's page for the arenas (item 16;
    2026-11-25): a page of the stage select in its own look, six arenas
    to a page, the picture in the map's frame, in every mode that has
    stage pages, online (a guest without the host's arenas told so:
    protocol 17) and in replays. Left (MODS.md, "What a stage editor
    needs next"): arenas larger than their host's room, pieces, more
    than 32 arenas.
19. **The fighters' models overhauled** (the owner, 2026-11-18: art made
    entirely by AI): each fighter remade with faces and hands, faithful
    to his original design and colours, and held by checks to the
    game's skeleton, proportions and outline, since the hitboxes are
    the balance. The pipeline and its checks are in
    (`tools/port_models/`, PORT.md "The fighters remade"): the game's
    motions from the port (`--gl-fighter-motion`), Blender driven
    headless for the new pieces and the reviews, the checks that fail
    (skeleton, weights, proportions, silhouette through every motion,
    self-intersection, budgets; `tests/rebushido/fighter_check.py`),
    the file with levels of detail and normal and
    occlusion-roughness-metal maps. The owner's rule (2026-11-18): the
    game's own design, cleaned, not a new one. Tatsumi is the first: the
    game's mesh made smooth (sixteen triangles for one, bending as the
    game's), its texture cleaned and sharp in a layout of its own.
    **The joints blended** (2026-11-25; PORT.md, "The joints blended"):
    the renderer bends every model's shoulders, elbows, waist, hips and
    knees as one skin by dual quaternions, the head, hands and feet
    rigid, `joints = blended|rigid`. Left: the clipping, which is the
    game's own; the other fighters (`tools/port_models/README.md`); the port
    choosing among the levels of detail.
20. **Time of day and many lights** (the owner, 2026-11-19: the sun must
    not be every stage's only light): each stage gets a time-of-day
    choice on the stage select (dawn, day, dusk, night, and the stage's
    own as built in), which turns the sun's angle and colour, the sky,
    the fog and the ambient, and so the shadows' length and direction;
    the host's choice holds online, as the stage does. More of the
    stage's own lights (lanterns, torches, windows, street lamps) placed
    in the lights mod, several casting shadows at once. Night is its own
    approach, not a dark day: the moon as a weak, cool, soft key; the
    shadows mostly from the near lights (torches' and lanterns' cube or
    spot shadows, flickering with the flame, several per fighter);
    darkness between the lights that the fighters walk through; the
    bloom and the tonemap set for the dark. The stages that are night
    in the original stay night as built in.
    **Time of day done** ([`PORT.md`](PORT.md), "The time of day";
    [`PLAYING.md`](PLAYING.md), "The time of day"): L1 and R1 on a stage
    page, `time_of_day` for the rest, the host's online (protocol 6);
    the sun's way and colour, the maps along it, the ambient, the fog,
    the sky, the fighters, the bloom and the stage's lights (their files'
    `times`) all follow it; night a weak cool soft moon with the stage's
    lights brightened. Left: the owner's eye on each time's numbers.
21. **Match rules for 1v1** (the owner, 2026-11-20): beside the game's
    own one fight, a **Stock** match (each player has N lives; a kill
    takes one; the match ends when a player has none, then back to the
    stage select) and **First to N** (rounds until one player has N
    wins, then the stage select). Chosen before the fight (versus, Vs.
    CPU, online: the host's rule holds), shown on the HUD (lives or the
    score), with the game's own kill and round flow inside each fight.
    **Done** ([`PORT.md`](PORT.md), "Match rules"; [`PLAYING.md`](PLAYING.md),
    "Match rules"): Circle and Start on a stage page (`match_rule`,
    `match_count` for where it starts), lives or wins as pips over the
    fight, a card of the score between fights and of the winner at the
    end, then the stage select with the fighters kept; versus the CPU a
    match per opponent; a draw a life from both in Stock (none on both
    last lives), no point in First to N; the host's online (protocol 8),
    the score in the state. Left: the owner's eye on the cards and the
    strip.
    **Crew** (the owner, 2026-11-20: "select 5 fighters and five weapons
    and those are essentially your five lives. First to kill all the
    opponents' fighters wins"). **Done** ([`PORT.md`](PORT.md), "Match
    rules", "Crew"; [`PLAYING.md`](PLAYING.md), "Crew"): the battle type
    chosen on its own page before the first select (Crew N, 2 to 8, no
    more than the grid's characters); each crew picked place by place on
    the game's own select, no character twice (the CPU's too); the loser's
    fighter out and the next in, the winner staying with the wounds he
    ended with (Stock keeps them too; First to starts everyone fresh); a
    draw one out of each, both crews out a drawn match; small pips over
    the fight, the crews in full on the cards between fights; the match's
    end back to the Main Menu (online both players; a room's match to the
    room). Protocol 12. Left: the owner's eye on the pages and cards
    (`tmp/crew/`, `tmp/crew2/`).
22. **Online lobbies of more than two** (after the seamless online
    menus): a room holds several players and spectators; spectators
    watch a match live (the session's inputs, rolled forward, no
    rollback of their own); a **rotation** of three (A+B, B+C, C+A, the
    winner staying or a fixed turn, as the host sets); players in a room
    may also start their own matches beside it.
    **Done** ([`NETPLAY.md`](NETPLAY.md), section 16; [`PORT.md`](PORT.md),
    "Rooms of several"; [`PLAYING.md`](PLAYING.md), "Rooms of several"):
    rooms of up to eight at the lobby (version 2; version 1 clients
    refused), the rotation the server's (a fixed turn or the winner
    staying), each match a session of two met by a ticket and ended where
    it is decided (protocol 10), matches beside the room's, spectators who
    take the match's feed from the lobby (its fight's start as a replay
    keeps it, then its players' inputs and hashes) and run it with no
    rollback, a member coming in mid-fight included; the room's page in
    the online pages; with it the room list's ping and leaving an online
    fight with F1 or Escape twice. Proved on one PC by six headless
    programs and by three hidden windows, every match's hashes the same
    on its players and spectators. Left: a room of real players on real
    networks; a spectator sees nothing of a match's menus before its
    first fight; the owner's eye on the room's page.
23. **Replays, saved by themselves**: every fight's inputs, seed, cards
    and settings that the state depends on, a few KB a fight, played
    back by running the game again (the port is deterministic); a
    replays page to watch, skip, slow and export; held by a test that a
    replay's state hashes match the fight's own. Online matches too.
    **Done** ([`PORT.md`](PORT.md), "Replays"; [`PLAYING.md`](PLAYING.md),
    "Replays"): every fight, online ones from either machine, kept in
    `replays/` (`replays`, `replays_keep`, `replays_megabytes`) as its
    start state (what the disc holds left out, deflated) and every input,
    about 220 KB a fight, its inputs a few hundred bytes; the settings
    menu's Replays page plays one with pause, a frame, slower and faster,
    from the start and to the kill; `--replay`, `--replay-check`; every
    hash held, and `tools/port_replay.py` and `tools/port_net.py session`
    play every recorded fight back the same. A replay's film (2026-11-25;
    [`PORT.md`](PORT.md), "A replay as a film"): Square on the Replays
    page, or `--replay FILE --export OUT`, plays it again in another run
    with no window shown and writes the fight as an .mp4 (Windows' own
    H.264 and AAC encoders) or an .avi (the port's own Motion JPEG, on
    Linux), a frame a blank, the sound the fight's own; `tools/
    port_film.py` holds its frames to `--tap-window` and its sound to
    `--wav`. Replays across builds (2026-11-26; [`PORT.md`](PORT.md),
    "Across builds: the lead-in"): each file keeps the run from power on
    to its fight (its setup, cards, every input, the console's hash every
    second), and a build of another core plays the session again from its
    own power on to the fight, every console hash held, stopping where its
    game went otherwise; an online fight's too (2026-11-26), from its
    session's own power on, with what the game reads of the session (its
    rules, pads and seed). Left: a run begun with a fight chosen.
24. **2v2** (longer term): four fighters in one fight, two teams; a
    button retargets to the other enemy; online with four players
    (rollback of four, the bench's four-fighter case as the budget).
    **Done but for four PCs** ([`PORT.md`](PORT.md), "Two on two";
    [`PLAYING.md`](PLAYING.md), "Two on two"; [`NETPLAY.md`](NETPLAY.md),
    section 18), the owner's "go ahead" of 2026-11-21: the game's two
    slots hold four, a team a slot, each fighter's record, model, points
    and replay frames put in as his step comes, each pair of enemies'
    globals kept (`src/port/duo.c`); each fighter a character and
    weapon of his own (2026-11-25: the grid again for each team's
    second, the third and fourth pads' with four players; a model no slot
    has loaded beside the game's, its textures in a second bank of the
    port's video memory, its motions and win pose going with him; of his
    first's model a copy; protocol 18, NETPLAY.md, section 20); R2 retargets (a fallen target is left by
    itself, the CPU turns to a nearer enemy), every blow and kill between
    enemies, none between team mates, a lock or a throw between two; the
    side view over all four; a team out loses, the game's own fall,
    replay and winner's view of the last to fall and his enemy; the rule
    page's Fighters row (one on one, CPU partners, four players) with
    Single, Stock and First to, `teams`; the third and fourth pads (the
    input, replays, the window's bindings and controllers); a light band
    and tags over the heads; the fighters' own models for all four.
    Online with the CPU's partners (protocol 13), in sessions and rooms,
    spectators watching. A rollback of the whole window at four
    7.9 ms against 5.8 at two. **Four players on four PCs** (2026-11-25,
    NETPLAY.md, section 18; PLAYING.md, "Four players online"): protocol
    15 and lobby version 4 (version 3 clients refused); the host's PC in
    the middle, passing each guest's inputs on (each guest needs one way,
    to the host, found by section 14's ladder: a link and a socket each);
    an input of four pads a frame, each guessed and rolled back on its own;
    the meeting of three guests (`--players 4`), or two players on each of
    two PCs (`--players 2x2`); rooms of four at the lobby (a seat a guest),
    rooms of several whose matches are of four, the spectators' feed of
    four pads; the online pages' *Room for* and Quick Match. A guest of four
    PCs who leaves the fight (or goes silent) gives his fighter to the CPU,
    decided by the host at one frame and passed on as his pad's input
    (protocol 16; NETPLAY.md, section 19), and the others play the match
    out; the host's leave ends the session for all. Since 2026-11-25 the
    same between the fights of a match of several (the card goes on, the
    fighter the CPU's from the next fight's start), a killed guest's
    program proved to be taken after its 15 seconds of silence
    (`four-silent`, `four-between`, Linux and Windows), and a mod's whole
    pages laid over the partners' textures in the second bank too. Proved
    headless on this PC, by address and through a local lobby (a punched
    hole, the relay), in a room of several with a spectator and a leave,
    on Linux and Windows, every hash the same on all; replays of the fight
    of four played back. Each partner sounds as himself (2026-11-25,
    protocol 20): his voice, weapon's and class's banks of sounds in a
    second bank of the port's sound memory; since 2026-11-26 what he
    throws or drops sounds as his too, not as whoever is in his slot
    (PORT.md, "A thrown thing's sounds"), and a room of several whose
    matches have several fights has its leave (`rooms-four-stock`: a
    leave in the card between a match's fights, the CPU's from the next
    fight's start; NETPLAY.md, section 19). Left: four real PCs and
    players; the
    owner's eye on the band, the tags and the pages (`tmp/2v2/`) and on
    fights of four characters (`tmp/2v2-chars/`).
25. **A ladder and Elo** (longer term): ranked matches through the
    lobby server, an Elo (or Glicko) rating per player, a ladder page;
    identities, anti-cheat by replay checks (item 23), and the
    server's own care for the data it keeps.
    **Done** ([`NETPLAY.md`](NETPLAY.md), section 17; [`PLAYING.md`](PLAYING.md),
    "Ranked"; `tools/relay/README.md`): lobby version 3. An identity is an
    Ed25519 key the program makes on first use (Monocypher, now in every
    program), kept in `rebushido-identity.key` beside the settings, with a
    copy a row away; names of 3 to 15 letters, none folded alike to
    another's. Every name, queue and result is signed over the lobby's
    nonce. The queue pairs by rating and round trip, the same setup code,
    never one address, the same pair at most three times a day; the match
    is a room's match under the ladder's rule (First to 3) on a stage the
    lobby draws. Both programs report the winner with the match's digest
    (BLAKE2b of its every final input and state hash); agreeing reports are
    rated by Glicko-2, a leaver loses, anything else is held disputed with
    both sides' replays uploaded for `--replay-check`. The store is SQLite
    in the service's own state folder (`StateDirectory=`), WAL, a backup a
    day with seven kept, holding keys, names, ratings, results and disputed
    replays for set times. Online Play's Ranked page and the ladder's page
    (the top, around you, your matches, everyone's). Proved by
    `test_relay.py` (identities and forgeries, names, pairing, results,
    disputes, leavers, limits, a restart, the backup, Glicko-2's worked
    example) and `port_net_lobby.py`'s `ranked`, `ranked-win` and
    `win-ranked` (programs and windows queued, paired, played, rated,
    disputed, a leaver, a forged report refused). Left: deploying the
    lobby (the owner's), real players on real networks, a tool to settle a
    dispute (by hand in SQL now), seasons.
26. **A training mode** (the owner, 2026-11-26): a fight against a
    dummy for learning the game, with nothing of the fight changed. An
    input display (both players' presses, by frame); the hit areas, the
    kill and wound zones and the weapons' reach drawn over the fighters
    in the remastered picture; a dummy that stands, guards, or plays back
    what the player recorded for it (several slots, one picked at random
    if asked); a moment saved and put back at a press (the rollback's own
    save and load, `src/port/state.c`) to drill it; slow motion and a step
    of one frame. A page of its own on the top menu, every option as a
    setting of `settings_list.h`.
    **Done** ([`PORT.md`](PORT.md), "Training"; [`PLAYING.md`](PLAYING.md),
    "Training"): the main menu's Training is a page (Training, Classic
    Training, Training Options, Back); a versus fight with no rule that
    starts both fighters again two seconds after a kill, left by the
    pause menu; the dummy stands, guards (the CPU's answer to an attack
    about to land, pressed on its pad) or plays back one of four slots
    (or one at random) recorded by Select and Cross with the player's
    pad on the dummy's side; Select and L1/R1 keep and put back the
    moment (state.c's save and load); Select and L2 slow motion,
    Triangle the freeze, Square a step of one frame; the input display
    and the hit areas (the body's 22 spheres and their cores, the
    blades) over the fight, at the window's resolution; six settings of
    the settings menu's Training page. Proved by `tools/port_training.py`
    on both sizes. Left: a guard that weighs the distance, the blade's sweep drawn as the hit test
    tests it, the owner's eye on `tmp/training/`.
27. **The CPU's difficulty** (the owner, 2026-11-26): a few levels beside
    the original's CPU, which stays as it is. The owner: the CPU must not
    "cheat"; it plays as a real player would, only making better
    decisions. So a CPU of any level reads only what a player could see
    (where the fighters are, what they are doing, their wounds), never the
    other's pad before it acts nor the fight's future; it answers after a
    human's time to react (a level may shorten it, never to nothing) and
    presses as a pad could. What a higher level changes is its choices:
    distance, when to strike and with what, when to guard or back away,
    reading the other's habits during the match. It plays through the
    same inputs as a player, so a fight against it replays and runs online
    as any other. A level may be learned from replays (item 23) of real
    players; a choice on the select screens and in the settings.
    **Done on the port** ([`CPU.md`](CPU.md); [`PLAYING.md`](PLAYING.md),
    the settings menu's CPU): a study of the original (it reads no pad,
    but it sees the second place's frame before it is drawn, answers with
    no delay, times its guard to the blade's frame from the motion's data
    and reads which button began the attack and the move asked for and not
    begun; measured, about three in ten of the attacks it meets answered
    in under a fifth of a second), and the port's CPU, `src/port/cpu.c`:
    Novice, Adept, Veteran, Master (`cpu` setting, `--cpu`), deciding from
    what it saw of the opponent 6 to 9 frames back (never under 5), by a
    pad's buttons, its memory and dice in the game's state (the level in
    the gameplay switches: replays and sessions keep it). Measured CPU
    against CPU, 96 fights a pair (`tools/port_cpu_bench.py`): against the
    original 34%, 46%, 61%, 67%; each level beats the one below (CPU.md,
    "Measuring the levels"); a fight is the same bytes on both sizes and
    passes the rollback self-test. Left: a choice on the select screens;
    the port's CPU in fights of four and for a takeover; walking round
    obstacles (it walks straight and sidles when blocked); dirt, thrown
    weapons and guns; levels learned from replays of real players; a
    person's test of how it feels to fight.
28. **A resync button** (the owner, 2026-11-26, instead of a desync
    report): when an online session finds the machines' hashes parted, a
    press (any player's, or offered on its own) puts the host's state on
    every machine (`src/port/state.c`'s save, sent whole) and the fight
    goes on from there, rather than the session ending. The resync is in
    the replay, so a replay of a resynced fight still plays.
29. **A replay theatre and photo mode**: a replay played with a free
    camera, paused, slowed or stepped, from any frame; the picture
    without the HUD; a still saved at full size, and a clip saved as a
    video (MP4) or GIF. Also a photo mode in a fight (paused, a free
    camera, the time of day changed), to show the stages' skies and
    dressing (items 17, 20).
30. **Stats** (the owner, 2026-11-26; no achievements): a player's record
    kept by the program: fights and matches won and lost, by fighter,
    weapon, stage, mode and opponent (CPU levels, online names); kills
    by kind and place of the blow, wounds given and taken, the longest
    run of wins; a match history with each fight's replay. A page of its
    own; online the ladder (item 25) keeps its own ratings beside it.
31. **A local tournament**: a bracket of up to eight players or CPUs
    (item 27's levels) on one machine or in a room (item 22), single or
    double elimination, under the match rules of item 21, the bracket
    shown between matches and its result kept in the stats (item 30).
32. **The sound remastered**, as a setting (the original's sound stays):
    a reverb of each stage's own (the dojo, the cave, the open), sound
    placed for headphones (HRTF), and better resampling of the game's
    samples than the chip's own.
