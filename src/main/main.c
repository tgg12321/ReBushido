/* Boot and the main loop.
 *
 * main() starts the machine (sys_Init), sets the game up (sys_InitGame),
 * shows the boot image and then runs frames for ever: read the pads, step
 * the CD and the sound queue, call the current screen's handler, wait for
 * the frame to end, show what was drawn. The screens are listed in sys.h.
 *
 * Also here: the display's set-up and its small controls (disp_), the loading
 * screens drawn between the steps of a long load, the pause menu, the game's
 * random numbers, and loaders that read files from a development host. */
#include "common.h"
#include <psxsdk/libsn.h>
#include "bb2.h"

/* Defined further down. */
static RECT g_disp_full_rect;
static RECT g_disp_boot_image_rect;
static s32 g_sys_prim_low_water;

GpuDb g_disp_db[2];

enum PauseMenuRow {
    PAUSE_MENU_RESUME = 0,
    PAUSE_MENU_SUB_MENU = 1, /* back to character select */
    PAUSE_MENU_TOP_MENU = 2, /* back to the main menu */
    PAUSE_MENU_ROWS = 3,
    /* practice mode adds three on/off rows under these */
    PAUSE_MENU_ROWS_WITH_TOGGLES = 6,
};

/* The pause menu builds its primitives in the half of a primitive buffer. */
#define PAUSE_PRIM_BUFFER_SIZE (SYS_PRIM_BUFFER_SIZE / 2)

/* The boot image is on screen for 150 vertical blanks and fades out over the
 * last of them. */
#define BOOT_IMAGE_FRAMES 150
#define BOOT_IMAGE_FADE_START 121
/* The boot image's file is a TIM with no colour table: its pixels are at
 * TIM_PIXELS_NO_CLUT. */

/* The field of view, side to side. */
#define DISP_FOV_DEGREES 45

/* How many times sys_PollHost polls the development host. */
#define HOST_POLLS 10000
/* A file on the host's disk is read this much at a time, or a sector at a
 * time as the disc's are. */
#define PCDRV_CHUNK_SIZE 0x4000
#define PCDRV_SECTOR_SIZE 0x800
#define PCDRV_SECTOR_SHIFT 11
/* The device prefix of a host file's name, which is not passed on. */
#define PCDRV_PREFIX_LENGTH 4
#define PCDRV_OPEN_READ 0 /* PCopen's flags for reading only */
/* What the host loaders return when they fail. */
#define PCDRV_READ_FAILED -1
#define PCDRV_NOT_FOUND -2

/* The console's memory, in megabytes. */
#define RAM_MEGABYTES 2

/* The game's random numbers: each is the last one's high half mixed with
 * the last one times RNG_MULTIPLIER plus RNG_INCREMENT. */
#define RNG_MULTIPLIER 5497
#define RNG_INCREMENT 0x7FA9
#define RNG_MAX 0x7FFF

/* The frame's wait: a field is taken as 256 scan lines, and the wait for the
 * vertical blank begins half a field ahead of it. */
#define FIELD_LINES_SHIFT 8
#define HALF_FIELD_LINES 128

/* The stage card's marks appear over this many calls of the loading screen
 * in all. */
#define STAGE_CARD_MARK_CALLS 240

u8 g_eff_initialized;
s32 g_rng_state;
u32 g_disp_prim_buffer_size;
/* Set whenever the pause menu has been open; nothing reads it. */
u8 g_fight_was_paused;
/* Four bytes, of which sys_InitGame clears three, after the two debug
 * options; nothing reads any. (Guess) a debug option's leftover. */
u8 g_sys_unused_boot_flags[4];

/* The first words of the program's data: eight zeroes that nothing reads.
 * What they were for is not known; the name says only where they are.
 * Sony's start-up code (__main) takes their address for the start of the
 * data and of its table of constructors, which is empty. */
s32 g_sys_data_start[8] = {0};

