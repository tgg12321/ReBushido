/* Sound effects: the sound banks and the queue of sounds to start (see
 * snd.h).
 *
 * A bank's samples are sent to the sound chip when it is loaded and stay
 * there; its header and key-on table stay in main RAM, wherever the caller
 * keeps them. The samples of the open banks are kept packed from the bottom
 * of the chip's memory: the common bank first, the rest after it in the order
 * they were loaded, closing up the holes that closed banks leave. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include "bb2.h"

/* Defined further down. */
static s16 g_snd_stage_reverbs[38][2];
static s32 snd_OpenVab(SoundVab *vab, s32 id);

/* Where each open bank's samples are in the sound chip's memory. */
static s32 g_snd_vab_spu_addrs[SND_VAB_COUNT];
static SoundRequest g_snd_queue[SND_QUEUE_SIZE];
static SoundVab *g_snd_vabs[SND_VAB_COUNT];

/* The first free byte of the sound chip's memory (the chip keeps the start
 * for itself). */
#define SPU_RAM_START 0x1010
/* The last byte of the sound chip's memory: above any bank's address. */
#define SPU_RAM_END 0x7FFFF

/* The banks that come out of a stage's file: the stage's own and the two
 * fighters' class banks, or one class bank when both are of one class. */
#define STAGE_VABS 3
#define STAGE_VABS_ONE_CLASS 2

/* A stage without reverb, in g_snd_stage_reverbs. */
#define NO_REVERB -1

/* A word of a bank's key-on table: what to play for one sound. */
#define KEY_ON_PROGRAM(k) ((k) & 0x7F)
#define KEY_ON_TONE(k) (((k) >> 7) & 0xF)
#define KEY_ON_NOTE(k) (((k) >> 11) & 0x7F)
#define KEY_ON_FINE(k) (((k) >> 18) & 0x7F)

#define CLOSE_VAB(id)                                                          \
    SsVabClose(id);                                                            \
    g_snd_vabs[id] = NULL;                                                     \
    g_snd_vab_spu_addrs[id] = 0

/* Cleared by snd_Init, never read. */
static s16 g_snd_unused_init_halfword;
/* Where the next bank's samples go in the sound chip's memory. */
static s32 g_snd_spu_next_addr;
/* How much of it the banks after the common one take. */
static s32 g_snd_spu_used_bytes;
/* Where the banks after the common one start. */
static s32 g_snd_spu_base_addr;
/* Eight bytes that no code names, this file's or hud.c's: one variable or
 * more. */
static s32 g_snd_unused_words[2];

/* Starts Sony's sound library with no bank open, no reverb and an empty
 * queue. */
void snd_Init(void) {
    s32 i;

    for (i = 0; i < SND_VAB_COUNT; i++) {
        g_snd_vabs[i] = NULL;
        g_snd_vab_spu_addrs[i] = 0;
    }
    SsInit();
    SsUtAllKeyOff(0);
    SsUtReverbOff();
    SsUtSetReverbType(SS_REV_TYPE_OFF);
    SsUtSetReverbDepth(0, 0);
    SsSetReservedVoice(0);
    SsSetTickMode(SS_TICK60);
    for (i = 0; i < SND_QUEUE_SIZE; i++) {
        g_snd_queue[i].sound = NULL;
        g_snd_queue[i].right = g_snd_queue[i].left = SND_VOLUME_MAX;
    }
    SsStart();
    g_snd_spu_used_bytes = 0;
    g_snd_unused_init_halfword = 0;
}

/* Stops the sound library (before the movie player is run) and forgets
 * every bank. */
void snd_Quit(void) {
    s32 i;

    SsUtAllKeyOff(0);
    SsUtReverbOff();
    SsUtSetReverbType(SS_REV_TYPE_OFF);
    SsUtSetReverbDepth(0, 0);
    SsEnd();
    SsQuit();
    for (i = 0; i < SND_VAB_COUNT; i++) {
        g_snd_vabs[i] = NULL;
        g_snd_vab_spu_addrs[i] = 0;
    }
    g_snd_spu_used_bytes = 0;
}

