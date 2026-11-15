/* Resources: reading what a fight needs out of the archive into arena blocks
 * and turning each file's offsets into pointers. One block per kind of thing:
 * two per fighter model, one for the weapons, the stage, the tiny model and
 * a cutscene's motion. Each kind has a load, a free and the callback the
 * arena calls when the block has been moved. Deciding which weapons to load
 * is in resource_weapon.c. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static void resource_OnModelMoved(s32 block_id, s32 delta);
static void resource_OnWeaponsMoved(s32 block_id, s32 delta);
static void resource_OnStageMoved(s32 block_id, s32 delta);
static void resource_OnTinyModelMoved(s32 block_id, s32 delta);
static void resource_OnCutsceneMoved(s32 block_id, s32 delta);

/* Offsets in a file's header are used rounded down to a whole word.
 * SECTION is what is `offset` into the file at `file`, as a pointer to
 * words; SECTION_ADDR the same as an address. */
#define SECTION(file, offset) ((s32 *)((u8 *)ADDR(file) + ALIGN4(offset)))
#define SECTION_ADDR(file, offset) ((s32)SECTION(file, offset))

/* A stage file's header: the number of the last offset, then the offsets
 * of its sections. The last is the textures' and the one before it the
 * cells'; the ones with a fixed place are these. */
enum StageFileSection {
    STAGE_FILE_MESHES = 1,
    STAGE_FILE_COLLISION = 2,
    STAGE_FILE_UNUSED_LIST = 3,
    STAGE_FILE_OBSTACLES = 4,
    /* in the stages with props: the props' meshes and their motion */
    STAGE_FILE_PROP_MESHES = 5,
    STAGE_FILE_PROP_MOTION = 6,
    /* the castle rooftop: the meshes of its backdrop, then the cells of
     * its first area */
    STAGE_FILE_BACKDROP_MESHES = 5,
    STAGE_FILE_ROOFTOP_CELLS = 6,
    /* the other two with areas: the cells of the first area */
    STAGE_FILE_AREA_CELLS = 5,
};

/* In a stage with areas each area has a record of five offsets after the
 * header's first word. */
#define STAGE_AREA_RECORD_WORDS 5

/* Where the weapons' textures go in video memory. Up to four texture groups
 * are loaded: their pictures two across and two down from where the file
 * has them, and their colour tables into the block of the model the fog
 * falls on (stage.h), so that the weapons fade with the stage: the plain
 * tables are loaded a block's height under the ones the meshes use, as a
 * fogged model's are. Each group has two rows' worth of a column there; by
 * the arithmetic the file has its tables at (704, 254), under a model's six
 * rows (not read from a file). */
#define WEAPON_TEX_GROUPS 32
#define WEAPON_TEX_COLUMN_DX 128
#define WEAPON_TEX_ROW_DY 64
#define WEAPON_CLUT_DX (-384)
#define WEAPON_CLUT_COLUMN_DX 32
#define WEAPON_CLUT_ROW_DY 1

static s16 g_resource_tiny_model = -1; /* the one loaded, or -1 */
static s16 g_cutscene_ndata_id = -1;   /* the same for the motion */

/* The texture group of each weapon model (and four spare entries). */
/* clang-format off */
static s16 g_resource_weapon_tex_groups[WEAPON_MODEL_COUNT + 4] = {
    /* the weapons, a model a clan: a group each */
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    /* WEAPON_MODEL_SUB_WEAPONS: a group each */
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25,
    26,
    /* the models that come along with another: the group of the
     * model they come with */
    24, 0, 1, 0, 1, 2, 3, 2, 3, 11, 11, 13, 13, 22, 22, 14,
    14, 17, 17,
    /* WEAPON_MODEL_GUNS */
    27, 28,
    /* WEAPON_MODEL_OFF_HAND_GUNS */
    29, 30,
    /* WEAPON_MODEL_STORY */
    31,
    /* WEAPON_MODEL_PRACTICE: the second set of models, whose groups are
     * counted from 0 again */
    0, 1, 2, 3, 4, 5, 6,
    /* spare */
    0, 0, 0, 0,
};
/* clang-format on */

static s32 g_resource_stage_unused_list;
static s32 g_fight_obstacles;

/* ---- models ---- */

/* Opens model slot `slot`'s two blocks and reads model `model_id` into the
 * second, unless it is there already and model_FinishLoad has not asked for
 * its textures again. With a staging address the file is read there instead
 * and the caller copies what it keeps. */
