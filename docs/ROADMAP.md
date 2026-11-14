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
5. **Release tooling.** A patch file against the retail disc, and the tool
   that makes it and applies it; then a first release.
6. **The native port.** A build of the same source for a PC, beside the
   PlayStation one. First a C equivalent beside each piece of assembly
   (the GTE operations, the BIOS calls, the functions the authors wrote
   in assembly), checked by host tests; the PlayStation build keeps the
   assembly, and `decomp` is not touched. Then Sony's libraries
   (graphics, sound, disc, pad, memory card) answered by PC code: the
   menu in a window, then a fight, then sound. The game's code stays C.
7. **The picture.** A wide picture, high resolution and a high frame rate
   on the port, which is what "Graphics: Remastered" will mean. On the
   PlayStation build only what is cheap: about 17 KB is left there before
   a fixed buffer has to move.