/* The handler of every screen, in the order of enum Screen (sys.h). */
static void (*g_sys_screen_handlers[])(void) = {
    fight_Init,
    fight_Update,
    menu_DebugInit,
    menu_DebugUpdate,
    replay_Start,
    replay_PlayFrame,
    cutscene_Init,
    cutscene_Update,
    menu_Init,
    menu_Update,
    staff_Init,
    staff_Update,
    fight_AfterFightInit,
    fight_AfterFightUpdate,
    sys_UnusedScreen,
    sys_MovieScreen,
    fight_WinnerViewInit,
    fight_WinnerViewUpdate,
    cutscene_EndingInit,
    cutscene_EndingUpdate,
    fight_TeamResultInit,
    fight_TeamResultUpdate,
    story_StageCardInit,
    story_StageCardUpdate,
    fight_AfterMatchInit,
    fight_AfterMatchUpdate,
    menu_SaveInit,
    menu_SaveUpdate,
    cutscene_KaishakuInit,
    cutscene_KaishakuUpdate,
    fight_BestTimesInit,
    fight_BestTimesUpdate,
    fight_KillCountInit,
    fight_KillCountUpdate,
};

/* Gives the development host time to talk. */
void sys_PollHost(void) {
    s32 i;

    for (i = 0; i < HOST_POLLS; i++) {
        pollhost();
    }
}

/* Reads a whole file from the development host's disk, a chunk at a time.
 * `file_name_base` is a name with a four-character device prefix, which is
 * skipped. Returns the size, PCDRV_NOT_FOUND if the file cannot be opened,
 * PCDRV_READ_FAILED if a read fails. Nothing calls it in the retail game.
 * After a failed read the file is closed with the console's own close() and
 * not the host's PCclose(): the original's behaviour. */
static s32 pcdrv_LoadFile(char *file_name_base, u8 *dest) {
    s32 fd;
    s32 total;
    s32 remaining;
    s32 chunk;

    fd = PCopen(file_name_base + PCDRV_PREFIX_LENGTH, PCDRV_OPEN_READ, 0);
    if (fd == -1) {
        return PCDRV_NOT_FOUND;
    }
    total = PClseek(fd, 0, SEEK_END);
    remaining = total;
    PClseek(fd, 0, SEEK_SET);
    while (remaining > 0) {
        chunk = PCDRV_CHUNK_SIZE;
        if (remaining <= PCDRV_CHUNK_SIZE) {
            chunk = remaining;
        }
        if (PCread(fd, dest, chunk) != chunk) {
            close(fd);
            return PCDRV_READ_FAILED;
        }
        remaining -= chunk;
        dest += chunk;
    }
    PCclose(fd);
    return total;
}

/* The same for `count` sectors starting at sector `sector_offset` of the
 * file. Returns the number of bytes read. Nothing calls it either. */
static s32 pcdrv_LoadSectors(
    char *file_name_base, u8 *dest, s32 sector_offset, s32 count) {
    s32 fd;
    s32 i;

    fd = PCopen(file_name_base + PCDRV_PREFIX_LENGTH, PCDRV_OPEN_READ, 0);
    if (fd == -1) {
        return PCDRV_NOT_FOUND;
    }
    PClseek(fd, sector_offset << PCDRV_SECTOR_SHIFT, SEEK_SET);
    for (i = 0; i < count; i++) {
        if (PCread(fd, dest, PCDRV_SECTOR_SIZE) != PCDRV_SECTOR_SIZE) {
            close(fd);
            return PCDRV_READ_FAILED;
        }
        dest += PCDRV_SECTOR_SIZE;
    }
    PCclose(fd);
    return count << PCDRV_SECTOR_SHIFT;
}

/* Half the screen's width over the tangent of half the angle. */
s32 math_FovToScreenDist(s32 fov_degrees) {
    s32 fov_angle = (fov_degrees * ANGLE_TURN) / 360; /* degrees in a turn */
    s32 half_fov_angle = fov_angle / 2;
    s16 cosine =
        g_math_sin_table[(half_fov_angle + ANGLE_TURN / 4) & ANGLE_MASK];
    s16 sine = g_math_sin_table[half_fov_angle & ANGLE_MASK];
    return (cosine * (DISP_WIDTH / 2)) / sine;
}

void disp_SetBackground(s32 clear, s32 red, s32 green, s32 blue) {
    s32 i;

    for (i = 0; i < 2; i++) {
        g_disp_db[i].draw.isbg = clear;
        setRGB0(&g_disp_db[i].draw, red, green, blue);
    }
}

/* The three options kept in the save data's flags. */

u32 disp_IsGrayscale(void) {
    return g_saved_record.flags & SAVE_FLAG_GRAYSCALE;
}

