/* A frame's lights made from where they come from (gl_light_list.h;
 * docs/PORT.md, "Where the lights come from"). Nothing here is the game's
 * state or changes a value the game computes: what it reads of the game
 * comes through the side channel (port_lights.h, port_fighter.h,
 * port_remaster.h). */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "gl_light_list.h"
#include "gl_lights.h"
#include "gl_time.h"
#include "port_fighter.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_lights.h"
#include "port_mods.h"
#include "port_remaster.h"

/* the console's 1.12 numbers */
#define ONE_12 4096.0f
/* a light's id: what it is in the top bits, the rest which */
#define ID_STAGE 0x10000000u
#define ID_FLASH 0x20000000u
#define ID_GLOW 0x30000000u
#define ID_STAGE_SHIFT 16
#define ID_LIGHT_SHIFT 4
#define ID_EVENT_MASK 0x00FFFFFFu
#define ID_GLOW_SHIFT 8
/* The frames the stage's tiles may go undrawn before its lights go out:
 * a menu over a stage still loaded. And the effects' frames likewise. */
#define STALE_FRAMES 30

/* The game's point light (eff.c): how long it lasts, and how it lights
 * here -- a flash of the clash's own colour, strongest the frame it
 * starts and gone with the game's own. */
#define FLASH_FRAMES 10.0f
#define FLASH_INTENSITY 1.5f
#define FLASH_RADIUS 4500.0f
/* a glowing quad of the effects: a small light at its place */
#define GLOW_INTENSITY 0.4f
#define GLOW_RADIUS 1600.0f

/* The flames' kinds (eff.h, enum FlameKind): how much farther than the
 * light file's radius each reaches, a fire being larger than a torch; its
 * intensity is the file's. */
static const float g_flame_reach[4] = {1.5f, 1.1f, 1.0f, 1.25f};
/* and half of each one's height in the world (eff.c, FLAME_..._HEIGHT):
 * its place is the flame's foot, the light is in its middle */
static const float g_flame_middle[4] = {112.0f, 60.0f, 30.0f, 84.0f};

/* The mesh's dump (tools/port_lights.py). */
#define DUMP_MAGIC "RBLM"
#define DUMP_VERSION 2
#define PATH_SIZE 1024
#define STAGES_MAX 64

static struct {
    s32 is_stage_off; /* the settings, off when set */
    s32 is_effects_off;
    s32 last_frames;  /* the stage view's frames last seen */
    s32 still_frames; /* calls since they changed */
    u32 last_serial;  /* the effects' serial last seen */
    s32 still_serial;
    s32 made;       /* lights given this frame */
    s32 said_stage; /* the stage and count last told the log, plus one */
    s32 said_made;
    u8 dumped[STAGES_MAX];
} g_light_list;

void port_GlLightListSetStageOn(s32 on) { g_light_list.is_stage_off = !on; }

void port_GlLightListSetEffectsOn(s32 on) { g_light_list.is_effects_off = !on; }

/* One light into the frame's list, counted. */
static void port_GlLightListAdd(const PortGlLight *light) {
    if (port_GlLightsAdd(light) >= 0) {
        g_light_list.made++;
    }
}

/* A colour as a screen shows it, 0 to 255, made linear (sRGB's curve, near
 * enough: the window's build has no mathematics library). */
static float port_GlLightLinear(u8 value) {
    float s = (float)value / 255.0f;

    return s * (s * (s * 0.305306011f + 0.682171111f) + 0.012522878f);
}

/* `offset` turned by a 1.12 matrix and added to `place`. */
static void port_GlLightTurn(
    float *out, const s16 *matrix, const s32 *place, const s32 *offset) {
    s32 row;

    for (row = 0; row < 3; row++) {
        out[row] = (float)place[row] +
                   ((float)matrix[row * 3 + 0] * (float)offset[0] +
                    (float)matrix[row * 3 + 1] * (float)offset[1] +
                    (float)matrix[row * 3 + 2] * (float)offset[2]) /
                       ONE_12;
    }
}

static void port_GlLightTurnOnly(
    float *out, const s16 *matrix, const s32 *direction) {
    static const s32 nought[3] = {0, 0, 0};

    port_GlLightTurn(out, matrix, nought, direction);
}