Model *resource_LoadModel(s32 slot, s32 model_id, s32 staging_addr) {
    s32 data_block;
    ArenaBlock *block;
    Model *model;
    s32 data;

    block = arena_FindBlock(slot);
    if (block) {
        model = (Model *)ADDR(block->start);
    } else {
        model = (Model *)arena_OpenBlock(slot);
        arena_CloseBlock(slot, (s32)model + sizeof(Model));
        arena_UpdatePeak(0);
        arena_SetMoveCallback(slot, resource_OnModelMoved);
        model->model_id = -1;
        model->load_status = MODEL_STATUS_DONE;
    }
    data_block = slot + ARENA_MODEL_DATA;
    if (arena_FindBlock(data_block)) {
        cloth_FreeModel(model);
    }
    if (arena_FindBlock(data_block) && model->model_id == model_id &&
        model->load_status != MODEL_STATUS_WANTS_TEXTURES) {
        /* the same model is still there */
        model->load_status = MODEL_STATUS_DONE;
    } else {
        model->staging = staging_addr;
        data = (s32)arena_OpenBlock(data_block);
        model->data = data;
        if (staging_addr != 0) {
            ndata_ReadModel(model_id, staging_addr);
        } else {
            /* The block stops where the file's last section, the textures,
             * starts: they are only needed until they are in video memory. */
            ndata_ReadModel(model_id, data);
            data = SECTION_ADDR(data, ((s32 *)ADDR(data))[*(s32 *)ADDR(data)]);
            arena_UpdatePeak(data);
        }
        arena_CloseBlock(data_block, data);
        arena_SetMoveCallback(data_block, resource_OnModelMoved);
        model->load_status = MODEL_STATUS_READ;
        SET_PTR(model->cloth, NULL);
        model->load_flags.word = 0;
    }
    model->slot = slot;
    model->model_id = model_id;
    model->mesh_block = slot + RELOC_MODEL;
    model->light_setup_index = slot;
    model->shadow_block = slot + RELOC_MODEL_SHADOWS;
    model->outline_color.word = 0x8000; /* green */
    return model;
}

void resource_ResizeModelData(s32 slot, s32 new_size) {
    arena_Resize(slot + ARENA_MODEL_DATA, new_size);
    arena_UpdatePeak(0);
}

void resource_FreeModel(s32 slot) {
    s32 data_block = slot + ARENA_MODEL_DATA;

    snd_CloseModelVab(slot);
    arena_ClearMoveCallback(data_block);
    arena_ClearMoveCallback(slot);
    arena_Free(data_block);
    arena_Free(slot);
}

/* The arena moved one of a model's two blocks by `delta` bytes: everything
 * that points into it follows. */
static void resource_OnModelMoved(s32 block_id, s32 delta) {
    Model *model;
    s32 slot;

    if (block_id < ARENA_MODEL_DATA) {
        model_Rebase(block_id, delta);
        return;
    }
    slot = block_id - ARENA_MODEL_DATA;
    reloc_RebaseBlock(slot, delta);
    reloc_RebaseBlock(block_id, delta);
    model = (Model *)arena_GetBlockAddr(slot);
    if (!model) {
        return;
    }
    model->data = model->data + delta;
    cloth_RebaseModel(model, delta);
    /* MODEL_LOAD_HAS_VAB, tested as a bit */
    if ((model->load_flags.word >> 1) & 1) {
        /* the model's sound bank is in the block too */
        /* FAKE: `slot` reused for the bank id */
        slot = SND_VAB_MODELS_STEP * model->slot + SND_VAB_MODEL;
        snd_RebaseVab(delta, slot);
    }
}

/* ---- weapons ---- */

/* Reads one weapon file and keeps the models marked in `slots`
 * (g_resource_weapon_slots). The file is a header of section offsets, then
 * the models (two meshes each: the thing and its shadow), then, unless the
 * header is too short to have one, the sounds, then the textures. The
 * textures are in groups shared by several models; the groups needed, four
 * at most, are loaded side by side in video memory and each kept model's
 * texture coordinates are moved to where its group went. */