/* The option that weakens CPU opponents (its name on the options screen has
 * not been checked). */
u32 cpu_IsEasy(void) {
    return (g_saved_record.flags >> 1) & 1; /* SAVE_FLAG_EASY_CPU */
}

u32 snd_IsMusicOff(void) {
    return (g_saved_record.flags >> 2) & 1; /* SAVE_FLAG_MUSIC_OFF */
}

/* Save data as it is before anything is unlocked: the starting fighters, an
 * empty staff-roll record, and best times of sixty minutes held by the first
 * fighter. */
static inline void sys_ResetSavedRecord(SaveRecord *rec) {
    s32 i;

    rec->flags = 0;
    rec->unlock_mask = SAVE_DEFAULT_UNLOCKS;
    rec->staff_roll_character_mask = 0;
    for (i = 0; i < SAVE_TIME_COUNT; i++) {
        rec->times[i].holder_character_index = 0;
        rec->times[i].holder_weapon_id = 0;
        /* sixty minutes */
        rec->times[i].time_frames = 60 * 60 * FRAMES_PER_SECOND;
    }
}

/* The save data the game starts with, until a memory card is loaded. The
 * first best time is fifteen minutes. */
static void sys_ResetSaveData(void) {
    g_camera_zoom = 0;
    sys_ResetSavedRecord(&g_saved_record);
    /* fifteen minutes */
    g_saved_record.times[0].time_frames = 15 * 60 * FRAMES_PER_SECOND;
    pad_ResetButtonConfig();
}

void disp_Reset(void) { ResetGraph(1); }

void disp_Blank(void) {
    SetDispMask(0);
    ResetGraph(1);
    ClearImage(&g_disp_full_rect, 0, 0, 0);
    DrawSync(0);
}

void disp_Show(void) { SetDispMask(1); }

/* Empty, and nothing calls it. */
static void sys_Stub(void) {}

/* Starts the counter the main loop times its frames with. */
static void sys_StartFrameTimer(void) { sys_StartHblankCounter(); }

/* Sets up the GPU and the GTE: 640x240, the two buffers one above the other
 * (each is shown while the other is drawn to), a 45-degree field of view
 * centred on the screen, a black background. */
static void disp_Init(void) {
    ResetGraph(0);
    SetGraphDebug(0);
    SetDispMask(0);
    InitGeom();
    SetGeomOffset(DISP_WIDTH / 2, DISP_HEIGHT / 2);
    SetGeomScreen(math_FovToScreenDist(DISP_FOV_DEGREES));
    SetDefDrawEnv(&g_disp_db[0].draw, 0, 0, DISP_WIDTH, DISP_HEIGHT);
    SetDefDrawEnv(&g_disp_db[1].draw, 0, DISP_HEIGHT, DISP_WIDTH, DISP_HEIGHT);
    SetDefDispEnv(&g_disp_db[0].disp, 0, DISP_HEIGHT, DISP_WIDTH, DISP_HEIGHT);
    SetDefDispEnv(&g_disp_db[1].disp, 0, 0, DISP_WIDTH, DISP_HEIGHT);
    disp_SetBackground(1, 0, 0, 0);
    ClearImage(&g_disp_full_rect, 0, 0, 0);
    DrawSync(0);
}

/* Starts the machine: interrupts, the pads (the driver fills the two packet
 * buffers from now on), the display, the CD, the memory card and the frame's
 * timer. */
void sys_Init(void) {
    ResetCallback();
    InitPAD((char *)g_pad_packets[0], PAD_PACKET_SIZE, (char *)g_pad_packets[1],
            PAD_PACKET_SIZE);
    StartPAD();
    ChangeClearPAD(0);
    disp_Init();
    g_disp_loading_screen = LOADING_SCREEN_NONE;
    g_disp_loading_noise_burst = 0;
    cdrom_Init();
    memcard_Init();
    sys_StartFrameTimer();
}

/* Shows the boot image: a 256x48 picture in the middle of a black 320-wide
 * screen, held for two and a half seconds and faded out at the end by
 * halving its colours every frame. `file_buffer` is where the file is read
 * to; the other two parameters are not used. */
