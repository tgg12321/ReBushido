/* The window's settings, every one of them in one place (window.h,
 * "the settings"; docs/PORT.md, "The window"): what rebushido.cfg calls
 * each, what it is built in as, how its value is read, the comment the
 * built-in text has for it, and how the settings menu shows it. The
 * member of PortSettings, the reader, the built-in text (--help), the
 * settings built in and the menu are all made from this list, so a new
 * setting is one entry here and nothing else. No include guard: a file
 * that wants the list defines the three macros and includes it.
 *
 *   PORT_SETTING(member, ...)         a setting held in the s32 `member`
 *                                     of PortSettings
 *   PORT_SETTING_CUSTOM(...)          one whose reader sets members
 *                                     declared by hand in PortSettings
 *   PORT_SETTINGS_HEADING(text)       a comment of the built-in text,
 *                                     after a blank line, before the
 *                                     settings that follow it
 *
 * The rest of an entry is PortSetting's fields by name (window.h): a
 * field left out is 0 or NULL. In short:
 *
 *   .name, .value     the line of the built-in text, `name = value`; the
 *                     value is the setting built in
 *   .note             the comment after it, its lines joined by \n
 *   .read             PORT_READ_SWITCH (left out: off or on, 0 or 1),
 *                     PORT_READ_WORDS (.words, each its place in the
 *                     list), PORT_READ_NUMBER (.least to .most, and any
 *                     .words with their .values), PORT_READ_BY (.reader)
 *   .is               what a line that cannot be read is told: `NAME is
 *                     .is` (a switch's is "on or off")
 *   .menu, .rank      the settings menu's group (left out: not in the
 *                     menu) and the place in it, the smallest first; the
 *                     ranks go in tens so that one can go between two
 *   .takes, .label, .help, .choices, .shown
 *                     as PortChoice has them; .choices left out are a
 *                     switch's off and on or the reader's .words, .shown
 *                     left out a switch's Off and On
 *
 * The order of the list is the built-in text's. */

/* clang-format off */

/* ---- the window ---- */

PORT_SETTING(scale,
    .name = "scale", .value = "fit", .note = "fit, integer or stretch",
    .read = PORT_READ_WORDS, .words = {"fit", "integer", "stretch"},
    .is = "fit, integer or stretch",
    .menu = PORT_MENU_WINDOW, .rank = 20, .takes = PORT_TAKES_NOW,
    .label = "Picture size",
    .help = "As large as fits, whole multiples of 320x240, or all of the "
            "window",
    .shown = {"Fit", "Whole multiples", "Stretch"})

/* the picture smoothed as it is scaled */
PORT_SETTING(is_linear,
    .name = "filter", .value = "nearest", .note = "nearest or linear",
    .read = PORT_READ_WORDS, .words = {"nearest", "linear"},
    .is = "nearest or linear",
    .menu = PORT_MENU_WINDOW, .rank = 30, .takes = PORT_TAKES_NOW,
    .label = "Pixels",
    .help = "The console's picture: blocks, or smoothed as it is scaled",
    .shown = {"Sharp", "Smooth"})

/* at the start */
PORT_SETTING(is_fullscreen,
    .name = "fullscreen", .value = "on", .note = "on or off: how it starts",
    .menu = PORT_MENU_WINDOW, .rank = 10, .takes = PORT_TAKES_NOW,
    .label = "Full screen",
    .help = "The whole screen, or a window (F11 does the same)")

/* wait for the display: 0, 1, or PORT_VSYNC_ADAPTIVE */
PORT_SETTING(vsync,
    .name = "vsync", .value = "on",
    .note = "on, off or adaptive: wait for the display;\n"
            "adaptive does not wait for a picture that\n"
            "is late, which tears it rather than show\n"
            "the one before twice",
    .read = PORT_READ_WORDS, .words = {"off", "on", "adaptive"},
    .is = "on, off or adaptive",
    .menu = PORT_MENU_WINDOW, .rank = 40, .takes = PORT_TAKES_NOW,
    .label = "Wait for the display",
    .help = "Vertical sync: off tears the picture; adaptive tears only a "
            "late one",
    .shown = {"Off", "On", "Adaptive"})