void snd_StopAllVoices(void) { SsUtAllKeyOff(0); }

void snd_Reset(void) {
    s32 i;

    SsUtAllKeyOff(0);
    for (i = 0; i < SND_QUEUE_SIZE; i++) {
        g_snd_queue[i].sound = NULL;
        g_snd_queue[i].right = g_snd_queue[i].left = SND_VOLUME_MAX;
        SsUtSetVVol(i, 0, 0);
    }
}

void snd_CloseModelVab(s32 model_slot) {
    s32 id;

    SsUtAllKeyOff(0);
    id = model_slot * SND_VAB_MODELS_STEP + SND_VAB_MODEL;
    CLOSE_VAB(id);
}

void snd_CloseWeaponVabs(void) {
    SsUtAllKeyOff(0);
    CLOSE_VAB(SND_VAB_WEAPON_1);
    CLOSE_VAB(SND_VAB_WEAPON_2);
}

void snd_CloseMenuVab(void) { CLOSE_VAB(SND_VAB_MENU); }

/* Closes every bank but the common one, with the reverb off and the queue
 * empty. */
void snd_CloseAllVabs(void) {
    s32 id;

    SsUtAllKeyOff(0);
    SsUtReverbOff();
    SsUtSetReverbType(SS_REV_TYPE_OFF);
    SsUtSetReverbDepth(0, 0);
    for (id = SND_VAB_COMMON + 1; id < SND_VAB_COUNT; id++) {
        SsVabClose(id);
        g_snd_vabs[id] = NULL;
        g_snd_vab_spu_addrs[id] = 0;
    }
    g_snd_spu_used_bytes = 0;
    snd_Reset();
}

/* Reads the common bank's file to `load_addr` and opens it as the first
 * bank in the sound chip's memory; the others go after it. Returns the size
 * of the part to keep. */
s32 snd_LoadCommonVab(s32 load_addr) {
    s32 file_index;
    u32 file_size;
    s32 kept_size;

    SsUtAllKeyOff(0);
    printf("common_vab start:%08x\n", load_addr);
    cdrom_WaitIdle();
    file_index = cdrom_GetFileIndex(CDROM_GROUP_DATA, CDROM_DATA_SYSTEM_SE);
    cdrom_StartRead(file_index, load_addr);
    file_size = cdrom_GetFileSize(file_index);
    cdrom_WaitIdle();
    g_snd_spu_used_bytes = 0;
    g_snd_spu_base_addr = SPU_RAM_START;
    g_snd_spu_next_addr = SPU_RAM_START;
    kept_size = snd_LoadVab(
        (SoundVab *)load_addr, SND_VAB_COMMON, load_addr + file_size);
    g_snd_spu_base_addr = g_snd_spu_next_addr;
    return kept_size;
}

void snd_CloseChanbaraVabs(void) {
    SsUtAllKeyOff(0);
    CLOSE_VAB(SND_VAB_CHANBARA);
    CLOSE_VAB(SND_VAB_MODEL_2);
}

/* Chanbara's two banks, OTHER_C.SE and then NINJA_C.SE. The second is read
 * to where the first one's kept part ends, so that what is kept is one block;
 * returns its size. */
s32 snd_LoadChanbaraVabs(s32 load_addr) {
    s32 first_file_index;
    s32 size;
    s32 first_kept_size;
    s32 second_file_index;

    snd_CloseChanbaraVabs();
    SsUtAllKeyOff(0);
    first_file_index =
        cdrom_GetFileIndex(CDROM_GROUP_DATA, CDROM_DATA_OTHER_C_SE);
    cdrom_WaitIdle();
    cdrom_StartRead(first_file_index, load_addr);
    size = cdrom_GetFileSize(first_file_index);
    cdrom_WaitIdle();
    first_kept_size =
        snd_LoadVab((SoundVab *)load_addr, SND_VAB_CHANBARA, load_addr + size);
    second_file_index =
        cdrom_GetFileIndex(CDROM_GROUP_DATA, CDROM_DATA_NINJA_C_SE);
    cdrom_WaitIdle();
    cdrom_StartRead(second_file_index, load_addr + first_kept_size);
    size = cdrom_GetFileSize(second_file_index) + first_kept_size;
    cdrom_WaitIdle();
    return snd_LoadVab((SoundVab *)(load_addr + first_kept_size),
                       SND_VAB_MODEL_2, load_addr + size) +
           first_kept_size;
}

