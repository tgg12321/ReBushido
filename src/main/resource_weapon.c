/* The weapons of a fight. The fight says which weapon models it needs and
 * which weapon sounds each fighter makes; when that differs from what is
 * loaded, the weapons' file is read again (resource_LoadWeaponFile in
 * resource.c). Also here: queueing the loaded weapon models for drawing, and
 * the two fighters' weapon sound banks. */
#include "common.h"
#include "bb2.h"

/* For each weapon model: its place among the loaded ones, or
 * WEAPON_SLOT_NONE if it is not loaded; WEAPON_SLOTS_END after the last.
 * Between resource_ClearWeaponRequests and resource_LoadWeapons a model that
 * has been asked for has WEAPON_SLOT_WANTED. */
static s16 g_resource_weapon_slots[WEAPON_MODEL_COUNT + 1];
#define WEAPON_SLOT_WANTED 1

/* What the list at the head of the weapons' sound banks takes up: a count
 * and a bank for each fighter. */
#define WEAPON_VAB_LIST_SIZE (3 * sizeof(s32))

/* The file with no weapon sounds at all, for when neither fighter has any:
 * the last of each set, after the files of its 136 and 36 pairs of sound
 * kinds. */
#define WEAPON_SET_0_NO_SOUNDS_FILE 136
#define WEAPON_SET_1_NO_SOUNDS_FILE 36

/* What the last load loaded. */
static s32 g_resource_loaded_weapon_file = -1;

/* Each fighter's weapon sound kind, or -1. */
static s16 g_resource_weapon_sounds[2];
/* The set the requested models are of, or -1 while none is requested. */
static s32 g_resource_weapon_set;

void resource_ClearWeaponRequests(void) {
    s32 i;

    for (i = 0; i < WEAPON_MODEL_COUNT; i++) {
        g_resource_weapon_slots[i] = WEAPON_SLOT_NONE;
    }
    g_resource_weapon_slots[WEAPON_MODEL_COUNT] = WEAPON_SLOTS_END;
    g_resource_weapon_sounds[1] = -1;
    g_resource_weapon_sounds[0] = -1;
    g_resource_weapon_set = -1;
}

/* Asks for a weapon model and the models that go with it. Everything asked
 * for must be of one set. */
void resource_RequestWeapon(s32 weapon_model) {
    u8 linked;

    g_resource_weapon_slots[weapon_model] = WEAPON_SLOT_WANTED;
    if (g_resource_weapon_set == -1) {
        if (weapon_model >= WEAPON_SET_1_FIRST_MODEL) {
            g_resource_weapon_set = 1;
        } else {
            g_resource_weapon_set = 0;
        }
    } else {
        if (g_resource_weapon_set == 0 &&
            !(weapon_model < WEAPON_SET_1_FIRST_MODEL)) {
            sys_Trap();
        }
        if (g_resource_weapon_set == 1 &&
            weapon_model < WEAPON_SET_1_FIRST_MODEL) {
            sys_Trap();
        }
    }
    linked = g_resource_weapon_linked[weapon_model][0];
    if (linked != WEAPON_LINKED_NONE) {
        g_resource_weapon_slots[linked] = WEAPON_SLOT_WANTED;
        g_resource_weapon_slots[g_resource_weapon_linked[weapon_model][1]] =
            WEAPON_SLOT_WANTED;
    }
}

/* Says which weapon sound kind fighter `fighter` (0 or 1) makes. The kind
 * must be one the requested models' set has. */
void resource_SetWeaponSounds(s32 fighter, s32 sound_kind) {
    s32 valid;

    if (g_resource_weapon_set == 0) {
        valid = sound_kind < WEAPON_SET_0_SOUND_KINDS;
    } else {
        valid = sound_kind < WEAPON_SET_1_SOUND_KINDS;
    }
    if (!valid) {
        sys_Trap();
    }
    if (fighter >= 2U) {
        sys_Trap();
    }
    g_resource_weapon_sounds[fighter] = sound_kind;
}