void resource_LoadWeaponFile(
    s32 weapon_set, s32 file, s16 *slots, s32 staging_addr) {
    s16 keep[120]; /* per mesh of the set: RELOC_KEEP or RELOC_DROP */
    /* per texture group: -1, or which of the four places it is loaded at */
    s16 group_place[WEAPON_TEX_GROUPS];
    s32 *block;
    s32 *header;
    s32 meshes;
    s32 sounds;
    s32 end_addr;
    s32 textures;
    /* the number of the header's last offset, then how many texture groups
     * have been given a place */
    s32 work;
    s32 i;
    s32 kept;
    s16 *cursor; /* over `slots`, then over `keep` */
    s16 *slot;
    u16 slot_value;
    s32 *dst;

    block = arena_OpenBlock(ARENA_WEAPONS);
    if (staging_addr != 0) {
        header = (s32 *)ADDR(staging_addr);
    } else {
        header = block;
    }
    ndata_ReadWeapons(weapon_set, file, (s32)header);

    meshes = (s32)header + ALIGN4(header[1]);
    work = header[0];
    textures = (s32)header + ALIGN4(header[work]);
    if (work >= 3) {
        sounds = (s32)header + ALIGN4(header[work - 1]);
    } else {
        sounds = 0;
    }

    for (i = 0; i < WEAPON_TEX_GROUPS; i++) {
        group_place[i] = -1;
    }

    /* the groups of the models asked for */
    cursor = slots;
    slot_value = *cursor++;
    i = 0;
    while ((s16)slot_value != WEAPON_SLOTS_END) {
        if ((s16)slot_value >= 0) {
            group_place[g_resource_weapon_tex_groups[i]] = 1;
        }
        i++;
        slot_value = *cursor++;
    }

    work = 0;
    for (i = 0; i < WEAPON_TEX_GROUPS; i++) {
        if (group_place[i] >= 0) {
            switch (work) {
            case 0:
                tex_LoadGroupAtSplit((u32 *)ADDR(textures), i, 0, 0,
                                     WEAPON_CLUT_DX, MODEL_FOG_PLAIN_CLUT_DY);
                break;
            case 1:
                tex_LoadGroupAtSplit(
                    (u32 *)ADDR(textures), i, 0, WEAPON_TEX_ROW_DY,
                    WEAPON_CLUT_DX,
                    MODEL_FOG_PLAIN_CLUT_DY + WEAPON_CLUT_ROW_DY);
                break;
            case 2:
                tex_LoadGroupAtSplit(
                    (u32 *)ADDR(textures), i, WEAPON_TEX_COLUMN_DX, 0,
                    WEAPON_CLUT_DX + WEAPON_CLUT_COLUMN_DX,
                    MODEL_FOG_PLAIN_CLUT_DY);
                break;
            case 3:
                tex_LoadGroupAtSplit(
                    (u32 *)ADDR(textures), i, WEAPON_TEX_COLUMN_DX,
                    WEAPON_TEX_ROW_DY, WEAPON_CLUT_DX + WEAPON_CLUT_COLUMN_DX,
                    MODEL_FOG_PLAIN_CLUT_DY + WEAPON_CLUT_ROW_DY);
                break;
            }
            group_place[i] = work;
            work++;
        }
    }

    DrawSync(0);

    /* a model is two meshes: both are kept or dropped */
    cursor = keep;
    if (weapon_set == 0) {
        slot = slots;
    } else {
        slot = slots + WEAPON_SET_1_FIRST_MODEL;
    }
    while (*slot != WEAPON_SLOTS_END) {
        if (*slot >= 0) {
            *cursor++ = RELOC_KEEP;
            *cursor++ = RELOC_KEEP;
        } else {
            *cursor++ = RELOC_DROP;
            *cursor++ = RELOC_DROP;
        }
        slot++;
    }
    *cursor = RELOC_LIST_END;

    /* The block's first word is kept for the offset of the sound banks. */
    if (staging_addr != 0) {
        dst = block + 1;
        end_addr = reloc_CopySelectedEntries(meshes, dst, keep);
        reloc_RelocateBlock(dst, RELOC_WEAPONS);
    } else {
        end_addr = reloc_KeepSelectedEntries((s32 *)ADDR(meshes), keep);
        reloc_RelocateBlock((s32 *)ADDR(meshes), RELOC_WEAPONS);
    }
    arena_UpdatePeak(end_addr);

    /* each kept model's texture coordinates, to where its group went. The
     * group is looked up by the model's place in the set, not by its
     * number as above: for the second set that is the wrong entry of the
     * table, which happens to hold the same group (the original's
     * behaviour) */
    cursor = keep;
    kept = 0;
    i = 0;
    while (*cursor != RELOC_LIST_END) {
        if (*cursor >= 0) {
            switch (group_place[g_resource_weapon_tex_groups[i]]) {
            case 0:
                tex_ShiftMesh(RELOC_WEAPONS, kept * 2, 0, 0, WEAPON_CLUT_DX,
                              MODEL_FOG_CLUT_DY);
                break;
            case 1:
                tex_ShiftMesh(
                    RELOC_WEAPONS, kept * 2, 0, WEAPON_TEX_ROW_DY,
                    WEAPON_CLUT_DX, MODEL_FOG_CLUT_DY + WEAPON_CLUT_ROW_DY);
                break;
            case 2:
                tex_ShiftMesh(
                    RELOC_WEAPONS, kept * 2, WEAPON_TEX_COLUMN_DX, 0,
                    WEAPON_CLUT_DX + WEAPON_CLUT_COLUMN_DX, MODEL_FOG_CLUT_DY);
                break;
            case 3:
                tex_ShiftMesh(
                    RELOC_WEAPONS, kept * 2, WEAPON_TEX_COLUMN_DX,
                    WEAPON_TEX_ROW_DY, WEAPON_CLUT_DX + WEAPON_CLUT_COLUMN_DX,
                    MODEL_FOG_CLUT_DY + WEAPON_CLUT_ROW_DY);
                break;
            }
            kept++;
        }
        cursor += 2;
        i++;
    }

    if (sounds != 0) {
        *block = end_addr - (s32)block;
        end_addr = snd_LoadWeaponVabs(sounds, end_addr);
        arena_UpdatePeak(end_addr);
    } else {
        *block = 0;
    }
    arena_CloseBlock(ARENA_WEAPONS, end_addr);
    arena_SetMoveCallback(ARENA_WEAPONS, resource_OnWeaponsMoved);
}