static void disp_ShowBootImage(u8 *file_buffer, void *unused1, s32 unused2) {
    RECT rect;
    s32 i;

    rect = g_disp_boot_image_rect;

    SetDispMask(0);
    SetDefDispEnv(&g_disp_db[1].disp, 0, 0, DISP_WIDTH / 2, DISP_HEIGHT);
    cdrom_WaitIdle();
    cdrom_StartRead(
        cdrom_GetFileIndex(CDROM_GROUP_DATA, CDROM_DATA_SEA2_16_TIM),
        (s32)file_buffer);
    cdrom_WaitIdle();
    PutDispEnv(&g_disp_db[1].disp);
    DrawSync(0);
    LoadImage(&rect, (u32 *)(file_buffer + TIM_PIXELS_NO_CLUT));
    DrawSync(0);
    SetDispMask(1);

    for (i = 0; i < BOOT_IMAGE_FRAMES; i++) {
        if (i >= BOOT_IMAGE_FADE_START) {
            /* The header's size is added twice, so the walk starts 20 bytes
             * into the pixels and runs 20 bytes past them: the original's
             * behaviour. */
            u16 *pixel_cursor =
                (u16 *)(file_buffer + TIM_PIXELS_NO_CLUT + TIM_PIXELS_NO_CLUT);
            s32 j;

#if RB_FIX_BOOT_FADE
            /* from the first pixel to the last */
            if (g_rb_settings.bugfixes) {
                pixel_cursor = (u16 *)(file_buffer + TIM_PIXELS_NO_CLUT);
            }
#endif
            for (j = 0; j < rect.w * rect.h; j++, pixel_cursor++) {
                u16 pixel = *pixel_cursor;
                s32 red = pixel & PIXEL_PART_MASK;

                *pixel_cursor =
                    (red >> 1) +
                    ((((pixel >> PIXEL_GREEN_SHIFT) & PIXEL_PART_MASK) >> 1)
                     << PIXEL_GREEN_SHIFT) +
                    ((((pixel >> PIXEL_BLUE_SHIFT) & PIXEL_PART_MASK) >> 1)
                     << PIXEL_BLUE_SHIFT);
            }
            LoadImage(&rect, (u32 *)(file_buffer + TIM_PIXELS_NO_CLUT));
        }

        DrawSync(0);
        VSync(0);
    }

    SetDispMask(0);
    SetDefDispEnv(&g_disp_db[1].disp, 0, 0, DISP_WIDTH, DISP_HEIGHT);
    SetDispMask(1);
}

void sys_Panic(void) {
    printf("OVER FLOW\n");
    while (1) {
        sys_PollHost();
    }
}

/* The effects' textures have to be loaded again: something else has used
 * their video memory. */
void eff_ClearInitFlag(void) { g_eff_initialized = 0; }

/* Loads the effects' textures and tables if they are not loaded. */
void eff_Init(void) {
    s32 size;

    if (g_eff_initialized) {
        return;
    }
    size = eff_LoadTextures(SYS_PRIM_BUFFER_0, SYS_EFFECTS_ADDR);
    printf("eff_init:%08x size:%08x\n", SYS_EFFECTS_ADDR, size);
    if (SYS_EFFECTS_SIZE < size) {
        sys_Panic();
    }
    g_eff_initialized = 1;
}

/* Starts the sound system and loads the common bank: its samples go to the
 * sound chip and what must stay in memory is copied to its place under the
 * effects' textures. */
void snd_Startup(void) {
    s32 size;

    snd_Init();
    /* read into a primitive buffer, which nothing is using yet */
    size = snd_LoadCommonVab(SYS_PRIM_BUFFER_0);
    if (size > SYS_SOUND_VAB_SIZE) {
        sys_Panic();
    }
    memcpy((u8 *)SYS_SOUND_VAB_ADDR, (u8 *)SYS_PRIM_BUFFER_0, size);
    snd_RebaseVab(SYS_SOUND_VAB_ADDR - SYS_PRIM_BUFFER_0, SND_VAB_COMMON);
    snd_SetDefaultOutput();
    g_snd_initialized = 1;
}

/* Sets the game up, once: the save data it starts with, the buffers, the
 * sound, the pads, the font, the fight's settings and the engine. */