/* The light a definition makes, but for where it is. */
static void port_GlLightFrom(
    PortGlLight *light, const PortLightDef *def, u32 id) {
    memset(light, 0, sizeof(*light));
    light->colour[0] = port_GlLightLinear(def->color[0]);
    light->colour[1] = port_GlLightLinear(def->color[1]);
    light->colour[2] = port_GlLightLinear(def->color[2]);
    light->intensity = (float)def->intensity / PORT_LIGHT_ONE;
    light->radius = (float)def->radius;
    light->kind =
        def->kind == PORT_LIGHT_SPOT ? PORT_GL_LIGHT_SPOT : PORT_GL_LIGHT_POINT;
    light->direction[0] = (float)def->direction[0];
    light->direction[1] = (float)def->direction[1];
    light->direction[2] = (float)def->direction[2];
    light->cone_inner = (float)def->cone_inner;
    light->cone_outer = (float)def->cone_outer;
    light->flicker = (float)def->flicker / PORT_LIGHT_ONE;
    light->casts_shadow = def->shadow;
    light->id = id;
}

/* Where a fighter is this frame: his bone's matrix and place, or his
 * body's middle (the mean of his bones) with no turn. FALSE where his
 * body was not drawn. */
static s32 port_GlLightFighter(
    const PortLightDef *def, float *out, float *direction) {
    static const s16 level[9] = {4096, 0, 0, 0, 4096, 0, 0, 0, 4096};
    u32 serial = port_FighterSerial();
    const PortFighterView *view = port_GetFighterView(def->on_index);
    s32 middle[3] = {0, 0, 0};
    s32 bone;

    if (view == NULL || view->model_id < 0 || serial - view->serial > 1) {
        return FALSE;
    }
    if (def->on_bone >= 0 && def->on_bone < PORT_FIGHTER_BONES) {
        const PortFighterMatrix *at = &view->bone[def->on_bone];

        port_GlLightTurn(out, at->m, at->t, def->at);
        port_GlLightTurnOnly(direction, at->m, def->direction);
        return TRUE;
    }
    for (bone = 0; bone < PORT_FIGHTER_BONES; bone++) {
        middle[0] += view->bone[bone].t[0] / PORT_FIGHTER_BONES;
        middle[1] += view->bone[bone].t[1] / PORT_FIGHTER_BONES;
        middle[2] += view->bone[bone].t[2] / PORT_FIGHTER_BONES;
    }
    port_GlLightTurn(out, level, middle, def->at);
    port_GlLightTurnOnly(direction, level, def->direction);
    return TRUE;
}

/* Where the thing a light is fastened to puts it: FALSE where that thing
 * is not in the frame. The stage's and a flame's are done by the caller. */
static s32 port_GlLightPlace(
    const PortLightDef *def, float *out, float *direction) {
    s32 count = 0;

    switch (def->on) {
    case PORT_LIGHT_ON_FIGHTER:
        return port_GlLightFighter(def, out, direction);
    case PORT_LIGHT_ON_WEAPON: {
        const PortFighterWeapon *weapons = port_GetFighterWeapons(&count);
        u32 serial = port_FighterSerial();

        if (weapons == NULL || def->on_index >= count ||
            serial - weapons[def->on_index].serial > 1) {
            return FALSE;
        }
        port_GlLightTurn(out, weapons[def->on_index].world.m,
                         weapons[def->on_index].world.t, def->at);
        port_GlLightTurnOnly(
            direction, weapons[def->on_index].world.m, def->direction);
        return TRUE;
    }
    case PORT_LIGHT_ON_PIECE: {
        const PortStagePiece *pieces = port_RemasterStagePieces(&count);

        if (pieces == NULL || def->on_index >= count) {
            return FALSE;
        }
        port_GlLightTurn(out, pieces[def->on_index].rotation,
                         pieces[def->on_index].place, def->at);
        port_GlLightTurnOnly(
            direction, pieces[def->on_index].rotation, def->direction);
        return TRUE;
    }
    default:
        return FALSE;
    }
}