void resource_FreeWeapons(void) {
    arena_Free(ARENA_WEAPONS);
    snd_CloseWeaponVabs();
}

/* The arena moved the weapons' block by `delta` bytes: the meshes' table and
 * the sound banks in the block follow. `block_id` is not used. */
static void resource_OnWeaponsMoved(s32 block_id, s32 delta) {
    s32 *vab_ids;
    s32 count;

    reloc_RebaseBlock(RELOC_WEAPONS, delta);
    vab_ids = arena_GetBlockAddr(ARENA_WEAPONS);
    if (vab_ids == NULL) {
        return;
    }
    {
        s32 sounds_offset = *vab_ids;

        if (sounds_offset == 0) {
            return;
        }
        vab_ids = (s32 *)((u8 *)vab_ids + sounds_offset);
    }
    /* a count, then the ids of the banks that were opened */
    count = *vab_ids;
    count = count - 1;
    if (count == -1) {
        return;
    }
    vab_ids++;
    do {
        snd_RebaseVab(delta, *vab_ids++);
        count--;
    } while (count != -1);
}

/* ---- the stage ---- */

/* A stage file is a header of section offsets and then: the meshes, the
 * collision grid, a list nothing uses, the fight's obstacles, the
 * cells of the stage grid and the textures. The header's first word is the
 * number of the textures' offset; the cells' is the one before it. The
 * stages with something extra have more sections in between:
 *  - the three with areas (see resource_SelectStageArea);
 *  - the three with props that fall when cut (the bus stop, the bamboo
 *    thicket and the lumberyard): the props' meshes and their motion;
 *  - the castle rooftop: the meshes of its backdrop;
 *  - the coast: no section, but room for the surf's quads after the cells.
 * If the stage is already loaded it is set up again and not read, except
 * chanbara's, which is always read again. */