/* The number of the pair (lo, hi), lo <= hi, when the pairs of `row_length`
 * things are counted in order: (0, 0), (0, 1)... (1, 1), (1, 2)... */
static s32 math_UnorderedPairIndex(s32 row_length, s32 lo, s32 hi) {
    s32 sum = 0;
    s32 i;

    for (i = 0; i < lo; i++) {
        sum += row_length;
        row_length--;
    }
    sum += hi - lo;
    return sum;
}

/* Loads what was requested, unless the same models and the same file are
 * loaded already. The requested models are numbered in order: that number is
 * a model's place in the loaded block. The file is the one for the two
 * fighters' pair of sound kinds. */
void resource_LoadWeapons(s32 staging_addr) {
    s16 *loaded;
    s16 *requested;
    s16 *slot;
    /* FAKE: `i` counts both loops and then holds the file number */
    s32 i;
    s32 unchanged;
    s32 next_place;
    s32 kinds;
    s32 lo;
    s32 hi;

    unchanged = 1;
    i = 0;
    loaded = g_resource_loaded_weapons;
    requested = g_resource_weapon_slots;
    do {
        s16 wanted = *requested;
        if ((wanted >= 0) != ((*loaded) >= 0)) {
            unchanged = 0;
        }
        *loaded = wanted;
        loaded++;
        i++;
        requested++;
    } while (i < WEAPON_MODEL_COUNT);
    next_place = 0;
    i = 0;
    slot = g_resource_weapon_slots;
    do {
        if ((*slot) >= 0) {
            *slot = next_place;
            next_place++;
        }
        i++;
        slot++;
    } while (i < WEAPON_MODEL_COUNT);
    kinds = WEAPON_SET_1_SOUND_KINDS;
    if (g_resource_weapon_set == 0) {
        kinds = WEAPON_SET_0_SOUND_KINDS;
    }
    hi = g_resource_weapon_sounds[0];
    if (hi == -1) {
        lo = g_resource_weapon_sounds[1];
        if (lo == hi) {
            i = WEAPON_SET_1_NO_SOUNDS_FILE;
            if (g_resource_weapon_set == 0) {
                i = WEAPON_SET_0_NO_SOUNDS_FILE;
            }
        } else {
            i = math_UnorderedPairIndex(kinds, lo, lo);
        }
    } else {
        lo = hi;
        if ((g_resource_weapon_sounds[1] != -1) &&
            (hi != g_resource_weapon_sounds[1])) {
            if (hi < g_resource_weapon_sounds[1]) {
                hi = g_resource_weapon_sounds[1];
            } else {
                lo = g_resource_weapon_sounds[1];
            }
        }
        i = math_UnorderedPairIndex(kinds, lo, hi);
    }
    if (g_resource_loaded_weapon_file != i) {
        g_resource_loaded_weapon_file = i;
        unchanged = 0;
    }
    if (!unchanged) {
        resource_FreeWeapons();
        resource_LoadWeaponFile(
            g_resource_weapon_set, i, g_resource_weapon_slots, staging_addr);
        stage_BuildModelFog();
    }
}

/* Empty, and nothing calls it. A guess from where it sits: it printed what
 * was loaded in development builds. */
static void resource_DebugWeapons(void) {}

/* ---- drawing the weapon models (scene graph) ----
 * A weapon model is two meshes of the weapons' block: the weapon and its
 * shadow. Their nodes are made in the primitive buffer each frame, so they
 * last as long as the frame's drawing does. */

/* Queues a loaded weapon model for drawing, and its shadow. `where` says
 * where it is:
 *  - WEAPON_DRAW_NOWHERE: nothing is drawn;
 *  - WEAPON_DRAW_FREE: at `pos`, turned by `rot` (a loose object; no shadow);
 *  - otherwise in a fighter's hand: twice the fighter's model slot, plus 1
 *    for the second hand, with WEAPON_DRAW_HELD set by the callers. The
 *    weapon goes where the model's hand bone is, and its matrix is written
 *    back to that bone for the fight code to read. */