/* The stage's lights: its files', each on what it is fastened to. */
static void port_GlLightListStage(s32 stage) {
    const PortLightSet *set = port_LightsOfStage(stage);
    const PortLightsGame *game = port_LightsGame();
    s32 i;

    if (set == NULL) {
        return;
    }
    for (i = 0; i < set->count; i++) {
        const PortLightDef *def = &set->light[i];
        u32 id =
            ID_STAGE | (u32)stage << ID_STAGE_SHIFT | (u32)i << ID_LIGHT_SHIFT;
        PortGlLight light;
        /* out at a time of day its file leaves out, and brighter as the
         * day darkens (gl_time.h) */
        float gain = port_GlTimeLightGain(def->times);

        if (gain <= 0.0f) {
            continue;
        }
        port_GlLightFrom(&light, def, id);
        light.intensity *= gain;
        if (def->on == PORT_LIGHT_ON_STAGE) {
            light.position[0] = (float)def->at[0];
            light.position[1] = (float)def->at[1];
            light.position[2] = (float)def->at[2];
            port_GlLightListAdd(&light);
        } else if (def->on == PORT_LIGHT_ON_FLAME) {
            s32 flame;

            if (game->flame_stage != stage) {
                continue;
            }
            for (flame = 0; flame < game->flame_count; flame++) {
                const PortLightFlame *at = &game->flame[flame];
                float reach = g_flame_reach[at->kind & 3];

                if (def->on_index >= 0 && def->on_index != flame) {
                    continue;
                }
                port_GlLightFrom(&light, def, id | (u32)flame);
                light.intensity *= gain;
                light.position[0] = (float)(at->at[0] + def->at[0]);
                /* the world's y runs down: the middle is above the foot */
                light.position[1] = (float)(at->at[1] + def->at[1]) -
                                    g_flame_middle[at->kind & 3];
                light.position[2] = (float)(at->at[2] + def->at[2]);
                light.radius *= reach;
                port_GlLightListAdd(&light);
            }
        } else if (port_GlLightPlace(def, light.position, light.direction)) {
            port_GlLightListAdd(&light);
        }
    }
}

/* A glowing quad's colour (eff.c, eff_DrawFlash: the quads' own). */
static void port_GlLightGlowColor(s32 flash, float *rgb) {
    switch (flash) {
    case 3: /* the hit's flash: red turning yellow */
    case 4:
        rgb[0] = 1.0f;
        rgb[1] = 0.35f;
        rgb[2] = 0.12f;
        break;
    case 0: /* a lock of blades: pale blue */
    case 5:
        rgb[0] = 0.22f;
        rgb[1] = 0.22f;
        rgb[2] = 1.0f;
        break;
    case 1: /* a clash's spark: pale red */
    case 2:
        rgb[0] = 1.0f;
        rgb[1] = 0.22f;
        rgb[2] = 0.22f;
        break;
    default: /* the streaks, the blunt hits, the armour's sparks: white */
        rgb[0] = 1.0f;
        rgb[1] = 0.95f;
        rgb[2] = 0.85f;
        break;
    }
}

/* The fight's effects: the game's point light, and the glowing quads. */
static void port_GlLightListEffects(void) {
    const PortLightsGame *game = port_LightsGame();
    PortGlLight light;
    s32 i;

    if (game->serial != g_light_list.last_serial) {
        g_light_list.last_serial = game->serial;
        g_light_list.still_serial = 0;
    } else if (g_light_list.still_serial < STALE_FRAMES) {
        g_light_list.still_serial++;
    }
    if (g_light_list.still_serial >= STALE_FRAMES) {
        return;
    }
    if (game->flash_on && game->flash_frames > 0) {
        memset(&light, 0, sizeof(light));
        light.position[0] = (float)game->flash_at[0];
        light.position[1] = (float)game->flash_at[1];
        light.position[2] = (float)game->flash_at[2];
        light.colour[0] = port_GlLightLinear((u8)(game->flash_color >> 16));
        light.colour[1] = port_GlLightLinear((u8)(game->flash_color >> 8));
        light.colour[2] = port_GlLightLinear((u8)game->flash_color);
        light.intensity =
            FLASH_INTENSITY * (float)game->flash_frames / FLASH_FRAMES;
        light.radius = FLASH_RADIUS;
        light.kind = PORT_GL_LIGHT_POINT;
        light.casts_shadow = TRUE;
        light.id = ID_FLASH | (game->flash_event & ID_EVENT_MASK);
        port_GlLightListAdd(&light);
    }
    for (i = 0; i < game->glow_count; i++) {
        memset(&light, 0, sizeof(light));
        light.position[0] = (float)game->glow[i].at[0];
        light.position[1] = (float)game->glow[i].at[1];
        light.position[2] = (float)game->glow[i].at[2];
        port_GlLightGlowColor(game->glow[i].flash, light.colour);
        light.intensity = GLOW_INTENSITY;
        light.radius = GLOW_RADIUS;
        light.kind = PORT_GL_LIGHT_POINT;
        light.id = ID_GLOW | (u32)game->glow[i].flash << ID_GLOW_SHIFT;
        port_GlLightListAdd(&light);
    }
}