void snd_RebaseChanbaraVabs(s32 rebase_delta) {
    snd_RebaseVab(rebase_delta, SND_VAB_CHANBARA);
    snd_RebaseVab(rebase_delta, SND_VAB_MODEL_2);
}

void snd_CloseJudgeVab(void) {
    SsUtAllKeyOff(0);
    CLOSE_VAB(SND_VAB_JUDGE);
}

/* Reads the judge's bank to `load_addr` and opens it. Returns the size of
 * the part to keep. */
s32 snd_LoadJudgeVab(s32 load_addr) {
    s32 file_index;
    s32 size;

    snd_CloseJudgeVab();
    file_index = cdrom_GetFileIndex(CDROM_GROUP_DATA, CDROM_DATA_JADGE_SE);
    cdrom_WaitIdle();
    cdrom_StartRead(file_index, load_addr);
    size = cdrom_GetFileSize(file_index);
    cdrom_WaitIdle();
    return snd_LoadVab((SoundVab *)load_addr, SND_VAB_JUDGE, load_addr + size);
}

void snd_RebaseJudgeVab(s32 rebase_delta) {
    snd_RebaseVab(rebase_delta, SND_VAB_JUDGE);
}

/* A stage's sound file starts with a table of where its banks are: the
 * stage's own, then one per fighter class. Its thirteenth word is the offset
 * of the stage's effect billboards. */
typedef struct {
    s32 off;
    s32 size;
} StageVabEntry;

#define STAGE_FILE_FLAMES 12

typedef struct {
    StageVabEntry ent[STAGE_VABS];
    s32 kept_size[STAGE_VABS];
} StageVabLoad;

/* Loads the stage's bank and the class banks of the two fighters (one, if
 * they are of the same class), then moves the parts to keep together at
 * `load_addr`. Returns their total size. */
s32 snd_LoadStageVabs(
    s32 load_addr, s32 stage, s32 first_class, s32 second_class) {
    StageVabLoad load;
    u8 *file; /* then where the next kept part goes */
    s32 base;
    s32 file_index;
    s32 size;
    u8 vab_count;
    s32 i;
    u32 byte;

    file = (u8 *)load_addr;
    SsUtAllKeyOff(0);
    for (i = 0; i < STAGE_VABS; i++) {
        CLOSE_VAB(g_snd_stage_vab_ids[i]);
    }
    file_index =
        cdrom_GetFileIndex(CDROM_GROUP_DATA, stage + CDROM_DATA_STAGE_BIN);
    cdrom_WaitIdle();
    cdrom_StartRead(file_index, (s32)file);
    size = cdrom_GetFileSize(file_index);
    cdrom_WaitIdle();
    ((s32 *)file)[STAGE_FILE_FLAMES] += (s32)file;
    eff_SetFlames((Flame *)((s32 *)file)[STAGE_FILE_FLAMES]);
    vab_count = STAGE_VABS;
    base = (s32)file;
    if (first_class == second_class) {
        vab_count = STAGE_VABS_ONE_CLASS;
    }
    load.ent[0].off = ((StageVabEntry *)file)[0].off;
    load.ent[0].size = ((StageVabEntry *)file)[0].size;
    load.ent[1].off = ((StageVabEntry *)file)[first_class + 1].off;
    load.ent[1].size = ((StageVabEntry *)file)[first_class + 1].size;
    if (vab_count == STAGE_VABS) {
        load.ent[2].off = ((StageVabEntry *)file)[second_class + 1].off;
        load.ent[2].size = ((StageVabEntry *)file)[second_class + 1].size;
    }
    for (i = 0; i < vab_count; i++) {
        load.ent[i].off += (s32)file;
        load.kept_size[i] =
            snd_LoadVab((SoundVab *)load.ent[i].off, g_snd_stage_vab_ids[i],
                        (s32)file + size);
    }
    for (i = 0; i < vab_count; i++) {
        for (byte = 0; byte < load.kept_size[i]; byte++) {
            file[byte] = ((u8 *)load.ent[i].off)[byte];
        }
        snd_RebaseVab((s32)file - load.ent[i].off, g_snd_stage_vab_ids[i]);
        load.ent[i].off = (s32)file;
        file += load.kept_size[i];
    }
    if (vab_count == STAGE_VABS_ONE_CLASS) {
        g_snd_vabs[SND_VAB_CLASS_2] = g_snd_vabs[SND_VAB_CLASS_1];
    }
    return (s32)file - base;
}