/* how far a stick is pushed before it counts, in parts of a hundred */
PORT_SETTING(deadzone,
    .name = "deadzone", .value = PORT_QUOTE(PORT_DEADZONE_DEFAULT),
    .note = "how far a stick counts, in parts of 100",
    .read = PORT_READ_NUMBER, .least = 1, .most = PORT_DEADZONE_MAX,
    .is = "a number from 1 to 99",
    .menu = PORT_MENU_CONTROLS, .rank = 10, .takes = PORT_TAKES_NOW,
    .label = "Stick dead zone",
    .help = "How far a stick is pushed before it counts",
    .choices = {"20", "30", "40", "50", "60", "70", "80"},
    .shown = {"20%", "30%", "40%", "50%", "60%", "70%", "80%"})

/* the counter in the corner is shown: 1, or PORT_FPS_TIMES with the
 * processor's and the card's time per part under it (docs/PORT.md,
 * "Measuring on the card") */
PORT_SETTING(fps,
    .name = "fps", .value = "on",
    .note = "on or off: the counter in the corner (F3)",
    .read = PORT_READ_WORDS, .words = {"off", "on", "times"},
    .is = "on, off or times",
    .menu = PORT_MENU_WINDOW, .rank = 50, .takes = PORT_TAKES_NOW,
    .label = "Counter",
    .help = "Pictures and frames a second in a corner, and where the time "
            "goes (F3)",
    .shown = {"Off", "On", "With times"})

/* PORT_CORNER_... */
PORT_SETTING(fps_corner,
    .name = "fps_corner", .value = "top-left",
    .note = "top-left, top-right, bottom-left,\n"
            "bottom-right",
    .read = PORT_READ_WORDS,
    .words = {"top-left", "top-right", "bottom-left", "bottom-right"},
    .is = "top-left, top-right, bottom-left or bottom-right",
    .menu = PORT_MENU_WINDOW, .rank = 60, .takes = PORT_TAKES_NOW,
    .label = "Counter's corner",
    .help = "Where the counter is",
    .shown = {"Top left", "Top right", "Bottom left", "Bottom right"})

/* input lag (docs/PORT.md, "Input lag"): frames run ahead, 0 to
 * PORT_RUN_AHEAD_MAX, so that a press is seen sooner... */
PORT_SETTING(run_ahead,
    .name = "run_ahead", .value = PORT_QUOTE(PORT_RUN_AHEAD_DEFAULT),
    .note = "0, 1 or 2: frames run ahead and put back,\n"
            "so that a press is seen that many of the\n"
            "game's frames sooner; what the game\n"
            "computes is the same",
    .read = PORT_READ_NUMBER, .least = 0, .most = PORT_RUN_AHEAD_MAX,
    .is = "0, 1 or 2",
    .menu = PORT_MENU_WINDOW, .rank = 70, .takes = PORT_TAKES_NOW,
    .label = "Run ahead",
    .help = "A press seen 1 or 2 frames sooner; the game computes the same",
    .choices = {"0", "1", "2"},
    .shown = {"Off", "1 frame", "2 frames"})

/* ... and whether a swap waits for the card, so that no picture is
 * queued behind another */
PORT_SETTING(low_latency,
    .name = "low_latency", .value = "on",
    .note = "on: each picture waits for the card, so\n"
            "none queues behind another; off lets the\n"
            "driver queue them",
    .menu = PORT_MENU_WINDOW, .rank = 80, .takes = PORT_TAKES_NOW,
    .label = "Low latency",
    .help = "Each picture waits for the card: none queued behind another")

/* ---- the remastered picture ---- */

PORT_SETTINGS_HEADING(
    "The remastered picture (F2 changes picture while the game runs).\n"
    "These four are a run's: changing one means starting it again.")