static void sys_InitGame(void) {
    /* the program has to end below the common sound bank */
    printf("LIMIT:%08x\n", SYS_SOUND_VAB_ADDR);
    sys_ResetSaveData();
    motion_InitBuffers();
    g_disp_prim_buffers[0] = SYS_PRIM_BUFFER_0;
    g_disp_prim_buffers[1] = SYS_PRIM_BUFFER_1;
    g_disp_prim_buffer_size = SYS_PRIM_BUFFER_SIZE;
#if RB_REMASTER_VIEW
    /* Where the run asks the remastered picture to draw more of the
     * stage than the console does, the port's own primitive buffers take
     * the place of the console's two, which cannot grow where they are
     * (port_remaster.h). The run's draw distance is settled before the
     * game starts, so this is asked once. */
    if (port_RemasterPrimBuffer(0) != NULL) {
        g_disp_prim_buffers[0] = (u32)port_RemasterPrimBuffer(0);
        g_disp_prim_buffers[1] = (u32)port_RemasterPrimBuffer(1);
        g_disp_prim_buffer_size = port_RemasterPrimBufferBytes();
    }
#endif
    g_eff_initialized = 0;
    g_snd_initialized = 0;
    snd_Startup();
    pad_Startup();
    font_LoadImage();
    fight_InitConfig();
    g_menu_debug_m_option = 0;
    g_fight_debug_view = 0;
    g_sys_unused_boot_flags[0] = 0;
    g_sys_unused_boot_flags[1] = 0;
    g_sys_unused_boot_flags[2] = 0;
    sys_InitEngineState();
    g_sys_frame_period = 2; /* 30 frames a second */
    g_motion_loaded_sets[1] = 0;
    g_fight_was_paused = 0;
    g_link_aborted = 0;
}

/* Waits for the GPU to finish drawing. Nothing calls it. */
static void disp_WaitDraw(void) { DrawSync(0); }

/* The pause menu. Runs its own frame loop over the frozen picture of the
 * fight until a row is chosen: triangle resumes, cross takes the row under
 * the cursor. In the first kind of practice (against the partner that stands
 * there) three more rows switch the practice options on and off with left
 * and right.
 * `paused_frame_db` is the display buffer holding the fight's last frame,
 * which is drawn again under the menu every frame; `paused_db_index`, its
 * number, is not used. */
static void fight_RunPauseMenu(GpuDb *paused_frame_db, s32 paused_db_index) {
    u32 local_ot[2];
    GpuDb *current_db;
    s32 selected_row;
    s32 has_toggle_rows;
    s32 row_count;
    u32 prim_buffer_addr;
    s32 index;
    GpuDb *paused_db;

    selected_row = 0;
    has_toggle_rows = 0;
    /* FAKE: a copy of the parameter; use paused_frame_db */
    paused_db = paused_frame_db;
    if (g_fight_mode == FIGHT_MODE_PRACTICE) {
        has_toggle_rows = g_fight_practice_round < 1;
    }
    row_count = PAUSE_MENU_ROWS;
    if (has_toggle_rows) {
        row_count = PAUSE_MENU_ROWS_WITH_TOGGLES;
    }

    g_fight_was_paused = 1;
    snd_Play(MENU_SOUND_PAUSE, SND_VOLUME_MAX, SND_VOLUME_MAX);
    /* the buffer the fight's last frame is not in */
    prim_buffer_addr = g_disp_prim_buffers[g_disp_frame_count & 1];

    while (1) {
        index = g_disp_frame_count & 1;
        g_disp_prim_cursor =
            (u32 *)ADDR(prim_buffer_addr + (index * PAUSE_PRIM_BUFFER_SIZE));
        g_disp_ot = &local_ot[index];
        current_db = &g_disp_db[index];

        ClearOTagR(g_disp_ot, 1);
        pad_Update();
        if (has_toggle_rows) {
            /* the options' bits ride above the row */
            hud_DrawPanel(HUD_PANEL_PAUSE_OPTIONS,
                          selected_row | (g_fight_practice_options << 16),
                          (s32)g_disp_prim_cursor, 0);
        } else {
            hud_DrawPanel(
                HUD_PANEL_PAUSE, selected_row, (s32)g_disp_prim_cursor, 0);
        }
        cdrom_Update();
        snd_FlushQueue();
        DrawSync(0);
        VSync(2);
        /* FAKE: do-while(0); read as two plain calls */
        do {
            PutDispEnv(&current_db->disp);
            PutDrawEnv(&current_db->draw);
        } while (0);
#if RB_REMASTER_VIEW
        /* the remastered picture puts the fight's last frame, which it
         * kept, here instead of drawing this table again
         * (port_remaster.h) */
        port_RemasterPause(
            PORT_PAUSE_FROZEN, paused_db->draw.clip.x, paused_db->draw.clip.y);
        DrawOTag(&paused_db->ot[DISP_OT_SIZE - 1]);
        port_RemasterPause(PORT_PAUSE_PANEL, 0, 0);
#else
        DrawOTag(&paused_db->ot[DISP_OT_SIZE - 1]);
#endif
        DrawOTag(g_disp_ot);
        g_disp_frame_count++;

        if (g_pad_state.pressed & PAD_ANY(PADRup)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            selected_row = PAUSE_MENU_RESUME;
            break;
        }
        if (g_pad_state.pressed & PAD_ANY(PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            break;
        }
        if (g_pad_state.pressed & PAD_ANY(PADLup)) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            selected_row =
                (selected_row == 0) ? row_count - 1 : selected_row - 1;
        } else if (g_pad_state.pressed & PAD_ANY(PADLdown)) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            selected_row =
                (selected_row == row_count - 1) ? 0 : selected_row + 1;
        }

        if (has_toggle_rows && (selected_row >= PAUSE_MENU_ROWS)) {
            if (g_pad_state.pressed & PAD_ANY(PADLleft)) {
                snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                g_fight_practice_options |=
                    1 << (selected_row - PAUSE_MENU_ROWS);
            } else if (g_pad_state.pressed & PAD_ANY(PADLright)) {
                snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                g_fight_practice_options &=
                    ~(1 << (selected_row - PAUSE_MENU_ROWS));
            }
        }
    }