void weapon_Queue(s32 weapon_model, s32 where, s32 *pos, s16 *rot) {
    SVECTOR offset;
    s32 ground_y;
    Model *model;
    ModelNode *node;
    ModelNode *hand;
    s32 second_hand;
    if (g_resource_weapon_slots[weapon_model] < 0) {
        sys_Trap();
    }
    node = (ModelNode *)g_disp_prim_cursor;
    ground_y = 0;
    node->node.kind = NODE_KIND_ROTATED;
    node->node.draw_flags = 0;
    node->node.mesh = g_resource_weapon_slots[weapon_model] * 2;
    node->node.mesh_block = RELOC_WEAPONS;
    node->node.rotation_order = ROTATION_ORDER_ZYX;
    SET_PTR(node->node.parent, NULL);
    node->node.light_setup_index = LIGHT_SCENERY;
    if (where != WEAPON_DRAW_NOWHERE) {
        if (where == WEAPON_DRAW_FREE) {
            node->node.rot.vx = rot[0];
            node->node.rot.vy = rot[1];
            node->node.rot.vz = rot[2];
            g_math_rot_matrix_funcs[ROTATION_ORDER_ZYX](
                &node->node.rot, &node->node.world);
            node->node.world.t[0] = pos[0];
            node->node.world.t[1] = pos[1];
            node->node.world.t[2] = pos[2];
        } else {
            /* FAKE: `where` loses its HELD bit here and gets it back below */
            where &= WEAPON_DRAW_HELD - 1;
            second_hand = where & 1;
            model = model_Get(where >> 1);
            hand = &model->bones[MODEL_HAND_BONE + second_hand];
            /* The hand bone's place in the body comes from the motion
             * frame; it has not been scaled to the character yet. */
            hand->node.local.t[0] =
                (hand->node.local.t[0] * model->scale) >> 12;
            hand->node.local.t[1] =
                (hand->node.local.t[1] * model->scale) >> 12;
            hand->node.local.t[2] =
                (hand->node.local.t[2] * model->scale) >> 12;
            MulMatrix0(&model->bones[0].node.world, &hand->node.local,
                       &node->node.world);
            offset.vx = hand->node.local.t[0];
            offset.vy = hand->node.local.t[1];
            offset.vz = hand->node.local.t[2];
            ApplyMatrix(&PTR(hand->node.parent)->world, &offset,
                        (VECTOR *)node->node.world.t);
            node->node.world.t[0] =
                node->node.world.t[0] + PTR(hand->node.parent)->world.t[0];
            node->node.world.t[1] =
                node->node.world.t[1] + PTR(hand->node.parent)->world.t[1];
            node->node.world.t[2] =
                node->node.world.t[2] + PTR(hand->node.parent)->world.t[2];
            where |= WEAPON_DRAW_HELD;
            hand->node.world = node->node.world;
            ground_y = model->ground_y;
        }
        {
            void **queue = g_render_queue_end;
            g_render_queue_end = queue + 1;
            *queue = node;
        }
        node++;
        if (where != WEAPON_DRAW_FREE) {
            s32 loaded = g_resource_weapon_slots[weapon_model];
            void **queue;
            node->node.kind = NODE_KIND_SHADOW;
            node->node.draw_flags = 0;
            NODE_EXTRA(node) = ground_y;
            queue = g_render_queue_end;
            SET_PTR(node->node.parent, &node[-1].node);
            node->node.matrix_state = NODE_MATRIX_DONE;
            node->node.rotation_order = ROTATION_ORDER_ZYX;
            node->node.light_setup_index = 0;
            node->node.mesh_block = RELOC_WEAPONS;
            node->node.mesh = loaded * 2 + 1;
            g_render_queue_end = queue + 1;
            *queue = node;
            node++;
        }
        g_disp_prim_cursor = (u32 *)node;
    }
}

