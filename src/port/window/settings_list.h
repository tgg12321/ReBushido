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

/* ---- the game ---- */

/* ReBushido's main menu (include/port_top_menu.h; docs/PLAYING.md, "The
 * main menu"): 0, Singleplayer, Versus, Training, Online Play and Options,
 * seen from the side only, with no options page at boot; 1, the game's
 * own menus and ReBushido's options page. A run of a test's (--plan or
 * --frames) has the original's unless --menu says otherwise. */
PORT_SETTING(top_menu,
    .name = "menu", .value = "rebushido",
    .note = "rebushido: ReBushido's main menu, with no\n"
            "options page at boot; original: the game's\n"
            "own menus and the options page",
    .read = PORT_READ_WORDS, .words = {"rebushido", "original"},
    .is = "rebushido or original",
    .menu = PORT_MENU_GAME, .rank = 90, .takes = PORT_TAKES_RESTART,
    .label = "Menus",
    .help = "ReBushido's main menu, or the game's own with the options "
            "page at boot",
    .shown = {"ReBushido", "Original"})

/* The original's bugs fixed (docs/REBUSHIDO.md, "The fixes"): what the
 * options page at boot set, which ReBushido's top menu does not show. The
 * settings menu changes the game's own at once, whichever menu. A gameplay
 * switch: in a session the host's holds. */
PORT_SETTING(bugfixes,
    .name = "bugfixes", .value = "off",
    .note = "the original's bugs fixed; off plays as the\n"
            "retail disc (with menu = original the\n"
            "options page at boot says)",
    .menu = PORT_MENU_GAME, .rank = 10, .takes = PORT_TAKES_NOW,
    .label = "Bug fixes",
    .help = "The original's bugs fixed; off plays as the retail disc")

/* The game's own three options, which its options page had (the save's
 * flags): written into the game's record at each frame of ReBushido's top
 * menu. With menu = original the game's options page and the card keep
 * them instead. In a session the host's hold. */
PORT_SETTING(difficulty,
    .name = "difficulty", .value = "hard",
    .note = "hard or easy: how the CPU fights (the game's\n"
            "own Difficulty)",
    .read = PORT_READ_WORDS, .words = {"hard", "easy"},
    .is = "hard or easy",
    .menu = PORT_MENU_GAME, .rank = 20, .takes = PORT_TAKES_NOW,
    .label = "Difficulty",
    .help = "How the CPU fights: the game's own Difficulty, from the next "
            "fight",
    .shown = {"Hard", "Easy"})

/* Which CPU plays the CPU's fighters (docs/CPU.md; port_cpu.h): the
 * game's own, as it is, or one of the port's four levels, which see the
 * fight as a player does and answer it after a player's time to react.
 * From the next fight. A gameplay switch: in a session the host's holds,
 * and a replay keeps it. */
PORT_SETTING(cpu,
    .name = "cpu", .value = "original",
    .note = "original, novice, adept, veteran or master:\n"
            "the game's own CPU, or one of ReBushido's\n"
            "levels, which play as a player would",
    .read = PORT_READ_WORDS,
    .words = {"original", "novice", "adept", "veteran", "master"},
    .is = "original, novice, adept, veteran or master",
    .menu = PORT_MENU_GAME, .rank = 25, .takes = PORT_TAKES_NOW,
    .label = "CPU",
    .help = "The game's own CPU, or one of ReBushido's levels, which see "
            "what a player sees and react as quickly; from the next mode or "
            "select screen",
    .shown = {"Original", "Novice", "Adept", "Veteran", "Master"})

PORT_SETTING(music,
    .name = "music", .value = "on",
    .note = "on or off: the game's music (its own option)",
    .menu = PORT_MENU_GAME, .rank = 30, .takes = PORT_TAKES_NOW,
    .label = "Music",
    .help = "The game's music; off leaves the sounds alone, from the next "
            "piece")

PORT_SETTING(monitor,
    .name = "monitor", .value = "color",
    .note = "color or bw: the game's own Monitor, black\n"
            "and white for a monochrome set",
    .read = PORT_READ_WORDS, .words = {"color", "bw"},
    .is = "color or bw",
    .menu = PORT_MENU_GAME, .rank = 40, .takes = PORT_TAKES_NOW,
    .label = "Monitor",
    .help = "The game's own Monitor: its picture in color, or black and "
            "white",
    .shown = {"Color", "Black and white"})

/* The PC package's updater (update.c; docs/PLAYING.md, "Updates"): ask,
 * the newest release asked for at a start, at most once a day, and a
 * notice on the top menu where there is a newer one; auto, downloaded as
 * well and put in place at the next start; off, nothing asked unless
 * the settings menu's Updates page is told to. */