/* The stage's banks have moved by `rebase_delta` bytes: two of them if the
 * fighters share a class bank, which the second fighter's is then pointed
 * at again. */
void snd_RebaseStageVabs(s32 rebase_delta) {
    u8 vab_count;
    u8 i;

    SsUtAllKeyOff(0);
    vab_count = (g_snd_vabs[SND_VAB_CLASS_2] == g_snd_vabs[SND_VAB_CLASS_1])
                    ? STAGE_VABS_ONE_CLASS
                    : STAGE_VABS;
    for (i = 0; i < vab_count; i++) {
        snd_RebaseVab(rebase_delta, g_snd_stage_vab_ids[i]);
    }
    if (vab_count == STAGE_VABS_ONE_CLASS) {
        g_snd_vabs[SND_VAB_CLASS_2] = g_snd_vabs[SND_VAB_CLASS_1];
    }
}

void snd_CloseStageVabs(void) {
    s32 i;

    for (i = 0; i < STAGE_VABS; i++) {
        CLOSE_VAB(g_snd_stage_vab_ids[i]);
    }
}

/* Turns on the stage's reverb, if it has one. Returns what Sony's library
 * answers for the type, or NO_REVERB. The depth is set from the stage number
 * and not from the table's second column, which nothing reads: the
 * original's behaviour, most likely a slip.
 * FAKE: `reverb` is g_snd_stage_reverbs[stage] and `depth` stage * 2 + 1. */
s32 snd_SetStageReverb(s32 stage) {
    s16 result;
    s16 *reverb;
    s16 depth;
    s16 *base;
    s32 stage_times_2;

    SsUtAllKeyOff(0);
    base = &g_snd_stage_reverbs[0][0];
    reverb = base + stage * 2;
    stage_times_2 = stage << 1;
    if (*reverb >= 0) {
        SsUtReverbOff();
        SsUtSetReverbType(SS_REV_TYPE_OFF);
        SsUtSetReverbDepth(0, 0);
        result = SsUtSetReverbType(*reverb);
        SpuClearReverbWorkArea(*reverb);
        depth = stage_times_2 + 1;
        SsUtSetReverbDepth(depth, depth);
        SsUtReverbOn();
    } else {
        result = NO_REVERB;
    }
    return result;
}

void snd_StopReverb(void) {
    SsUtAllKeyOff(0);
    SsUtReverbOff();
    SsUtSetReverbType(SS_REV_TYPE_OFF);
    SsUtSetReverbDepth(0, 0);
}

/* Moves bank `id`'s samples down the sound chip's memory, from `src` to
 * `dest`, by way of `scratch` in main RAM. Returns the address after them. */
static s32 snd_MoveVabSamples(s32 scratch, s32 id, s32 dest, s32 src) {
    SsUtAllKeyOff(0);
    SsVabClose(id);
    SpuSetTransferStartAddr(src);
    SpuRead(scratch, g_snd_vabs[id]->vab_body_size);
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    SpuSetTransferStartAddr(dest);
    SpuWrite(scratch, g_snd_vabs[id]->vab_body_size);
    SpuIsTransferCompleted(SPU_TRANSFER_WAIT);
    SsVabFakeHead((u8 *)g_snd_vabs[id]->vab_header, id, dest);
    SsVabFakeBody(id);
    g_snd_vab_spu_addrs[id] = dest;
    return dest + g_snd_vabs[id]->vab_body_size;
}

