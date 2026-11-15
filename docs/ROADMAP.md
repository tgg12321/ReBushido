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
      pixel for pixel, at scale 1. Left of it: the window does not use
      it yet ("What the window still needs").
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
      primitive buffers of its own for it. Left of it: a stage's
      geometry was never meant to be seen whole, so its far edge shows
      where the ground stops.
   4. **The wide picture.** Done for the scene (`aspect`): the game
      never culls to the right or the bottom, so a wider view needs
      nothing of the projection — only that the renderer keep what the
      console's GPU threw away at the edge of its drawing area ("A
      wider picture"). Left of it: the HUD stays four by three in the
      middle and does not move out to the new edges.
   5. **A high frame rate**, 144 a second: the renderer drawing between
      the game's own steps, which the side channel's exact positions
      are what makes possible.
   6. **Lighting and shadows**: the normals the mesh has and the
      primitive does not.

   On the PlayStation build only what is cheap: about 17 KB is left
   there before a fixed buffer has to move.