PORT_SETTING(updates,
    .name = "updates", .value = "ask",
    .note = "ask: a newer release is shown on the top\n"
            "menu (asked for at most once a day); auto:\n"
            "downloaded too, installed at the next\n"
            "start; off: never asked for by itself",
    .read = PORT_READ_WORDS, .words = {"ask", "auto", "off"},
    .is = "ask, auto or off",
    .menu = PORT_MENU_GAME, .rank = 100, .takes = PORT_TAKES_NOW,
    .label = "Updates",
    .help = "A newer release shown, or downloaded and installed at the "
            "next start, or never asked for",
    .shown = {"Ask", "Automatic", "Off"})

/* Which releases: release, the repository's latest (never a
 * prerelease); beta, the newest of all, prereleases too. */
PORT_SETTING(update_channel,
    .name = "update_channel", .value = "release",
    .note = "release or beta: beta takes prereleases too",
    .read = PORT_READ_WORDS, .words = {"release", "beta"},
    .is = "release or beta",
    .menu = PORT_MENU_GAME, .rank = 110, .takes = PORT_TAKES_NOW,
    .label = "Update channel",
    .help = "Releases only, or the prereleases (beta) as well",
    .shown = {"Release", "Beta"})

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

/* ... and whether the keys and controllers are read again as late as the
 * refresh a frame's first picture makes allows (port_LateRead) */
PORT_SETTING(late_input,
    .name = "late_input", .value = "on",
    .note = "on: the keys and controllers read again\n"
            "just in time for the refresh a frame's\n"
            "first picture makes, so that a press is\n"
            "in it sooner; off reads them only as the\n"
            "frame begins",
    .menu = PORT_MENU_WINDOW, .rank = 85, .takes = PORT_TAKES_NOW,
    .label = "Late input",
    .help = "The keys read again just in time for the frame's first picture")

/* PORT_CPU_PREFER_...: where one L3 cache is clearly the larger (AMD's
 * X3D of two dies), the step's thread asks for its processors
 * (cpu_prefer.c; docs/PORT.md, "The processors with the larger cache") */
PORT_SETTING(cpu_prefer,
    .name = "cpu_prefer", .value = "auto",
    .note = "auto: where one of the processor's caches\n"
            "is clearly the larger (two dies, one with\n"
            "3D V-Cache), the game runs on its cores;\n"
            "elsewhere nothing changes. off: never",
    .read = PORT_READ_WORDS, .words = {"off", "auto"},
    .is = "auto or off",
    .menu = PORT_MENU_WINDOW, .rank = 90, .takes = PORT_TAKES_RESTART,
    .label = "Larger-cache cores",
    .help = "On a processor with 3D V-Cache on one die, the game on that "
            "die",
    .shown = {"Off", "Auto"})

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

/* Those models' shoulders, elbows, waist, hips and knees bent as one skin,
 * each joint's two parts carried by a turning mix of their bones; rigid,
 * the parts on their own bones as the game's (src/port/gl/gl_joints.h).
 * Only what is drawn changes. */
PORT_SETTING(joints_blended,
    .name = "joints", .value = "blended",
    .note = "blended: the models' shoulders, elbows,\n"
            "waist, hips and knees bend as one skin;\n"
            "rigid: each part on its own bone, as the\n"
            "game's (Remastered)",
    .read = PORT_READ_WORDS, .words = {"rigid", "blended"},
    .is = "rigid or blended",
    .menu = PORT_MENU_REMASTER, .rank = 112, .takes = PORT_TAKES_NOW,
    .label = "Fighters' joints",
    .help = "The models' joints bent as one skin, or rigid parts as the "
            "game's",
    .shown = {"Rigid", "Blended"})

/* How much a fighter's shadow darkens the ground it falls on, in
 * hundredths: 0 is the game's own black silhouettes; `cast`, the shadows
 * cast from the fighters' own bodies; built in, `map`, the sun's own
 * shadow maps (src/port/gl/gl_shadow.h, gl_sun.h; PORT_SHADOWS_SOFT,
 * PORT_SHADOWS_CAST, PORT_SHADOWS_MAP). */
PORT_SETTING(shadows,
    .name = "shadows", .value = "map",
    .note = "original: the game's own black shadows;\n"
            "soft: the ground under them darkened;\n"
            "cast: cast from the fighters' own\n"
            "bodies, darkened (cast:N, N hundredths);\n"
            "map: the sun's shadows of everything, on\n"
            "the ground, walls, steps and the fighters",
    .read = PORT_READ_BY, .reader = port_ReadShadows,
    .is = "original, soft, cast, cast:N, map, or how much of the ground a "
          "shadow takes away, in hundredths",
    .menu = PORT_MENU_REMASTER, .rank = 80, .takes = PORT_TAKES_NOW,
    .label = "Shadows",
    .help = "The game's black ones, the ground darkened, cast by the body, "
            "or the sun's own",
    .choices = {"original", "soft", "cast", "map"},
    .shown = {"Original", "Soft", "Cast", "Sun"})

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