void resource_LoadStage(s32 stage, s32 staging_addr) {
    s32 *header;
    /* whether the stage that is loaded will do, then the stage's block */
    s32 work;
    s32 *meshes, *cells, *tim_section;
    s32 last_section; /* the number of the header's last offset */
    s32 *block_end;
    s32 *textures;
    s32 collision_grid, unused_list, obstacles;

    header = arena_GetBlockAddr(ARENA_STAGE);
    if (header != NULL) {
        if (g_resource_stage == stage) {
            work = 1;
            switch (stage) {
            case STAGE_TATARA_SHRINE:
                break;
            case STAGE_BUS_STOP:
            case STAGE_BAMBOO_THICKET:
            case STAGE_LUMBERYARD:
                /* the props stand again */
                last_section = header[0];
                stage_ReadCells(
                    (u16 *)ADDR(SECTION_ADDR(header, header[last_section - 1])),
                    RELOC_STAGE);
                break;
            case STAGE_CHANBARA:
                work = 0;
                break;
            }
            stage_RunInitHook();
            if (work != 0) {
                return;
            }
        }
    }

    g_resource_stage = stage;
    work = ARENA_STAGE;
    header = arena_OpenBlock(ARENA_STAGE);

    if (staging_addr != 0) {
        ndata_ReadStage(stage, staging_addr);
    } else {
        ndata_ReadStage(stage, (s32)header);
    }

    if (staging_addr != 0) {
        /* everything but the textures goes into the block */
        last_section = *(s32 *)ADDR(staging_addr);
        arena_Copy(staging_addr, (s32)header,
                   ((s32 *)ADDR(staging_addr))[last_section]);
    }

    {
        s32 meshes_offset = header[STAGE_FILE_MESHES];
        last_section = header[0];
        meshes = SECTION(header, meshes_offset);
        {
            s32 *last = (s32 *)ADDR((last_section << 2) + (s32)header);
            cells = SECTION(header, last[-1]);
            collision_grid = SECTION_ADDR(header, header[STAGE_FILE_COLLISION]);
            unused_list = SECTION_ADDR(header, header[STAGE_FILE_UNUSED_LIST]);
            obstacles = SECTION_ADDR(header, header[STAGE_FILE_OBSTACLES]);

            tim_section = SECTION(header, last[0]);

            if (staging_addr != 0) {
                textures = (s32 *)ADDR(
                    staging_addr +
                    ALIGN4(((s32 *)ADDR(staging_addr))[last_section]));
            } else {
                /* the word after the last offset: where the file ends */
                textures = tim_section;
                arena_UpdatePeak(SECTION_ADDR(header, last[1]));
            }
        }
    }

    /* The block ends where the cells start (they are read into tables of
     * their own), or for some stages where the textures do. */
    g_resource_stage_has_areas = 0;
    /* FAKE: block_end = cells */
    block_end = (s32 *)ADDR((s32)cells - (s32)header);
    block_end = (s32 *)ADDR((s32)block_end + (s32)header);
    switch (stage) {
    case STAGE_TATARA_SHRINE: {
        block_end = tim_section;
        meshes = SECTION(header, header[last_section - 2]);
        cells = SECTION(header, *(header + last_section - 1));
        g_resource_stage_has_areas = 1;
        break;
    }
    case STAGE_BUS_STOP:
    case STAGE_BAMBOO_THICKET:
    case STAGE_LUMBERYARD:
        block_end = tim_section;
        reloc_RelocateBlock(
            SECTION(header, header[STAGE_FILE_PROP_MESHES]), RELOC_STAGE_PROPS);
        block_end = (s32 *)ADDR(stage_SetUpProps(
            (s16 *)ADDR(SECTION_ADDR(header, header[STAGE_FILE_PROP_MOTION])),
            RELOC_STAGE_PROPS, (s32)block_end));
        break;
    case STAGE_KIMIGAURA_COAST:
        wave_SetBuffer((s32)block_end);
        block_end = (s32 *)ADDR((s32)block_end + wave_GetBufferSize());
        break;
    case STAGE_CASTLE_ROOFTOP:
        block_end = tim_section;
        meshes = SECTION(header, header[last_section - 2]);
        cells = SECTION(header, header[last_section - 1]);
        reloc_RelocateBlock(SECTION(header, header[STAGE_FILE_BACKDROP_MESHES]),
                            RELOC_STAGE_PROPS);
        g_resource_stage_has_areas = 1;
        break;
    case STAGE_CHANBARA:
        block_end = tim_section;
        g_resource_stage_has_areas = 1;
        cells = SECTION(header, header[STAGE_FILE_AREA_CELLS]);
        break;
    }

    reloc_RelocateBlock(meshes, RELOC_STAGE);
    tex_LoadStageGroup(textures, 0);
    stage_ReadCells((u16 *)cells, RELOC_STAGE);
    collision_SetGrid(collision_grid);
    g_resource_stage_unused_list = unused_list;
    g_fight_obstacles = obstacles;
    DrawSync(0);
    arena_CloseBlock(work, (s32)block_end);
    arena_SetMoveCallback(work, resource_OnStageMoved);
    stage_RunInitHook();
    /* only the story moves between a stage's areas */
    if (g_fight_mode != FIGHT_MODE_STORY) {
        if (stage != STAGE_CHANBARA) {
            resource_SelectStageArea(-1);
        }
    }
}