/* Queues the thing worn at the waist that goes with a weapon model (a
 * reading: its scabbard), and its shadow. `slot_and_side` is twice the
 * fighter's model slot plus the side it is worn on; `which` picks between
 * the weapon's two linked models. */
void weapon_QueueWorn(s32 weapon_model, s32 slot_and_side, s32 which) {
    SVECTOR unused; /* nothing uses it; it only takes room in the frame */
    u8 linked;
    s16 *pose;
    ModelNode *node;
    Model *model;
    void **queue;

    linked = g_resource_weapon_linked[weapon_model][which];
    if (linked == WEAPON_LINKED_NONE) {
        return;
    }
    if (g_resource_weapon_slots[linked] < 0) {
        sys_Trap();
    }
    model = model_Get(slot_and_side >> 1);
    node = (ModelNode *)g_disp_prim_cursor;
    node->node.kind = NODE_KIND_ROTATED;
    node->node.draw_flags = 0;
    node->node.mesh = g_resource_weapon_slots[linked] * 2;
    node->node.mesh_block = RELOC_WEAPONS;
    node->node.rotation_order = ROTATION_ORDER_ZYX;
    node->node.light_setup_index = LIGHT_SCENERY;
    pose = g_weapon_worn_poses[slot_and_side & 1];
    node->node.local.t[0] = ((*pose) * model->scale) >> 12;
    pose++;
    node->node.local.t[1] = ((*pose) * model->scale) >> 12;
    pose++;
    node->node.local.t[2] = ((*pose) * model->scale) >> 12;
    pose++;
    node->node.rot.vx = *pose;
    pose++;
    node->node.rot.vy = *pose;
    pose++;
    node->node.rot.vz = *pose;
    SET_PTR(node->node.parent, &model->bones[MODEL_WAIST_BONE].node);
    node->node.matrix_state = NODE_MATRIX_REBUILD;
    node_UpdateWorldMatrix(&node->node);
    queue = g_render_queue_end;
    g_render_queue_end = queue + 1;
    *queue = node;
    node++;
    node->node.mesh = g_resource_weapon_slots[linked] * 2 + 1;
    node->node.kind = NODE_KIND_SHADOW;
    SET_PTR(node->node.parent, &node[-1].node);
    node->node.draw_flags = 0;
    node->node.rotation_order = ROTATION_ORDER_ZYX;
    node->node.matrix_state = NODE_MATRIX_DONE;
    node->node.light_setup_index = 0;
    node->node.mesh_block = RELOC_WEAPONS;
    NODE_EXTRA(node) = model->ground_y;
    queue = g_render_queue_end;
    g_render_queue_end = queue + 1;
    *queue = node;
    g_disp_prim_cursor = (u32 *)(node + 1);
}

/* Copies the two fighters' weapon banks to `output_addr` and opens them. The
 * pack table holds the one or two banks they chose, the lower-numbered first
 * (g_resource_weapon_sounds; -1 = that fighter has none). What is written is
 * a list of the bank ids opened (a count, then the ids), then each fighter's
 * copy of its bank. Returns the end of it. */