/* The lights of a frame beside the stage's own -- a stage's torches and
 * lanterns, the flash where blades meet -- each lighting the ground, the
 * stage's pieces and the fighters at the pixel, the strongest few with
 * shadows (src/port/gl/gl_lights.h; docs/PORT.md, "The lighting
 * overhaul"). */
PORT_SETTING(lights,
    .name = "lights", .value = "on",
    .note = "on: the lights of a stage's own and of a\n"
            "fight (torches, lanterns, blades meeting)\n"
            "light the ground and the fighters; off for\n"
            "the stage's own lights alone",
    .menu = PORT_MENU_REMASTER, .rank = 125, .takes = PORT_TAKES_NOW,
    .label = "Lights",
    .help = "Torches, lanterns and sparks lighting the stage and the "
            "fighters")

/* The lighting's finish (src/port/gl/gl_post.h; docs/PORT.md, "The
 * lighting overhaul", c and d): each its own pass after the scene, on the
 * remastered picture alone. */
PORT_SETTING(reflections,
    .name = "reflections", .value = "on",
    .note = "on: polished floors, lacquered stages and\n"
            "glazed tiles reflect the fighters and the\n"
            "sky (Remastered)",
    .menu = PORT_MENU_REMASTER, .rank = 126, .takes = PORT_TAKES_NOW,
    .label = "Reflections",
    .help = "Polished floors and glazed tiles reflect the fighters and the "
            "sky")

PORT_SETTING(ambient_occlusion,
    .name = "ambient_occlusion", .value = "on",
    .note = "on: the ground darkened softly where it\n"
            "meets a wall, a step or a fighter's feet",
    .menu = PORT_MENU_REMASTER, .rank = 127, .takes = PORT_TAKES_NOW,
    .label = "Ambient occlusion",
    .help = "The ground darkened softly in corners and under the fighters")

PORT_SETTING(bloom,
    .name = "bloom", .value = "on",
    .note = "on: torches, lamps and the flash where\n"
            "blades meet glow (with lights on)",
    .menu = PORT_MENU_REMASTER, .rank = 128, .takes = PORT_TAKES_NOW,
    .label = "Bloom",
    .help = "Torches, lamps and the flash of a clash glow")

PORT_SETTING(tonemap,
    .name = "tonemap", .value = "on",
    .note = "on: where the lights make a surface\n"
            "brighter than white it rolls off instead\n"
            "of clipping; the stage's own light as it was",
    .menu = PORT_MENU_REMASTER, .rank = 129, .takes = PORT_TAKES_NOW,
    .label = "Tonemap",
    .help = "Bright light rolls off to white instead of clipping flat")

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

/* The stage's own lights, from the light files of the mods (the program
 * ships one for the stages with torches, fires and lanterns) and the
 * game's own flames (src/port/gl/gl_light_list.h; docs/MODS.md,
 * "Lights"). */
PORT_SETTING(stage_lights,
    .name = "stage_lights", .value = "on",
    .note = "the stage's torches, fires and lanterns\n"
            "lighting what is about them (Remastered)",
    .menu = PORT_MENU_REMASTER, .rank = 150, .takes = PORT_TAKES_NOW,
    .label = "Stage lights",
    .help = "Torches, fires and lanterns light the stage and the "
            "fighters")

/* The fight's flashes as lights: where blades meet, a hit, a shot, and
 * the glow of the sparks (src/port/gl/gl_light_list.h). */
PORT_SETTING(effect_lights,
    .name = "effect_lights", .value = "on",
    .note = "the flash where blades meet, and the\n"
            "sparks, lighting what is about them",
    .menu = PORT_MENU_REMASTER, .rank = 160, .takes = PORT_TAKES_NOW,
    .label = "Flash lights",
    .help = "The flash of a clash, a hit or a shot lights the scene")

/* A Remastered Classic stage's dressing (ROADMAP 17; src/port/gl/
 * gl_dressing.h; docs/MODS.md, "Dressing"): grass, bushes and reeds in the
 * wind, pushed by the fighters and their slashes; water; dust, leaves,
 * embers, snow and petals; scenery meshes. The picture's alone: nothing of
 * the game's state, so a session online is the same either way. */