#if RB_REMASTER_VIEW
    port_RemasterPause(PORT_PAUSE_OFF, 0, 0);
#endif

    if (selected_row != PAUSE_MENU_RESUME) {
        if (selected_row == PAUSE_MENU_SUB_MENU) {
            g_menu_reopen_mode = 1;
            g_sys_screen = SCREEN_MENU_INIT;
            cdrom_StopAndWait();
        } else if (selected_row == PAUSE_MENU_TOP_MENU) {
            g_sys_screen = SCREEN_MENU_INIT;
            cdrom_StopAndWait();
        }
    }

    DrawSync(0);
    ResetRCnt(RCntCNT1);
    g_fight_was_paused = 1;
}

void rng_SetSeed(s32 seed) { g_rng_state = seed; }

s32 rng_Next(void) {
    s32 state = g_rng_state;
    s32 stepped = state * RNG_MULTIPLIER + RNG_INCREMENT;
    state = (state >> 16) ^ stepped;
    g_rng_state = state;
    return state & RNG_MAX;
}

/* Starts the machine and the game, shows the boot image, then runs a frame
 * after another for ever, beginning with the screen that plays the opening
 * movie. */
void main(void) {
    s32 index;
    GpuDb *frame_db;
    u32 *frame_ot;
    /* whether this frame went unshown; the next frame's pad_Update is told.
     * Nothing sets it before the first frame: the original's behaviour. */
    s32 skipped;

    __main();
    SetSp(SYS_STACK_TOP);
    SetMem(RAM_MEGABYTES);
    sys_Init();
    sys_InitGame();
    SetDispMask(1);
#if RB_PORT
    if (port_QuickFight() != NULL) {
        /* --fight: no options page, boot image or movie; the first screen
         * is the fight's, as in the quick-boot build */
        const PortFight *fight = port_QuickFight();

        rb_StartFight(fight->mode, fight->roster, fight->weapon, fight->stage,
                      fight->is_cpu[1], g_rb_settings.bugfixes);
    } else {
        rb_RunOptionsPage();
        disp_ShowBootImage((u8 *)SYS_LOAD_ADDR, frame_db, index);
        g_sys_screen = SCREEN_MOVIE;
    }
#elif RB_OPTIONS_PAGE && !RB_QUICK_BOOT
    /* the settings are read from the card and the player may change them */
    rb_RunOptionsPage();
#endif
#if RB_QUICK_BOOT
    /* no boot image and no movie: the first screen is the fight's */
    rb_QuickBoot();
#elif !RB_PORT
    /* only the first argument is used; the others are whatever was there
     * (the original's behaviour) */
    disp_ShowBootImage((u8 *)SYS_LOAD_ADDR, frame_db, index);

    g_sys_screen = SCREEN_MOVIE;
#endif
    g_sys_skip_frames = 0;
    g_disp_frame_count = 0;
#if RB_FIX_FIRST_FRAME_PAD
    /* no frame has gone unshown yet: the first pad_Update was told
     * whatever the register held */
    if (g_rb_settings.bugfixes) {
        skipped = 0;
    }
#endif

    for (;;) {
        /* A fresh ordering table and primitive buffer for this frame. */
        index = g_disp_frame_count & 1;
        frame_db = &g_disp_db[index];
        frame_ot = frame_db->ot;
        ClearOTagR(frame_ot, DISP_OT_SIZE);
        g_disp_ot = frame_ot;
        g_disp_prim_cursor = (u32 *)ADDR(g_disp_prim_buffers[index]);
        eff_SelectPrimBuffer(index);
        font_BeginFrame();
        pad_Update(skipped);
        cdrom_Update();
        snd_FlushQueue();

        /* The link cable went dead: back to the main menu. */
        if (g_link_aborted) {
            cdrom_StopAndWait();
            g_disp_loading_screen = LOADING_SCREEN_NONE;
            g_link_aborted = 0;
            g_menu_reopen_mode = 0;
            g_sys_screen = SCREEN_MENU_INIT;
        }

        g_sys_screen_handlers[g_sys_screen]();
        font_AddTpage();

        /* Wait out the frame: until the counter of scan lines is half a
         * field short of g_sys_frame_period fields, then for the vertical
         * blank. rand() is stirred meanwhile, so that it depends on how
         * long each frame took. */
        while (GetRCnt(RCntCNT1) <
               ((g_sys_frame_period - 1) << FIELD_LINES_SHIFT) +
                   HALF_FIELD_LINES) {
            rand();
        }

        VSync(1);
        DrawSync(0);
        VSync(0);
        ResetRCnt(RCntCNT1);

        skipped = g_sys_skip_frames;
        if (skipped == 0) {
            PutDispEnv(&frame_db->disp);
            PutDrawEnv(&frame_db->draw);
        }

        {
#if RB_REMASTER_VIEW
            s32 remaining =
                (s32)(g_disp_prim_buffers[index] + g_disp_prim_buffer_size) -
                (s32)g_disp_prim_cursor;
#else
            s32 remaining = g_disp_prim_buffers[index] + SYS_PRIM_BUFFER_SIZE -
                            (s32)g_disp_prim_cursor;
#endif
            if (remaining < g_sys_prim_low_water) {
                g_sys_prim_low_water = remaining;
            }
#if RB_REMASTER_VIEW
            /* the least the frame left of its primitive buffer, for the
             * report of every limit the drawing path has
             * (port_remaster.h) */
            port_RemasterNoteRoom(remaining, (s32)g_disp_prim_buffer_size);
#endif
            if (remaining < 0) {
                printf("common prim over flow\n");
                while (1) {
                    sys_PollHost();
                }
            }
        }

        if (g_sys_skip_frames != 0) {
            g_sys_skip_frames--;
        } else {
            DrawOTag(&frame_db->ot[DISP_OT_SIZE - 1]);
            g_disp_frame_count++;
        }

        /* Pause the fight on Start, and in practice mode by itself two
         * frames in (so that practice opens on its menu). */
        if (g_sys_screen == SCREEN_FIGHT_UPDATE && skipped == 0) {
            if ((g_pad_state.pressed & PAD_ANY(PADstart)) ||
                (g_fight_mode == FIGHT_MODE_PRACTICE &&
                 g_fight_auto_pause_countdown != 0 &&
                 --g_fight_auto_pause_countdown == 0)) {
                fight_RunPauseMenu(frame_db, index);
            }
        }
    }
}