/* Closes up the holes in the sound chip's memory: puts the open banks after
 * the common one in order of address, and from the first that does not start
 * where the one before it ends, moves each down. `loading_id` is not used.
 * The loop that only sets `addr` is the game's own: it does count through
 * the banks and work out the same address each time. */
static s32 snd_CompactSpuMemory(s16 loading_id, s32 scratch) {
    s16 sorted_ids[SND_VAB_COUNT];
    s16 count;
    u16 taken;
    u32 lowest_addr;
    s16 lowest_id;
    s16 id;
    s16 j;
    s16 k;
    s32 addr;

    count = 0;
    taken = 0;
    for (;;) {
        lowest_addr = SPU_RAM_END;
        lowest_id = -1;
        for (id = SND_VAB_COMMON + 1; id < SND_VAB_COUNT; id++) {
            if (!((taken >> id) & 1) && g_snd_vab_spu_addrs[id] != 0 &&
                g_snd_vab_spu_addrs[id] < lowest_addr) {
                lowest_addr = g_snd_vab_spu_addrs[id];
                lowest_id = id;
            }
        }
        if (lowest_id == -1) {
            break;
        }
        sorted_ids[count++] = lowest_id;
        taken += 1 << lowest_id;
    }
    for (j = 0; j < count; j++) {
        addr = g_snd_vab_spu_addrs[SND_VAB_COMMON] +
               g_snd_vabs[SND_VAB_COMMON]->vab_body_size;
    }
    for (j = 0; j < count; j++) {
        if (g_snd_vab_spu_addrs[sorted_ids[j]] == addr) {
            addr += g_snd_vabs[sorted_ids[j]]->vab_body_size;
        } else {
            for (k = j; k < count; k++) {
                addr = snd_MoveVabSamples(scratch, sorted_ids[k], addr,
                                          g_snd_vab_spu_addrs[sorted_ids[k]]);
            }
            return 0;
        }
    }
    return 0;
}

/* Opens the bank at `vab` as bank `id`: closes the one that has the id,
 * closes up the sound chip's memory, turns the file's offsets into
 * addresses and sends the samples. Returns the size of the part that must
 * stay in memory, or 0 if it did not open.
 * Chanbara mode has no weapon bank for the second fighter: its enemies' bank
 * is loaded by snd_LoadChanbaraVabs.
 * Defined in the old style, as are the two after it: `id` is a halfword that
 * the callers pass as a word. */
s32 snd_LoadVab(vab, id, scratch)
SoundVab *vab;
s16 id;
s32 scratch;
{
    s16 i;
    s16 opened_id;

    if (menu_GetSettings()->menu_mode == FIGHT_MODE_CHANBARA &&
        id == SND_VAB_WEAPON_2) {
        return 0;
    }
    SsUtAllKeyOff(0);
    if (g_snd_vabs[id]) {
        CLOSE_VAB(id);
    }
    if (id != SND_VAB_COMMON) {
        g_snd_spu_next_addr = g_snd_vab_spu_addrs[SND_VAB_COMMON];
        for (i = 0; i < SND_VAB_COUNT; i++) {
            if (g_snd_vabs[i]) {
                g_snd_spu_next_addr += g_snd_vabs[i]->vab_body_size;
            }
        }
    }
    g_snd_spu_used_bytes = g_snd_spu_next_addr - g_snd_spu_base_addr;
    if (id != SND_VAB_COMMON) {
        snd_CompactSpuMemory(id, scratch);
    }
    vab->key_on_table += (s32)vab;
    vab->vab_header += (s32)vab;
    vab->vab_body += (s32)vab;
    opened_id = snd_OpenVab(vab, id);
    SsVabTransCompleted(SS_WAIT_COMPLETED);
    if (opened_id != -1) {
        g_snd_vabs[opened_id] = vab;
        g_snd_spu_used_bytes += vab->vab_body_size;
        g_snd_spu_next_addr = g_snd_spu_base_addr + g_snd_spu_used_bytes;
        g_snd_vab_spu_addrs[id] = SsUtGetVBaddrInSB(id);
        return vab->vab_body - (s32)vab;
    }
    printf("vab id:%d mistake\n", id);
    return 0;
}