PORT_SETTING(dressing,
    .name = "dressing", .value = "on",
    .note = "on: a Remastered Classic stage's grass,\n"
            "bushes, water, dust, leaves and embers\n"
            "(Remastered); off: the stage as it was",
    .menu = PORT_MENU_REMASTER, .rank = 155, .takes = PORT_TAKES_NOW,
    .label = "Stage dressing",
    .help = "Grass, bushes, water, dust, leaves and embers on Remastered "
            "Classic stages")

/* The kind every stage starts as on the stage select, and the kind the
 * modes with no stage page fight on (include/port_stage_choice.h;
 * docs/PLAYING.md, "Kinds of stage"): 0 Classic, the disc's stage with
 * the engine's and the renderer's improvements only; 1 Remastered
 * Classic, the same stage dressed (its lights, the stages-hd textures,
 * the sky). Square on a stage page switches one stage; in a session the
 * host's holds. */
PORT_SETTING(stage_kind,
    .name = "stage_kind", .value = "remastered",
    .note = "remastered: the stages dressed (their own\n"
            "lights, the stages-hd textures, the sky);\n"
            "classic: as the disc has them. Square\n"
            "switches a stage on the stage select",
    .read = PORT_READ_WORDS, .words = {"classic", "remastered"},
    .is = "classic or remastered",
    .menu = PORT_MENU_REMASTER, .rank = 165, .takes = PORT_TAKES_NOW,
    .label = "Stages",
    .help = "Every stage dressed (Remastered Classic), or as the disc has "
            "it (Classic); Square on the stage select switches one",
    .shown = {"Classic", "Remastered Classic"})

/* The time of day the modes with no stage page fight at
 * (include/port_stage_choice.h; docs/PLAYING.md, "The time of day"): 0
 * the stage's own lighting, then dawn, day, dusk and night, which the
 * remastered picture of a dressed stage shows. The stage pages start
 * every stage at its own (the owner, 2026-11-19); L1 and R1 there turn
 * one stage's. In a session the host's holds. */
PORT_SETTING(time_of_day,
    .name = "time_of_day", .value = "stage",
    .note = "stage: each stage's own light; or dawn,\n"
            "day, dusk or night where no stage page\n"
            "is shown. The stage select starts every\n"
            "stage at its own; L1 and R1 turn it",
    .read = PORT_READ_WORDS,
    .words = {"stage", "dawn", "day", "dusk", "night"},
    .is = "stage, dawn, day, dusk or night",
    .menu = PORT_MENU_REMASTER, .rank = 167, .takes = PORT_TAKES_NOW,
    .label = "Time of day",
    .help = "The time of day of fights chosen without the stage select "
            "(Remastered Classic stages); the stage select starts every "
            "stage at its own, and L1 and R1 there turn one",
    .shown = {"The stage's own", "Dawn", "Day", "Dusk", "Night"})

/* How the opening movie is shown in the remastered picture
 * (PORT_MOVIES_...; src/port/gl/gl_movie.h; docs/PORT.md, "The movie
 * made better"): the console's frames as blocks or smoothed by the
 * `filter` setting, stretched smooth, or cleaned of the decoder's blocks
 * and made larger along their edges on the card -- or a mod's frames,
 * made larger from the disc (tools/port_movies.py), where there is one.
 * The console's picture (F2) shows the movie as `original`. */
PORT_SETTING(movies,
    .name = "movies", .value = "enhanced",
    .note = "original: the console's frames, by\n"
            "`filter`; smooth: stretched smooth;\n"
            "enhanced: cleaned and made larger on the\n"
            "graphics card, or a mod's movie made from\n"
            "the disc (tools/port_movies.py)",
    .read = PORT_READ_WORDS, .words = {"original", "smooth", "enhanced"},
    .is = "original, smooth or enhanced",
    .menu = PORT_MENU_REMASTER, .rank = 168, .takes = PORT_TAKES_NOW,
    .label = "Movies",
    .help = "The opening movie as the console's frames, stretched smooth, "
            "or cleaned and made larger on the graphics card",
    .shown = {"Original", "Smooth", "Enhanced"})

/* The mods beside the program are read (docs/MODS.md); 0 runs the game
 * with none of them, as `--mods off` does. */
PORT_SETTING(mods,
    .name = "mods", .value = "on",
    .note = "the mods in mods/ beside the program\n"
            "(docs/MODS.md); off for the game's own",
    .menu = PORT_MENU_MODS, .rank = 30, .takes = PORT_TAKES_RESTART,
    .label = "Mods",
    .help = "The mods in mods/ beside the program")

