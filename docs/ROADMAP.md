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
      and a fighter veils him again, and a picture between two frames has
      only to move the camera: all 38 stages are measured against the
      console's own tiles ("The ground as one mesh"). Left of it: a
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
      input lag. Left of it: `original` is still the default; a face
      the next frame has not is drawn where it is rather than moved,
      and a camera cut shows the game's own picture.
   6. **Lighting and shadows**: the normals the mesh has and the
      primitive does not.

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
11. **Menus and UI**: the menus redrawn (the Japanese options of the
    title screen in English), and the settings menu holding all of
    ReBushido's own settings.
12. **Optimisation**, for a smooth and seamless game.