/* The bank's samples are where they were in the sound chip's memory; only
 * the header is opened again at its new address. */
s32 snd_RebaseVab(rebase_delta, id)
s32 rebase_delta;
s16 id;
{
    s16 opened_id;

    SsUtAllKeyOff(0);
    if (g_snd_vabs[id]) {
        g_snd_vabs[id] = (SoundVab *)((u8 *)g_snd_vabs[id] + rebase_delta);
        g_snd_vabs[id]->key_on_table += rebase_delta;
        g_snd_vabs[id]->vab_header += rebase_delta;
        SsVabClose(id);
        opened_id = SsVabFakeHead(
            (u8 *)g_snd_vabs[id]->vab_header, id, g_snd_vab_spu_addrs[id]);
        if (opened_id != id) {
            return opened_id;
        }
        opened_id = SsVabFakeBody(opened_id);
    } else {
        opened_id = -1;
    }
    return opened_id;
}

static s32 snd_OpenVab(vab, id)
SoundVab *vab;
s16 id;
{
    SsVabClose(id);
    SsVabOpenHeadSticky((u8 *)vab->vab_header, id, g_snd_spu_next_addr);
    ((VabHdr *)vab->vab_header)->id = id;
    return SsVabTransBody((u8 *)vab->vab_body, id);
}

void snd_SetDefaultOutput(void) {
    SsSetMVol(SND_VOLUME_MAX, SND_VOLUME_MAX);
    SsUtAllKeyOff(0);
    SsSetStereo();
    SsSetAutoKeyOffMode(0);
}

/* Queues a sound. One asked for when the queue is full is dropped. It
 * returns nothing, though it is declared to (snd.h says why). */
s32 snd_Play(s32 sound, s32 right, s32 left) {
    s16 i = 0;
    SoundDef *def = &g_snd_sounds[sound];

    for (; i < SND_QUEUE_SIZE; i++) {
        if (!g_snd_queue[i].sound) {
            g_snd_queue[i].sound = def;
            g_snd_queue[i].right = right;
            g_snd_queue[i].left = left;
            return;
        }
    }
}

/* Starts each queued sound whose bank is loaded on the next voice that is
 * not playing, and empties the queue. A sound that finds no free voice is
 * dropped. */
void snd_FlushQueue(void) {
    u8 key_status[SND_VOICE_COUNT];
    s16 i;
    s16 voice;
    s16 next_voice;
    u16 vab;
    SoundDef *def;
    u32 *key_on;

    SpuGetAllKeysStatus(key_status);
    next_voice = 0;
    for (i = 0; i < SND_QUEUE_SIZE; i++) {
        def = g_snd_queue[i].sound;
        /* a bank that is open: its address in RAM has the top bit set, and
         * the original tests the sign */
        if (def && (s32)g_snd_vabs[def->vab] < 0) {
            voice = next_voice;
            for (; voice < SND_VOICE_COUNT; voice++) {
                if (SpuGetKeyStatus(1 << voice) != SPU_ON) {
                    vab = def->vab;
                    if (vab == SND_VAB_CLASS_2 &&
                        g_snd_vabs[SND_VAB_CLASS_2] ==
                            g_snd_vabs[SND_VAB_CLASS_1]) {
                        vab = SND_VAB_CLASS_1;
                    }
                    key_on = &((u32 *)g_snd_vabs[vab]->key_on_table)[def->key];
                    SsUtKeyOnV(voice, vab, KEY_ON_PROGRAM(*key_on),
                               KEY_ON_TONE(*key_on), KEY_ON_NOTE(*key_on),
                               KEY_ON_FINE(*key_on), g_snd_queue[i].left,
                               g_snd_queue[i].right);
                    next_voice = voice + 1;
                    break;
                }
            }
        }
        g_snd_queue[i].sound = NULL;
    }
}