/* The mods made from the player's own disc, the art, the stages' pages
 * and the fighters, by the package's prepare/ (prepare.c; docs/MODS.md,
 * "Made from your disc"): at a start where one is not made for this disc
 * and this version, with a screen of how far it has come that Escape
 * skips; 0 never makes them. */
PORT_SETTING(disc_mods,
    .name = "disc_mods", .value = "on",
    .note = "make the remastered art, stages and\n"
            "fighters from your disc at the first start\n"
            "(a few minutes; Escape skips); off: never",
    .menu = PORT_MENU_MODS, .rank = 35, .takes = PORT_TAKES_RESTART,
    .label = "Mods made from your disc",
    .help = "The art, the stages' textures and the fighters made from your "
            "disc at a start, when they are not made yet")

/* The English main menu the program ships as a mod of its own,
 * mods/english/ (docs/MODS.md, "The English menu"); 0 leaves it out. */
PORT_SETTING(english,
    .name = "english", .value = "on",
    .note = "the main menu in English, a mod the program\n"
            "ships (mods/english/); off for Japanese",
    .menu = PORT_MENU_MODS, .rank = 10, .takes = PORT_TAKES_RESTART,
    .label = "English main menu",
    .help = "The main menu's Japanese drawn again in English (Remastered)")

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

/* Versus the CPU and training go on to the game's own stage pages after
 * the weapons, as versus mode does, and fight on the stage chosen there
 * (include/port_play.h; docs/PLAYING.md, "A stage in every mode"); 0 lets
 * them pick their stages as the original does. A gameplay switch: in a
 * session the host's holds. */
PORT_SETTING(stage_select_everywhere,
    .name = "stage_select_everywhere", .value = "on",
    .note = "versus the CPU and training choose their\n"
            "stage on the stage select, as versus mode\n"
            "does; off: the stages the original picks",
    .menu = PORT_MENU_GAME, .rank = 50, .takes = PORT_TAKES_NOW,
    .label = "Stage select in every mode",
    .help = "Versus the CPU and training choose their stage after the "
            "weapons, as versus mode does")

/* A fight's music (include/port_fight_music.h; docs/PLAYING.md, "Music on
 * the stage select"): the stage's piece in every mode, story, versus the
 * CPU and team battle too, which the original plays with none; the
 * game's own rule; or none. The stage select's row (L1 and R1) starts
 * from it. In a session the host's holds. */
PORT_SETTING(fight_music,
    .name = "fight_music", .value = "stage",
    .note = "stage: the stage's music in every mode;\n"
            "original: none in story, versus the CPU\n"
            "and team battle, as the original; none.\n"
            "L1 and R1 choose another on the stage select",
    .read = PORT_READ_WORDS, .words = {"stage", "original", "none"},
    .is = "stage, original or none",
    .menu = PORT_MENU_MODS, .rank = 55, .takes = PORT_TAKES_NOW,
    .label = "Fight music",
    .help = "The stage's music in every mode, the original's (none in "
            "versus the CPU), or none; L1 and R1 on the stage select choose "
            "any piece",
    .shown = {"Stage's own", "Original", "None"})

/* Every fighter and mode the save keeps for progress is open, read so
 * wherever the game reads its save, never written into it
 * (include/port_play.h; docs/PLAYING.md, "Everything unlocked"). A
 * gameplay switch: in a session the host's holds. */
PORT_SETTING(unlock_all,
    .name = "unlock_all", .value = "off",
    .note = "every hidden fighter and slash mode open,\n"
            "without writing them into the save",
    .menu = PORT_MENU_GAME, .rank = 60, .takes = PORT_TAKES_NOW,
    .label = "Unlock everything",
    .help = "Every hidden fighter and slash mode open; the save keeps "
            "only what you have won")

/* The match rule of one against one (include/port_match.h; docs/PLAYING.md,
 * "Match rules"): the game's own single fight; Stock, each player
 * match_count lives; First to, match_count wins; Crew, crews of
 * match_count fighters and weapons each picks. The stage select starts
 * at it, Circle and Start there change it; versus the CPU with the
 * original's stages plays it. In a session the host's holds. */
PORT_SETTING(match_rule,
    .name = "match_rule", .value = "single",
    .note = "single: one fight, as the game; stock:\n"
            "match_count lives each; first_to:\n"
            "match_count wins; crew: a crew of\n"
            "match_count fighters each. Circle and\n"
            "Start change it on the stage select",
    .read = PORT_READ_WORDS, .words = {"single", "stock", "first_to", "crew"},
    .is = "single, stock, first_to or crew",
    .menu = PORT_MENU_GAME, .rank = 70, .takes = PORT_TAKES_NOW,
    .label = "Match rule",
    .help = "One fight as the game has it, a Stock match of lives, "
            "First to a number of wins, or a Crew battle of fighters "
            "picked in turn; Circle on the stage select changes it",
    .shown = {"Single fight", "Stock", "First to", "Crew"})