/* PORT_GRAPHICS_... (port_remaster.h; docs/PORT.md, "Remastered in the
 * window"): which picture a run starts with. Every new feature is on in
 * the builds the owner plays (2026-11-17). */
PORT_SETTING(graphics,
    .name = "graphics", .value = "remastered",
    .note = "original, remastered, or follow (the\n"
            "options page at boot says which)",
    .read = PORT_READ_WORDS, .words = {"original", "remastered", "follow"},
    .is = "original, remastered or follow",
    .menu = PORT_MENU_REMASTER, .rank = 10, .takes = PORT_TAKES_NOW,
    .label = "Graphics",
    .help = "The console's own picture, or the scene drawn again (F2)",
    .choices = {"original", "remastered"},
    .shown = {"Original", "Remastered"})

/* PORT_RESOLUTION_WINDOW, or 1 and up */
PORT_SETTING(resolution,
    .name = "resolution", .value = "0",
    .note = "how many times the console's; 0 is the\n"
            "window's own size",
    .read = PORT_READ_NUMBER, .least = 0, .most = PORT_REMASTER_SCALE_MAX,
    .is = "0 (the window's own size) or a number up to 8",
    .menu = PORT_MENU_REMASTER, .rank = 20, .takes = PORT_TAKES_NOW,
    .label = "Resolution",
    .help = "How many times the console's the scene is drawn at",
    .choices = {"0", "1", "2", "3", "4", "6", "8"},
    .shown = {"The window's", "1x", "2x", "3x", "4x", "6x", "8x"})

/* PORT_DISTANCE_...: the whole stage, every frame, so that nothing of it
 * comes and goes as the camera turns (docs/PORT.md, "How far the stage
 * is drawn") */
PORT_SETTING(distance,
    .name = "draw_distance", .value = "max",
    .note = "original, far or max: how much of the stage",
    .read = PORT_READ_BY, .reader = port_ReadDistance,
    .is = "original, far or max",
    .menu = PORT_MENU_REMASTER, .rank = 40, .takes = PORT_TAKES_RESTART,
    .label = "Draw distance",
    .help = "How much of the stage is drawn, and its fog pushed out",
    .choices = {"original", "far", "max"},
    .shown = {"Original", "Far", "The whole stage"})

/* 1 (off), 2 or 4 */
PORT_SETTING(supersample,
    .name = "antialiasing", .value = "2",
    .note = "off, 2 or 4: the scene's edges smoothed",
    .read = PORT_READ_NUMBER, .least = 1, .most = PORT_SUPERSAMPLE_MAX,
    .words = {"off"}, .values = {1},
    .is = "off, 2 or 4",
    .menu = PORT_MENU_REMASTER, .rank = 30, .takes = PORT_TAKES_RESTART,
    .label = "Antialiasing",
    .help = "The scene's edges smoothed: 4 or 8 samples a pixel",
    .choices = {"off", "2", "4"},
    .shown = {"Off", "2x", "4x"})

/* aspect_w by aspect_h: the remastered picture is a wider view of the
 * same scene, with the fight's HUD at its edges (port_remaster.h);
 * "Graphics: Original" is the console's four by three whatever this
 * says */
PORT_SETTING_CUSTOM(
    .name = "aspect", .value = "16:9",
    .note = "16:9 for a wider view of the same scene,\n"
            "or 4:3 as the console",
    .read = PORT_READ_BY, .reader = port_ReadAspect,
    .is = "W:H, as 4:3 or 16:9",
    .menu = PORT_MENU_REMASTER, .rank = 50, .takes = PORT_TAKES_RESTART,
    .label = "Shape",
    .help = "A wider view of the same scene, not a stretched picture",
    .choices = {"4:3", "16:10", "16:9", "21:9"},
    .shown = {"4:3", "16:10", "16:9", "21:9"})

/* The stage's ground drawn as one static mesh instead of from the game's
 * tile primitives, which is what Remastered means by the stage now: it
 * has been seen on a real card and is measured stage by stage
 * (src/port/gl/gl_stage.h, g_stage_proved). A stage the measurements do
 * not hold keeps the game's own tiles of its own accord, and the run's
 * log says which path each stage took. */