/* Settles on the stage's first area: sets it up from the header's own
 * sections and cuts the block off where the other areas' data starts. */
static void resource_DropStageAreas(void) {
    s32 *header;
    s32 *cells;
    s32 *meshes;

    if (!g_resource_stage_has_areas) {
        return;
    }
    header = arena_GetBlockAddr(ARENA_STAGE);
    meshes = SECTION(header, header[STAGE_FILE_MESHES]);
    switch (g_resource_stage) {
    case STAGE_CASTLE_ROOFTOP:
        cells = SECTION(header, header[STAGE_FILE_ROOFTOP_CELLS]);
        break;
    case STAGE_TATARA_SHRINE:
    case STAGE_CHANBARA:
        cells = SECTION(header, header[STAGE_FILE_AREA_CELLS]);
        break;
    }
    reloc_RelocateBlock(meshes, RELOC_STAGE);
    stage_ReadCells((u16 *)cells, RELOC_STAGE);
    arena_Resize(ARENA_STAGE, ((u8 *)cells - (u8 *)header));
    g_resource_stage_has_areas = 0;
}

/* Makes area `area` of the stage the one fought in, or with a negative
 * `area` settles on the first for good. In a stage with areas the header's
 * first word is followed by one record of five offsets per area: its
 * meshes, collision grid, unused list, obstacles and cells. */
void resource_SelectStageArea(s32 area) {
    s32 *header;
    s32 *meshes;
    s32 collision_grid;
    s32 unused_list;
    s32 obstacles;
    s32 *cells;
    s32 *record;

    if (area < 0) {
        resource_DropStageAreas();
        return;
    }
    if (!g_resource_stage_has_areas) {
        return;
    }
    record = arena_GetBlockAddr(ARENA_STAGE);
    header = record;
    record++;
    record = record + area * STAGE_AREA_RECORD_WORDS;
    meshes = (s32 *)((u8 *)header + *record);
    record++;
    collision_grid = (s32)header + *record;
    record++;
    unused_list = (s32)header + *record;
    record++;
    obstacles = (s32)header + *record;
    cells = (s32 *)((u8 *)header + record[1]);
    reloc_UnrelocateBlock(RELOC_STAGE);
    reloc_RelocateBlock(meshes, RELOC_STAGE);
    collision_SetGrid(collision_grid);
    g_resource_stage_unused_list = unused_list;
    g_fight_obstacles = obstacles;
    stage_ReadCells((u16 *)cells, RELOC_STAGE);
}

static void resource_FreeStage(void) {
    arena_Free(ARENA_STAGE);
    g_resource_stage = -1;
    g_resource_stage_has_areas = 0;
}

/* The arena moved the stage's block by `delta` bytes: the meshes' table,
 * the collision grid, the lists and what the stage has of its own follow.
 * `block_id` is not used. */
static void resource_OnStageMoved(s32 block_id, s32 delta) {
    s32 word_delta;

    reloc_RebaseBlock(RELOC_STAGE, delta);
    collision_RebaseGrid(delta);
    word_delta = (delta / 4) * 4;
    g_resource_stage_unused_list += word_delta;
    g_fight_obstacles += word_delta;
    switch (g_resource_stage) {
    case STAGE_BUS_STOP:
    case STAGE_BAMBOO_THICKET:
    case STAGE_LUMBERYARD:
        stage_RebaseProps(delta);
        break;
    case STAGE_CASTLE_ROOFTOP:
        /* The original's behaviour: the delta is not passed, so the
         * backdrop's meshes are moved by whatever the last call left in the
         * register. */
#if RB_FIX_ROOFTOP_REBASE
        if (g_rb_settings.bugfixes) {
            reloc_RebaseBlock(RELOC_STAGE_PROPS, delta);
        } else
#endif
        {
            reloc_RebaseBlock(RELOC_STAGE_PROPS);
        }
        break;
    case STAGE_KIMIGAURA_COAST:
        wave_RebaseBuffer(delta);
        break;
    }
}