/* The lives of a Stock match, the wins of a First to (1 to 9; the menu
 * offers seven of them, the file and Start on the stage select any), the
 * size of a Crew match's crews (2 to 8). */
PORT_SETTING(match_count,
    .name = "match_count", .value = "3",
    .note = "lives of stock, wins of first_to: 1 to 9;\n"
            "a crew's fighters: 2 to 8",
    .read = PORT_READ_NUMBER, .least = 1, .most = 9,
    .is = "a number from 1 to 9",
    .menu = PORT_MENU_GAME, .rank = 75, .takes = PORT_TAKES_NOW,
    .label = "Match count",
    .help = "The lives of a Stock match, the wins of a First to, or the "
            "fighters of a Crew; Start on the stage select changes it",
    .choices = {"1", "2", "3", "4", "5", "7", "9"},
    .shown = {"1", "2", "3", "4", "5", "7", "9"})

/* Two on two (include/port_duo.h; docs/PLAYING.md, "Two on two"): one on
 * one as the game; or four fighters in two teams, each team two of its
 * first player's fighter, the second of each the CPU's (cpu) or played
 * from the third and fourth pads (pads; the CPU's where the team's first
 * is). The rule page before the select starts at it; versus, versus the
 * CPU and online, not with Crew. In a session the host's holds. */
PORT_SETTING(teams,
    .name = "teams", .value = "one",
    .note = "one: one on one; cpu: two on two, the\n"
            "partners the CPU's; pads: two on two, the\n"
            "partners the third and fourth pads'",
    .read = PORT_READ_WORDS, .words = {"one", "cpu", "pads"},
    .is = "one, cpu or pads",
    .menu = PORT_MENU_GAME, .rank = 77, .takes = PORT_TAKES_NOW,
    .label = "Teams",
    .help = "One on one, or two on two: each side two of its fighter, "
            "the partners the CPU's or the third and fourth pads'; R2 "
            "turns to the other enemy",
    .shown = {"One on one", "Two on two, CPU", "Two on two, pads"})

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

/* ---- replays ---- */

PORT_SETTINGS_HEADING(
    "Replays (docs/PLAYING.md): every fight kept, and how many of them.")

/* Every fight kept as a replay in replays/ beside the program (docs/
 * PLAYING.md, "Replays"; include/port_replay.h): its start and every
 * input, a few hundred KB. A test's run (--plan, --frames) keeps none
 * unless its --replays says where. */
PORT_SETTING(replays,
    .name = "replays", .value = "on",
    .note = "every fight kept as a replay in replays/\n"
            "beside the program (the settings menu's\n"
            "Replays page plays them)",
    .menu = PORT_MENU_GAME, .rank = 100, .takes = PORT_TAKES_NOW,
    .label = "Keep replays",
    .help = "Every fight kept in replays/ beside the program, to watch on "
            "the Replays page")

/* how many replays are kept: the oldest go once there are more (0: all) */
PORT_SETTING(replays_keep,
    .name = "replays_keep", .value = "200",
    .note = "the newest so many replays are kept, the\n"
            "oldest taken away (0: all of them)",
    .read = PORT_READ_NUMBER, .least = 0, .most = PORT_REPLAYS_KEEP_MAX,
    .is = "a number from 0 to 100000",
    .menu = PORT_MENU_GAME, .rank = 110, .takes = PORT_TAKES_NOW,
    .label = "Replays kept",
    .help = "The newest so many are kept, the oldest taken away",
    .choices = {"50", "100", "200", "500", "1000", "0"},
    .shown = {"50", "100", "200", "500", "1000", "All"})

/* ... and how many megabytes they take at most (0: no limit) */
PORT_SETTING(replays_megabytes,
    .name = "replays_megabytes", .value = "500",
    .note = "megabytes the replays take at most, the\n"
            "oldest taken away (0: no limit)",
    .read = PORT_READ_NUMBER, .least = 0, .most = PORT_REPLAYS_MEGABYTES_MAX,
    .is = "a number from 0 to 1000000",
    .menu = PORT_MENU_GAME, .rank = 120, .takes = PORT_TAKES_NOW,
    .label = "Replays' room",
    .help = "Megabytes the replays take at most, the oldest taken away",
    .choices = {"100", "250", "500", "1000", "5000", "0"},
    .shown = {"100 MB", "250 MB", "500 MB", "1 GB", "5 GB", "No limit"})