s32 snd_LoadWeaponVabs(s32 pack_table_addr, s32 output_addr) {
    s32 count;
    s32 tail_off;
    s32 first_section_end_offset;
    s32 tail_addr;
    s32 first_section_offset;
    s32 first_section_addr;
    s32 first_section_size;
    s32 second_section_addr;
    s32 second_section_size;
    s32 slot0_selection;
    s32 slot1_selection;
    s32 pack_cursor_addr;
    s32 header_cursor_addr;
    s32 *count_slot;
    u32 header_entry_count;
    s32 copy_end_addr;
    s32 *table = (s32 *)ADDR(pack_table_addr);

    count = table[0];
    header_cursor_addr = output_addr;
    tail_off = table[count + 1];
    first_section_end_offset = table[2];
    /* FAKE: tail_addr = pack_table_addr + tail_off, in two steps */
    tail_addr = pack_table_addr;
    tail_addr += tail_off;
    first_section_offset = table[1];
    first_section_addr = pack_table_addr + first_section_offset;
    first_section_size = first_section_end_offset - first_section_offset;

    if (count >= 2) {
        second_section_addr = pack_table_addr + first_section_end_offset;
        second_section_size = table[3] - first_section_end_offset;
    } else {
        second_section_addr = 0;
        second_section_size = 0;
    }

    slot0_selection = g_resource_weapon_sounds[0];
    slot1_selection = g_resource_weapon_sounds[1];
    pack_cursor_addr = header_cursor_addr + WEAPON_VAB_LIST_SIZE;

    if (slot0_selection == -1) {
        if (slot1_selection == slot0_selection) {
            /* (both are -1) */
            slot0_selection = slot1_selection;
        } else {
            slot1_selection = 0;
        }
    } else if (slot1_selection == -1) {
        slot0_selection = 0;
    } else if (slot0_selection == slot1_selection) {
        slot0_selection = 0;
        slot1_selection = 0;
    } else if (slot0_selection < slot1_selection) {
        slot0_selection = 0;
        slot1_selection = 1;
    } else if (slot1_selection < slot0_selection) {
        slot0_selection = 1;
        slot1_selection = 0;
    } else {
        sys_Trap();
    }

    /* The count of banks is written first: one for each fighter that has
     * one. FAKE: `~x >> 31` is `x >= 0`. */
    header_entry_count = ~slot0_selection;
    count_slot = (s32 *)ADDR(header_cursor_addr);
    header_cursor_addr += 4;
    header_entry_count >>= 31;
    if (slot1_selection >= 0) {
        header_entry_count += 1;
    }
    *count_slot = header_entry_count;

    if (slot0_selection >= 0) {
        *(s32 *)ADDR(header_cursor_addr) = SND_VAB_WEAPON_1;
        header_cursor_addr += 4;
        if (slot0_selection == 0) {
            mem_MoveWords(
                first_section_addr, pack_cursor_addr, first_section_size);
            copy_end_addr = pack_cursor_addr + first_section_size;
        } else {
            mem_MoveWords(
                second_section_addr, pack_cursor_addr, second_section_size);
            copy_end_addr = pack_cursor_addr + second_section_size;
        }
        arena_UpdatePeak(copy_end_addr);
        pack_cursor_addr += snd_LoadVab(
            (SoundVab *)ADDR(pack_cursor_addr), SND_VAB_WEAPON_1, tail_addr);
    }

    if (slot1_selection >= 0) {
        *(s32 *)ADDR(header_cursor_addr) = SND_VAB_WEAPON_2;
        if (slot1_selection == 0) {
            mem_MoveWords(
                first_section_addr, pack_cursor_addr, first_section_size);
            copy_end_addr = pack_cursor_addr + first_section_size;
        } else {
            mem_MoveWords(
                second_section_addr, pack_cursor_addr, second_section_size);
            copy_end_addr = pack_cursor_addr + second_section_size;
        }
        arena_UpdatePeak(copy_end_addr);
        pack_cursor_addr += snd_LoadVab(
            (SoundVab *)ADDR(pack_cursor_addr), SND_VAB_WEAPON_2, tail_addr);
    }
    return pack_cursor_addr;
}

void resource_ForgetWeapons(void) {
    s32 i;

    for (i = 0; i < WEAPON_MODEL_COUNT; i++) {
        g_resource_loaded_weapons[i] = WEAPON_SLOT_NONE;
    }
    g_resource_loaded_weapon_file = -1;
}