/* A stage's reverb: Sony's type, or NO_REVERB, and what was probably meant
 * to be its depth (see snd_SetStageReverb), which nothing reads. */

/* clang-format off */
static s16 g_snd_stage_reverbs[38][2] = {
    /* type, depth */
    {SS_REV_TYPE_STUDIO_A, 2},   /* STAGE_MAIN_TATARA_SHRINE */
    {SS_REV_TYPE_ROOM, 2},       /* STAGE_MEIKYOKAN_DOJO */
    {SS_REV_TYPE_ROOM, 2},       /* STAGE_SHAINTO_HQ */
    {NO_REVERB, 0},              /* STAGE_TATARA_SHRINE */
    {NO_REVERB, 0},              /* STAGE_BUS_STOP */
    {NO_REVERB, 0},              /* STAGE_LOST_BRIDGE */
    {NO_REVERB, 0},              /* STAGE_TATARA_SHRINE_PATH */
    {NO_REVERB, 0},              /* STAGE_BAMBOO_THICKET */
    {NO_REVERB, 0},              /* STAGE_OUTSIDE_MEIKYOKAN */
    {NO_REVERB, 0},              /* STAGE_FUTURE_DUMP_SITE */
    {NO_REVERB, 0},              /* STAGE_SNAKE_CLIFF */
    {NO_REVERB, 0},              /* STAGE_KIMIGAURA_COAST */
    {NO_REVERB, 0},              /* STAGE_ORANGE_TREE_FIELDS */
    {NO_REVERB, 0},              /* STAGE_CASTLE_ROOFTOP */
    {NO_REVERB, 0},              /* STAGE_UNDER_LOST_BRIDGE */
    {NO_REVERB, 0},              /* STAGE_TERRACED_FIELDS */
    {NO_REVERB, 0},              /* STAGE_UNDER_RAILWAY */
    {NO_REVERB, 0},              /* STAGE_WATER_TOWER_DAM */
    {NO_REVERB, 0},              /* STAGE_LUMBERYARD */
    {NO_REVERB, 0},              /* STAGE_LIGHTHOUSE */
    {NO_REVERB, 0},              /* STAGE_KABUKI_STAGE */
    {NO_REVERB, 0},              /* STAGE_ABANDONED_SHIP */
    {NO_REVERB, 0},              /* STAGE_FERRY_DOCK */
    {NO_REVERB, 0},              /* STAGE_SHAINTO_GATE */
    {SS_REV_TYPE_STUDIO_C, 32},  /* STAGE_PURPLE_CAVERN */
    {NO_REVERB, 0},              /* STAGE_ABANDONED_STATION */
    {NO_REVERB, 0},              /* STAGE_RAILWAY */
    {NO_REVERB, 0},              /* STAGE_ABANDONED_TUNNEL */
    {NO_REVERB, 8},              /* STAGE_ABANDONED_SHIPYARD */
    {NO_REVERB, 0},              /* STAGE_BUILDING_ROOFTOP */
    {SS_REV_TYPE_ROOM, 8},       /* STAGE_BUILDING_PARKING_LOT */
    {NO_REVERB, 2},              /* STAGE_SHAINTO_CEMETERY */
    {SS_REV_TYPE_ROOM, 2},       /* STAGE_TEAM_1 */
    {SS_REV_TYPE_ROOM, 2},       /* STAGE_TEAM_2 */
    {NO_REVERB, 2},              /* STAGE_CHANBARA */
    {SS_REV_TYPE_ROOM, 2},       /* STAGE_PRACTICE_1 */
    {SS_REV_TYPE_ROOM, 2},       /* STAGE_PRACTICE_2 */
    {NO_REVERB, 0},              /* stage 37 */
};
/* clang-format on */