/* A replay as a film (docs/PLAYING.md, "Replays"; sdl_export.c): the
 * Replays page makes one in films/ beside the program, of this size, an
 * .mp4 on Windows and an .avi elsewhere */
PORT_SETTING(film_size,
    .name = "film_size", .value = "1920x1080",
    .note = "the size of a replay's film, made on the\n"
            "Replays page: 1920x1080, 1280x720, 2560x1440\n"
            "or 3840x2160",
    .read = PORT_READ_WORDS,
    .words = {"1920x1080", "1280x720", "2560x1440", "3840x2160"},
    .is = "1920x1080, 1280x720, 2560x1440 or 3840x2160",
    .menu = PORT_MENU_GAME, .rank = 130, .takes = PORT_TAKES_NOW,
    .label = "Film size",
    .help = "The size of a replay's film, made on the Replays page",
    .shown = {"1920 x 1080", "1280 x 720", "2560 x 1440", "3840 x 2160"})

/* ... and its picture: the remastered one, as the settings draw it, or
 * the console's own */
PORT_SETTING(film_picture,
    .name = "film_picture", .value = "remastered",
    .note = "the picture of a replay's film: remastered\n"
            "(as the picture settings draw it) or original",
    .read = PORT_READ_WORDS, .words = {"remastered", "original"},
    .is = "remastered or original",
    .menu = PORT_MENU_GAME, .rank = 140, .takes = PORT_TAKES_NOW,
    .label = "Film picture",
    .help = "A replay's film in the remastered picture, as your picture "
            "settings draw it, or the console's own",
    .shown = {"Remastered", "Original"})

/* The replay theatre's clip (docs/PLAYING.md, "The replay theatre"; G
 * while a replay plays): a GIF (the port's own, no sound, 640 across) or
 * a film as the Replays page makes one, of the frames marked, in the
 * theatre's view */
PORT_SETTING(clip_kind,
    .name = "clip", .value = "gif",
    .note = "the replay theatre's clip of the frames\n"
            "marked: gif (640 across, no sound) or film\n"
            "(as film_size and film_picture make one)",
    .read = PORT_READ_WORDS, .words = {"gif", "film"},
    .is = "gif or film",
    .menu = PORT_MENU_GAME, .rank = 150, .takes = PORT_TAKES_NOW,
    .label = "Theatre's clip",
    .help = "The replay theatre's clip of the frames marked: a GIF, or a "
            "film as the Replays page makes one",
    .shown = {"GIF", "Film"})

/* ---- playing online ---- */

PORT_SETTINGS_HEADING(
    "Playing online (docs/PLAYING.md): what the settings menu's Play\n"
    "online page starts with, and keeps.")

/* The settings menu's Play online page (docs/PLAYING.md): host by
 * address (0), join by address (1), host a room of the lobby (2), or
 * browse the lobby's rooms (3) */
PORT_SETTING(net_role,
    .name = "net_role", .value = "room",
    .note = "room: host a room at the lobby; rooms: browse\n"
            "its rooms and join one; host: wait for the other\n"
            "player at your address; join: go to the host at\n"
            "net_address",
    .read = PORT_READ_WORDS, .words = {"host", "join", "room", "rooms"},
    .is = "room, rooms, host or join")

/* the lobby server, `A.B.C.D:PORT` or `[IPv6]:PORT`; "" for none */
PORT_SETTING_CUSTOM(
    .name = "net_lobby", .value = PORT_NET_LOBBY_DEFAULT,
    .note = "the lobby server: its address and port (nothing\n"
            "for none)",
    .read = PORT_READ_BY, .reader = port_ReadNetLobby,
    .is = "an address and a port, as 135.181.108.74:7845, or nothing")

/* the room's name a host opens, and the name the lobby shows */
PORT_SETTING_CUSTOM(
    .name = "net_room", .value = "",
    .note = "the name of the room you host (your player name if\n"
            "nothing)",
    .read = PORT_READ_BY, .reader = port_ReadNetRoom,
    .is = "letters, digits, spaces and - _ . ! ?, up to 24")

PORT_SETTING_CUSTOM(
    .name = "net_player", .value = "",
    .note = "your name in the lobby",
    .read = PORT_READ_BY, .reader = port_ReadNetPlayer,
    .is = "letters, digits, spaces and - _ . ! ?, up to 16")

/* the host to join, `A.B.C.D:PORT` as it was written */
PORT_SETTING_CUSTOM(
    .name = "net_address", .value = "127.0.0.1:7845",
    .note = "the host's address and port",
    .read = PORT_READ_BY, .reader = port_ReadNetSetting,
    .is = "an address and a port, as 192.168.1.20:7845 or\n"
          "[2001:db8::7]:7845")