/* A list in the stage file that nothing in the game uses. Nothing calls
 * it. */
static s32 resource_GetStageUnusedList(void) {
    return g_resource_stage_unused_list;
}

s32 resource_GetStageObstacles(void) { return g_fight_obstacles; }

s32 resource_GetStage(void) { return g_resource_stage; }

s32 resource_HasStageAreas(void) { return g_resource_stage_has_areas; }

/* ---- the tiny model ---- */

/* The tiny models come in two copies, the second for grayscale mode. */
s32 *resource_LoadTinyModel(s32 tiny_model) {
    s32 id = tiny_model;
    s32 block_id = ARENA_TINY_MODEL;
    s32 *header;

    if (tex_IsGrayscale()) {
        id = id + (NDATA_TINY_MODELS_GRAY - NDATA_TINY_MODELS);
    }
    {
        ArenaBlock *block = arena_FindBlock(ARENA_TINY_MODEL);

        if (block && g_resource_tiny_model == id) {
            header = (s32 *)ADDR(block->start);
        } else {
            s32 end_addr;
            s32 *meshes;

            header = arena_OpenBlock(block_id);
            ndata_ReadTinyModel(id, (s32)header);
            /* the header's second offset: where the textures start */
            end_addr = SECTION_ADDR(header, header[2]);
            meshes = SECTION(header, header[1]);
            arena_UpdatePeak(end_addr);
            reloc_RelocateBlock(meshes, RELOC_TINY_MODEL);
            arena_CloseBlock(block_id, end_addr);
            g_resource_tiny_model = id;
        }
    }
    arena_SetMoveCallback(block_id, resource_OnTinyModelMoved);
    return header;
}

void resource_ResizeTinyModel(s32 new_size) {
    arena_Resize(ARENA_TINY_MODEL, new_size);
    arena_UpdatePeak(0);
}

/* `block_id` is not used. */
static void resource_OnTinyModelMoved(s32 block_id, s32 delta) {
    model_RebaseTiny(delta);
    reloc_RebaseBlock(RELOC_TINY_MODEL, delta);
}

void resource_FreeTinyModel(void) { arena_Free(ARENA_TINY_MODEL); }

/* ---- the spare block: nothing calls these four ---- */

/* Nothing calls it. */
static void resource_OpenSpareBlock(void) { arena_OpenBlock(ARENA_SPARE); }

/* Its parameters are not used. */
static void resource_OnSpareBlockMoved(s32 block_id, s32 delta) {}

/* Nothing calls it. */
static void resource_CloseSpareBlock(s32 end_addr) {
    arena_UpdatePeak(end_addr);
    arena_CloseBlock(ARENA_SPARE, end_addr);
    arena_SetMoveCallback(ARENA_SPARE, resource_OnSpareBlockMoved);
}

/* Nothing calls it. */
static void resource_ResizeSpareBlock(s32 new_size) {
    arena_Resize(ARENA_SPARE, new_size);
}

/* ---- a cutscene's motion ---- */

/* Reads the archive's file `ndata_id` into the cutscene's block. */
s32 *cutscene_LoadBlock(s32 ndata_id) {
    s32 *header;
    s32 end_addr;

    header = arena_OpenBlock(ARENA_CUTSCENE);
    ndata_ReadChecked(ndata_id, (s32)header);
    /* the last of the file's offsets is where it ends */
    end_addr = SECTION_ADDR(header, header[header[0] + 1]);
    arena_UpdatePeak(end_addr);
    arena_CloseBlock(ARENA_CUTSCENE, end_addr);
    g_cutscene_ndata_id = ndata_id;
    arena_SetMoveCallback(ARENA_CUTSCENE, resource_OnCutsceneMoved);
    return header;
}

void cutscene_FreeBlock(void) { arena_Free(ARENA_CUTSCENE); }

/* `block_id` is not used. */
static void resource_OnCutsceneMoved(s32 block_id, s32 delta) {
    cutscene_Rebase(delta);
}

/* ---- everything ---- */

void resource_FreeAll(void) {
    model_Free(0);
    model_Free(1);
    resource_FreeStage();
    resource_FreeWeapons();
    resource_ForgetWeapons();
    resource_FreeTinyModel();
}
