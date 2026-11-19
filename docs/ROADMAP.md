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
    motions"). Left: the scene's textures by whole page, music by stage,
    the stage editor's next steps (`MODS.md`, "Stages"), and new weapons
    on the select screens with motions of their own ("Weapons and
    motions", "What is not done yet").
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
    font too (Archivo at its widest, and Marcellus): the narration, the
    training page, the memory card's messages, the names on the select
    screens and the group battle's, the stage card, the results, and
    ReBushido's options page's characters; and the title picture after
    the opening movie is the art mod's, at the window's resolution
    ([`MODS.md`](MODS.md), "The sharp menus", "The title picture"). Left:
    the lettering of screens no dump has reached (the setup mode's
    pages, the slash mode's, the endings), each a block of
    `tools/port_ui.py`'s table away.
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
    the suspects).
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
    Blender, passes. Left: the stage select's page for the arenas (item
    16; MODS.md, "What a stage editor needs next"), arenas larger than
    their host's room, pieces, textures past 64 by 64, the
    ground-as-one-mesh proved on an arena.
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
    occlusion-roughness-metal maps. Tatsumi is the first: new hands
    closed round the grip, the face in relief under its painting, the
    body held inside the game's outline. Left: the other fighters; the
    face's texture and a head modelled whole; folds and seams in the
    normal map; the port reading the maps (ROADMAP 14) and the levels of
    detail.