PORT_SETTING(stage_mesh,
    .name = "stage_mesh", .value = "on",
    .note = "the stage's ground drawn from one static\n"
            "mesh instead of the game's own tiles; off\n"
            "for the tiles. A stage the mesh is not\n"
            "measured good on keeps the tiles either way",
    .menu = PORT_MENU_REMASTER, .rank = 60, .takes = PORT_TAKES_NOW,
    .label = "Ground as one mesh",
    .help = "The stage's ground from one mesh instead of the game's tiles")

/* What stands on the ground drawn from the same mesh
 * (src/port/gl/gl_stage.h, port_GlStageSetPropsOn). */
PORT_SETTING(stage_props_mesh,
    .name = "stage_props_mesh", .value = "on",
    .note = "what stands on the ground -- railings,\n"
            "fences, walls, roofs -- drawn still from\n"
            "meshes of its own with the ground's mesh;\n"
            "off for the game's own faces of them",
    .menu = PORT_MENU_REMASTER, .rank = 70, .takes = PORT_TAKES_NOW,
    .label = "Railings and walls as meshes",
    .help = "What stands on the ground drawn still, from meshes of its own")

/* The fighters drawn from models of their own where there is one in
 * mods/fighters/; with none there, nothing changes
 * (src/port/gl/gl_fighter.h). */
PORT_SETTING(fighter_models,
    .name = "fighter_models", .value = "on",
    .note = "the fighters drawn from models of their own\n"
            "where a mod's models/ has one (M100.glb);\n"
            "off for the game's own",
    .menu = PORT_MENU_REMASTER, .rank = 110, .takes = PORT_TAKES_NOW,
    .label = "Fighters' own models",
    .help = "The fighters drawn from a mod's models where it has them")

/* How much a fighter's shadow darkens the ground it falls on, in
 * hundredths: 0 is the game's own black silhouettes; built in, the
 * shadows cast from the fighters' own bodies (src/port/gl/gl_shadow.h;
 * PORT_SHADOWS_SOFT, PORT_SHADOWS_CAST). */
PORT_SETTING(shadows,
    .name = "shadows", .value = "cast",
    .note = "original: the game's own black shadows;\n"
            "soft: the ground under them darkened;\n"
            "cast: cast from the fighters' own\n"
            "bodies, darkened (cast:N, N hundredths)",
    .read = PORT_READ_BY, .reader = port_ReadShadows,
    .is = "original, soft, cast, cast:N, or how much of the ground a "
          "shadow takes away, in hundredths",
    .menu = PORT_MENU_REMASTER, .rank = 80, .takes = PORT_TAKES_NOW,
    .label = "Shadows",
    .help = "The game's black ones, the ground darkened, or cast by the body",
    .choices = {"original", "soft", "cast"},
    .shown = {"Original", "Soft", "Cast"})

/* The game's own lighting worked out at each pixel on the ground's mesh
 * and the fighters' models (src/port/gl/gl_light.h; docs/PORT.md,
 * "Lighting"). */
PORT_SETTING(smooth_light,
    .name = "smooth_light", .value = "on",
    .note = "on: the game's own lights worked out at\n"
            "each pixel of the ground and the fighters'\n"
            "models, not once a face or a vertex",
    .menu = PORT_MENU_REMASTER, .rank = 120, .takes = PORT_TAKES_NOW,
    .label = "Smooth lighting",
    .help = "The game's own lights worked out at each pixel of ground and "
            "body")

/* How the effects are drawn: PORT_EFFECTS_... (port_remaster.h;
 * docs/PORT.md, "The effects"). */