void port_GlLightListFrame(void) {
    const PortStageView *view = port_GetRemasterStageView();
    s32 stage = port_RemasterStageNumber();

    if (view != NULL && view->frames != g_light_list.last_frames) {
        g_light_list.last_frames = view->frames;
        g_light_list.still_frames = 0;
    } else if (g_light_list.still_frames < STALE_FRAMES) {
        g_light_list.still_frames++;
    }
    port_GlLightsBegin();
    g_light_list.made = 0;
    if (stage >= 0 && g_light_list.still_frames < STALE_FRAMES) {
        if (!g_light_list.is_stage_off) {
            port_GlLightListStage(stage);
        }
        if (!g_light_list.is_effects_off) {
            port_GlLightListEffects();
        }
    }
    port_GlLightsEnd();
    /* the run's log: the stage's lights as they first stand, and as the
     * count of a stage's own changes (a flash comes and goes in it) */
    if (stage >= 0 && g_light_list.made > 0 &&
        (stage + 1 != g_light_list.said_stage ||
         g_light_list.made > g_light_list.said_made)) {
        g_light_list.said_stage = stage + 1;
        g_light_list.said_made = g_light_list.made;
        fprintf(stderr, "lights: stage %d's frame has %d lights\n", stage,
                g_light_list.made);
    }
}

/* ---- the mesh's dump ---- */

static void port_GlLightPutWord(FILE *file, u32 word) {
    u8 bytes[4];

    bytes[0] = (u8)word;
    bytes[1] = (u8)(word >> 8);
    bytes[2] = (u8)(word >> 16);
    bytes[3] = (u8)(word >> 24);
    fwrite(bytes, 1, sizeof(bytes), file);
}

void port_GlLightListDumpMesh(
    const void *vertices, s32 stride, s32 count, s32 pieces_from) {
    const char *folder = port_ModsGetDumpFolder();
    s32 stage = port_RemasterStageNumber();
    const u16 *vram = g_port_gl_vram != NULL ? g_port_gl_vram : g_port_vram;
    char path[PATH_SIZE];
    FILE *file;

    if (folder == NULL || stage < 0 || stage >= STAGES_MAX ||
        g_light_list.dumped[stage] || vertices == NULL || count <= 0) {
        return;
    }
    g_light_list.dumped[stage] = TRUE;
    snprintf(path, sizeof(path), "%s/meshes", folder);
    port_HostMakeFolder(path);
    snprintf(path, sizeof(path), "%s/meshes/stage_%02d.bin", folder, stage);
    file = fopen(path, "wb");
    if (file == NULL) {
        return;
    }
    /* the header, then the vertices as gl_stage.c has them (StageVertex),
     * then video memory, 1024 by 512 pixels of 16 bits */
    fwrite(DUMP_MAGIC, 1, 4, file);
    port_GlLightPutWord(file, DUMP_VERSION);
    port_GlLightPutWord(file, (u32)stage);
    port_GlLightPutWord(file, (u32)count);
    port_GlLightPutWord(file, (u32)stride);
    port_GlLightPutWord(file, (u32)pieces_from);
    fwrite(vertices, (size_t)stride, (size_t)count, file);
    fwrite(vram, sizeof(u16), PORT_VRAM_PIXELS, file);
    /* and the stage's textures as they were loaded (PortStageTexture):
     * the row a face names a colour table by is the one the fog writes
     * over each frame, and the table itself is where it was loaded */
    {
        u32 version = 0;
        s32 textures = 0;
        const PortStageTexture *texture =
            port_RemasterStageTextures(&textures, &version);

        port_GlLightPutWord(file, (u32)textures);
        fwrite(texture, sizeof(PortStageTexture), (size_t)textures, file);
    }
    fclose(file);
}