/* Draws the current loading screen (g_disp_loading_screen) straight into the
 * buffer on show, with its own small ordering table. The loaders call it
 * between files, so the picture moves while the disc is read. */
void disp_DrawLoadingScreen(void) {
    u32 local_ot[2];
    DRAWENV env;
    /* FAKE: one local for the number of noise streaks and the stage's mark */
    s32 work;
    s32 prim_addr;
    s32 screen;

    prim_addr = (s32)g_shared_buffer;
    if (g_disp_loading_screen == LOADING_SCREEN_NONE) {
        return;
    }
    SetDefDrawEnv(&env, 0, (g_disp_frame_count & 1) ? DISP_HEIGHT : 0,
                  DISP_WIDTH, DISP_HEIGHT);
    env.isbg = 0;
    PutDrawEnv(&env);
    g_disp_ot = local_ot;
    ClearOTagR(local_ot, 2);
    screen = g_disp_loading_screen;
    switch (screen) {
    case LOADING_SCREEN_PLAIN:
    case LOADING_SCREEN_NEXT_OPPONENT:
        /* The picture, with noise streaks over it: now and then one, or four
         * to seven every call during a burst. */
        prim_addr = hud_DrawLoadingPicture(prim_addr, screen);
        if (g_disp_loading_noise_burst) {
            s32 i;
            work = (rand() & 3) + 4;
            for (i = 0; i < work; i++) {
                prim_addr =
                    hud_DrawLoadingNoise(prim_addr, g_disp_loading_screen);
            }
        } else if ((rand() & 7) == 0) { /* one call in eight */
            hud_DrawLoadingNoise(prim_addr, g_disp_loading_screen);
        }
        break;
    case LOADING_SCREEN_TEAM_SCORES:
        hud_DrawTeamScores(g_team_packed_scores, prim_addr, 0);
        break;
    case LOADING_SCREEN_STAGE_CARD: {
        /* The story's stage card (story.h): one more stage of the route is
         * marked each time the timer runs out; the marks share the calls
         * between them, and stay in the buffer once drawn. */
        u8 drawn;
        s32 chapter;

        chapter = g_story_chapter;
        drawn = g_story_route_drawn;
        if ((u32)chapter < drawn) {
            break;
        }
        if (STAGE_CARD_MARK_CALLS / (chapter + 1) >= ++g_story_route_timer) {
            break;
        }
        if (drawn == chapter) {
            g_story_route_drawn = drawn + 1;
        } else {
            u8 next = drawn + 1;
            g_story_route_drawn = next;
            g_story_route_timer = 0;
            work = g_story_route[drawn];
            if (next == chapter) {
                work |= HUD_CARD_MARK_LAST; /* where the story is now */
            }
            hud_DrawStageCardMark(work, prim_addr, 0);
        }
        break;
    }
    }
    DrawOTag(g_disp_ot + 1);
    DrawSync(0);
}