PORT_SETTING(effects,
    .name = "effects", .value = "smooth",
    .note = "original: blood, sparks and dust where the\n"
            "console put them; exact: where the\n"
            "projection put them, moved between frames;\n"
            "smooth: their textures smoothed as well",
    .read = PORT_READ_BY, .reader = port_ReadEffects,
    .is = "original, exact or smooth",
    .menu = PORT_MENU_REMASTER, .rank = 130, .takes = PORT_TAKES_NOW,
    .label = "Effects",
    .help = "Blood, sparks and dust: as placed, exactly placed, or smoothed "
            "too",
    .choices = {"original", "exact", "smooth"},
    .shown = {"Original", "Exact", "Smooth"})

/* PORT_FRAME_RATE_..., or pictures a second */
PORT_SETTING(frame_rate,
    .name = "frame_rate", .value = "display",
    .note = "original: one picture a blank, as the\n"
            "console; display: as many as the screen\n"
            "shows; or a number a second. The game\n"
            "steps at the console's rate either way",
    .read = PORT_READ_NUMBER, .least = 1, .most = PORT_FRAME_RATE_MAX,
    .words = {"original", "display"},
    .values = {PORT_FRAME_RATE_ORIGINAL, PORT_FRAME_RATE_DISPLAY},
    .is = "original, display, or a number of pictures a second",
    .menu = PORT_MENU_REMASTER, .rank = 140, .takes = PORT_TAKES_RESTART,
    .label = "Frame rate",
    .help = "Pictures a second; the game itself steps as the console does",
    .choices = {"original", "display", "60", "120", "144", "240"},
    .shown = {"Original", "The display's", "60", "120", "144", "240"})

/* A night sky of the port's own over the stages open to it, in the place
 * of the game's colour of the distance (src/port/gl/gl_sky.h): 0
 * original, 1 night. It changes nothing the game computes, and is the
 * owner's to confirm on a real card. */
PORT_SETTING(sky,
    .name = "sky", .value = "night",
    .note = "night: a night sky over the stages open\n"
            "to it; original: the game's flat colour\n"
            "of the distance",
    .read = PORT_READ_WORDS, .words = {"original", "night"},
    .is = "original or night",
    .menu = PORT_MENU_REMASTER, .rank = 90, .takes = PORT_TAKES_NOW,
    .label = "Sky",
    .help = "A night sky over the stages open to it, or the game's dark",
    .shown = {"Original", "Night"})

/* The stage's walls thinned where they hide a fighter
 * (src/port/gl/gl_see.h); as the sky, the owner's to confirm. */
PORT_SETTING(see_through,
    .name = "see_through", .value = "on",
    .note = "the stage's walls thinned where they hide\n"
            "a fighter from the camera; off as the\n"
            "console",
    .menu = PORT_MENU_REMASTER, .rank = 100, .takes = PORT_TAKES_NOW,
    .label = "See through walls",
    .help = "The stage thinned where it hides a fighter from the camera")

/* The mods beside the program are read (docs/MODS.md); 0 runs the game
 * with none of them, as `--mods off` does. */
PORT_SETTING(mods,
    .name = "mods", .value = "on",
    .note = "the mods in mods/ beside the program\n"
            "(docs/MODS.md); off for the game's own",
    .menu = PORT_MENU_MODS, .rank = 30, .takes = PORT_TAKES_RESTART,
    .label = "Mods",
    .help = "The mods in mods/ beside the program")

/* The English top menu the program ships as a mod of its own,
 * mods/english/ (docs/MODS.md, "The English menu"); 0 leaves it out. */
PORT_SETTING(english,
    .name = "english", .value = "on",
    .note = "the top menu in English, a mod the program\n"
            "ships (mods/english/); off for Japanese",
    .menu = PORT_MENU_MODS, .rank = 10, .takes = PORT_TAKES_RESTART,
    .label = "English top menu",
    .help = "The top menu's Japanese drawn again in English (Remastered)")

/* The menus' lettering drawn again, the mod the program ships as
 * mods/ui/ (docs/MODS.md, "The sharp menus"); 0 leaves it out. */
