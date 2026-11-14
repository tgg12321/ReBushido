# ReBushido roadmap

What is planned for [ReBushido](REBUSHIDO.md), in the order it will be done.
No dates.

1. **The foundation.** Done. The branch, the switch for each change, the
   ReBushido build beside the matching one, the place the settings live, the
   version text, a disc to play, and the checks.
2. **The options page and the settings' save.** A page at boot where the
   player sets "Bugfixes" and the picture, and a file of ReBushido's own on
   the memory card that keeps the choices. The game's save is never
   touched.
3. **Scripted play tests.** Done, ahead of item 2, so that everything
   after it can be tried. The game run in an emulator with no window, by
   script, on the developer's machine only (`make playtest`,
   [`tests/play/README.md`](../tests/play/README.md)); and a build that
   boots straight into a chosen fight, for people and for agents
   (`make quickboot-disc`, [`REBUSHIDO.md`](REBUSHIDO.md)).
4. **The bug fixes.** The list is in [`STATUS.md`](STATUS.md), "Suspected
   bugs and oddities of the original". Each is behind the "Bugfixes"
   setting; one that changes how the game plays by much gets its own.
5. **Widescreen.** A wide picture for the fight and the menus.
6. **The 640 by 480 mode.**
7. **60 frames a second**, after measuring what rate the game runs at and
   what a frame costs.
8. **Release tooling.** A patch file against the retail disc, and the tool
   that makes it and applies it.

Later, and outside this roadmap: a native port.