/* The whole of video memory's display area, for clearing both buffers. */
static RECT g_disp_full_rect = {0, 0, DISP_WIDTH, 2 * DISP_HEIGHT};
/* Where the boot image goes on the 320-wide screen. */
static RECT g_disp_boot_image_rect = {32, 96, 256, 48};
/* The least room a frame has left in its primitive buffer so far. */
static s32 g_sys_prim_low_water = SYS_PRIM_BUFFER_SIZE;
/* When the program was built, and a number (400) that is not understood.
 * Nothing reads either. */
#if RB_PORT
/* The port has no place for the text's address: a 64-bit program's
 * compiler cannot put an address into a 32-bit word of a table it fills
 * (docs/PORT.md, "64 bits"). */
static s32 g_sys_build_date_ref[2] = {0, 400};
#else
static s32 g_sys_build_date_ref[2] = {(s32) "Fri Aug  7 22:26:32 1998\n", 400};
#endif

/* Small variables this file's code uses. Many belong to other subsystems;
 * they are defined here because a file reaches a small variable through the
 * gp register only if it defines it, and this file's code does. */
u8 g_fight_debug_view;
u8 g_disp_loading_noise_burst;
s32 g_disp_frame_count;
u8 g_sys_frame_period;
u8 g_menu_debug_m_option;
s16 g_camera_zoom;
/* Practice mode: frames until the fight pauses itself. */
u8 g_fight_auto_pause_countdown;
u32 *g_disp_ot;
u8 g_disp_loading_screen;
u32 g_disp_prim_buffers[2];
s32 g_team_packed_scores;
u8 g_fight_practice_options;
u8 g_story_chapter;
s32 g_story_route_timer;
s16 g_sys_screen;
u8 g_fight_practice_round;
u32 *g_disp_prim_cursor;
s16 g_fight_mode;
u8 g_story_route_drawn;
u8 g_snd_initialized;
u8 g_sys_skip_frames;
u8 g_link_aborted;