PORT_SETTING(ui,
    .name = "ui", .value = "on",
    .note = "the menus' lettering drawn again, sharp at\n"
            "any size: a mod the program ships (mods/ui/)",
    .menu = PORT_MENU_MODS, .rank = 20, .takes = PORT_TAKES_RESTART,
    .label = "Sharp menus",
    .help = "The menus' lettering drawn again at the window's size "
            "(Remastered)")

/* A mod's music is heard in place of the game's (docs/MODS.md,
 * "Music"); 0 plays the game's own, from the next sample on. */
PORT_SETTING(music_mods,
    .name = "music_mods", .value = "on",
    .note = "a mod's music in place of the game's; off\n"
            "for the game's own (docs/MODS.md)",
    .menu = PORT_MENU_MODS, .rank = 40, .takes = PORT_TAKES_NOW,
    .label = "Mods' music",
    .help = "A mod's music in place of the game's, where a mod has some")

/* The blanks in which the game waits for the disc behind its loading
 * picture are hurried through, not shown at the console's pace
 * (docs/PORT.md, "Quick loading"); 0 shows each as the console would.
 * Nothing the game computes changes either way. */
PORT_SETTING(quick_loading,
    .name = "quick_loading", .value = "on",
    .note = "the waits for the disc behind a loading\n"
            "picture hurried through; off at the\n"
            "console's pace",
    .menu = PORT_MENU_WINDOW, .rank = 90, .takes = PORT_TAKES_NOW,
    .label = "Quick loading",
    .help = "The waits for the disc behind a loading picture hurried "
            "through")

/* ---- playing online ---- */

PORT_SETTINGS_HEADING(
    "Playing online (docs/PLAYING.md): what the settings menu's Play\n"
    "online page starts with, and keeps.")

/* The settings menu's Play online page (docs/PLAYING.md): host (0) or
 * join (1) */
PORT_SETTING(net_role,
    .name = "net_role", .value = "host",
    .note = "host: wait for the other player; join: go to\n"
            "the host at net_address",
    .read = PORT_READ_WORDS, .words = {"host", "join"},
    .is = "host or join")

/* the host to join, `A.B.C.D:PORT` as it was written */
PORT_SETTING_CUSTOM(
    .name = "net_address", .value = "127.0.0.1:7845",
    .note = "the host's address and port",
    .read = PORT_READ_BY, .reader = port_ReadNetSetting,
    .is = "four numbers and a port, as 192.168.1.20:7845")

/* the port a host waits at */
PORT_SETTING(net_port,
    .name = "net_port", .value = PORT_QUOTE(PORT_NET_PORT_DEFAULT),
    .note = "the port a host waits at",
    .read = PORT_READ_NUMBER, .least = 1, .most = PORT_NET_PORT_MAX,
    .is = "a number from 1 to 65535")

/* whether a host is found at every address of the PC (1) or at
 * 127.0.0.1 alone (0) */
PORT_SETTING(net_listen,
    .name = "net_listen", .value = "all",
    .note = "all: a host is found at every address of this\n"
            "PC; local: on this PC alone (127.0.0.1)",
    .read = PORT_READ_WORDS, .words = {"local", "all"},
    .is = "all or local")

/* the frames of delay */
PORT_SETTING(net_delay,
    .name = "net_delay", .value = PORT_QUOTE(PORT_NET_DELAY_DEFAULT),
    .note = "frames a press counts later, 0 to 8: the\n"
            "host's is both players'",
    .read = PORT_READ_NUMBER, .least = 0, .most = PORT_NET_DELAY_MAX,
    .is = "a number from 0 to 8")

/* ---- the controls ---- */

PORT_SETTINGS_HEADING(
    "The keys and the controllers' buttons: a layout to start from, and\n"
    "lines of pN.BUTTON for what to change in it (below)")

/* the bindings of one of g_port_layouts (`layout`), which the file's own
 * pN lines are read over */
PORT_SETTING_CUSTOM(
    .name = "controls", .value = "original",
    .note = "original is the only one so far",
    .read = PORT_READ_BY, .reader = port_ReadControls)

/* clang-format on */