/* a host by address: the other player's public address, sent to while
 * waiting so that both routers let the two through; "" for none */
PORT_SETTING_CUSTOM(
    .name = "net_punch_to", .value = "",
    .note = "hosting by address: the other player's public\n"
            "address, to punch through to (nothing for none)",
    .read = PORT_READ_BY, .reader = port_ReadNetPunchTo,
    .is = "an address and a port, or nothing")

/* whether the host asks its router to open its port (UPnP, NAT-PMP,
 * PCP: portmap.c) */
PORT_SETTING(net_portmap,
    .name = "net_portmap", .value = "on",
    .note = "the host asks its router to open its port (UPnP,\n"
            "NAT-PMP or PCP), and closes it again as it ends")

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

/* ---- training ---- */

PORT_SETTINGS_HEADING(
    "Training (the main menu's Training; include/port_training.h): the\n"
    "dummy, its recordings, slow motion and what is shown over the fight")

/* What the dummy does in a fight of training: stands; guards, pressing
 * the attack the game's CPU answers an attack about to land with; or
 * plays back a slot recorded for it (Select and Cross in the fight). */
PORT_SETTING(training_dummy,
    .name = "training_dummy", .value = "stand",
    .note = "stand, guard or playback: what the dummy\n"
            "does (playback plays its slot round again)",
    .read = PORT_READ_WORDS, .words = {"stand", "guard", "playback"},
    .is = "stand, guard or playback",
    .menu = PORT_MENU_TRAINING, .rank = 10, .takes = PORT_TAKES_NOW,
    .label = "Dummy",
    .help = "Stands, guards as the game's CPU would, or plays back its "
            "recording",
    .shown = {"Stands", "Guards", "Plays back"})

/* The slot recorded into and played back, of four, each a minute. */
PORT_SETTING(training_slot,
    .name = "training_slot", .value = "1",
    .note = "1 to 4: the slot Select and Cross record\n"
            "into, and the dummy plays back",
    .read = PORT_READ_NUMBER, .least = 1, .most = 4,
    .is = "a number from 1 to 4",
    .menu = PORT_MENU_TRAINING, .rank = 20, .takes = PORT_TAKES_NOW,
    .label = "Slot",
    .help = "The slot recorded into and played back, each a minute long",
    .choices = {"1", "2", "3", "4"},
    .shown = {"1", "2", "3", "4"})

/* Playback picks one of the slots recorded at random each time round. */
PORT_SETTING(training_random,
    .name = "training_random", .value = "off",
    .note = "on: playback plays a random slot of those\n"
            "recorded, a new one each time round",
    .menu = PORT_MENU_TRAINING, .rank = 30, .takes = PORT_TAKES_NOW,
    .label = "Random slot",
    .help = "Playback plays one of the slots recorded, at random, each "
            "time round")

/* Slow motion (Select and L2): a frame of the fight in every 2 to 8. */
PORT_SETTING(training_slow,
    .name = "training_slow", .value = "2",
    .note = "2 to 8: slow motion shows each of the\n"
            "fight's frames so many times",
    .read = PORT_READ_NUMBER, .least = 2, .most = 8,
    .is = "a number from 2 to 8",
    .menu = PORT_MENU_TRAINING, .rank = 40, .takes = PORT_TAKES_NOW,
    .label = "Slow motion",
    .help = "How slow Select and L2 makes the fight",
    .choices = {"2", "3", "4", "8"},
    .shown = {"Half speed", "A third", "A quarter", "An eighth"})

/* Both players' presses, frame by frame, over a fight of training. */
PORT_SETTING(training_inputs,
    .name = "training_inputs", .value = "on",
    .note = "on: both pads' presses shown over a fight\n"
            "of training, the frames each was held",
    .menu = PORT_MENU_TRAINING, .rank = 50, .takes = PORT_TAKES_NOW,
    .label = "Input display",
    .help = "Both players' presses over the fight, with the frames each "
            "was held")

/* The fighters' hit areas, their kill and wound zones and the weapons'
 * reach, drawn over a fight of training in the remastered picture. */
PORT_SETTING(training_hit_areas,
    .name = "training_hit_areas", .value = "on",
    .note = "on: the bodies' kill and wound zones and\n"
            "the blades' reach drawn over a fight of\n"
            "training (the remastered picture)",
    .menu = PORT_MENU_TRAINING, .rank = 60, .takes = PORT_TAKES_NOW,
    .label = "Hit areas",
    .help = "The bodies' kill and wound zones and the blades' reach over "
            "the fighters, in the remastered picture")

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
