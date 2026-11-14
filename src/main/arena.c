/* The arena: the buffer everything loaded for a fight lives in (the models,
 * the weapons, the stage), cut into blocks laid end to end. There are no
 * holes: when a block is freed or resized the blocks after it are moved to
 * close the gap, and each one's owner is called to fix its pointers. */
#include "common.h"
#include "bb2.h"

static u8 g_arena[ARENA_SIZE];
/* The open blocks, in address order. */
static ArenaBlock g_arena_blocks[ARENA_BLOCK_COUNT];

static s32 g_arena_free_ptr;  /* the end of the last block */
static s32 g_arena_free_size; /* from there to the end of the arena */
/* The most that was ever in use, of what arena_UpdatePeak was shown: opening
 * and resizing blocks does not count by itself. */
static s32 g_arena_peak;
static s32 g_arena_block_count;

/* Empties the arena: no block open, all of it free, and the weapons that
 * were in it forgotten. */
void arena_Reset(void) {
    s32 i;

    for (i = 0; i < ARENA_BLOCK_COUNT; i++) {
        g_arena_blocks[i].id = ARENA_NO_BLOCK;
    }
    g_arena_free_ptr = (s32)g_arena;
    g_arena_free_size = ARENA_SIZE;
    g_arena_block_count = 0;
    g_arena_peak = 0;
    resource_ForgetWeapons();
}

/* Notes that the arena is in use as far as `end_addr` (0: as far as the end
 * of the last block), keeping the most that ever was. Stops the game if that
 * is past the arena's end. */
void arena_UpdatePeak(s32 end_addr) {
    s32 peak;

    if (!end_addr) {
        end_addr = g_arena_free_ptr;
    }
    end_addr -= (s32)g_arena;
    peak = end_addr;
    if (end_addr < g_arena_peak) {
        peak = g_arena_peak;
    }
    g_arena_peak = peak;
    if (end_addr >= ARENA_SIZE) {
        sys_Trap();
    }
}

/* Moves the blocks from `first` to the last by `delta` bytes and tells their
 * owners. The drawing is let finish first, as it may be reading from them. */
static void arena_ShiftBlocks(s32 first, s32 delta) {
    s32 size;
    s32 i;
    s32 src;
    s32 dst;
    s32 count;

    size = 0;
    src = g_arena_blocks[first].start;
    count = g_arena_block_count;
    dst = src + delta;
    for (i = first; i < count; i++) {
        size += g_arena_blocks[i].size;
    }
    if (size != 0) {
        DrawSync(0);
        mem_MoveWords(src, dst, size);
        for (i = first; i < g_arena_block_count; i++) {
            g_arena_blocks[i].start += delta;
            if (g_arena_blocks[i].on_move) {
                g_arena_blocks[i].on_move(g_arena_blocks[i].id, delta);
            }
        }
    }
    g_arena_free_ptr += delta;
    g_arena_free_size -= delta;
}

/* Frees a block: the blocks after it move down over it, and their entries
 * move up the table. Nothing happens if it is not open. */
void arena_Free(s32 block_id) {
    s32 i;
    s32 j;
    s32 last;

    for (i = 0; i < g_arena_block_count; i++) {
        if (g_arena_blocks[i].id == block_id) {
            arena_ShiftBlocks(i + 1, -g_arena_blocks[i].size);
            if (i < g_arena_block_count - 1) {
                for (j = i + 1; j < g_arena_block_count; j++) {
                    g_arena_blocks[j - 1] = g_arena_blocks[j];
                }
            }
            last = g_arena_block_count - 1;
            g_arena_blocks[last].id = ARENA_NO_BLOCK;
            g_arena_blocks[last].on_move = NULL;
            g_arena_block_count = last;
            return;
        }
    }
}

/* Makes a block `new_size` bytes long: the blocks after it move by the
 * difference. */
void arena_Resize(s32 block_id, s32 new_size) {
    s32 i;
    s32 growth;

    for (i = 0; i < g_arena_block_count; i++) {
        if (g_arena_blocks[i].id == block_id) {
            growth = new_size - g_arena_blocks[i].size;
            if (growth == 0) {
                return;
            }
            arena_ShiftBlocks(i + 1, growth);
            g_arena_blocks[i].size = new_size;
            return;
        }
    }
}

/* Opens a block at the end of the last one, freeing the block of that id
 * if there is one, and returns where it starts. It has no size until
 * arena_CloseBlock gives it one. */
s32 *arena_OpenBlock(s32 block_id) {
    ArenaBlock *block;
    s32 index;
    s32 start;

    arena_Free(block_id);
    index = g_arena_block_count;
    block = &g_arena_blocks[index];
    g_arena_block_count = index + 1;
    start = g_arena_free_ptr;
    block->start = start;
    block->id = block_id;
    block->on_move = NULL;
    return (s32 *)start;
}

/* Ends the block being filled at `end_addr`: its size is from the end of
 * the block before it to there. Stops the game if the block is not open. */
void arena_CloseBlock(s32 block_id, s32 end_addr) {
    s32 i;
    ArenaBlock *block;

    for (i = 0; i < g_arena_block_count; i++) {
        block = &g_arena_blocks[i];
        if (block->id == block_id) {
            break;
        }
    }
    if (i < g_arena_block_count) {
        s32 size = end_addr - g_arena_free_ptr;
        block->size = size;
        g_arena_free_ptr += size;
        g_arena_free_size -= size;
        return;
    }
    sys_Trap();
}

void arena_SetMoveCallback(s32 block_id, ArenaMoveFn on_move) {
    s32 i;
    s32 count = g_arena_block_count;

    for (i = 0; i < count; i++) {
        if (g_arena_blocks[i].id == block_id) {
            g_arena_blocks[i].on_move = on_move;
            return;
        }
    }
}

void arena_ClearMoveCallback(s32 block_id) {
    s32 i;
    s32 count = g_arena_block_count;

    for (i = 0; i < count; i++) {
        if (g_arena_blocks[i].id == block_id) {
            g_arena_blocks[i].on_move = NULL;
            return;
        }
    }
}

ArenaBlock *arena_FindBlock(s32 block_id) {
    s32 i;
    s32 count = g_arena_block_count;

    for (i = 0; i < count; i++) {
        if (g_arena_blocks[i].id == block_id) {
            return &g_arena_blocks[i];
        }
    }
    return NULL;
}

s32 *arena_GetBlockAddr(s32 block_id) {
    ArenaBlock *block = arena_FindBlock(block_id);

    if (block) {
        return (s32 *)block->start;
    }
    return NULL;
}

/* Empty, and nothing calls it. A guess from where it sits: it printed the
 * block table in development builds. */
static void arena_DebugDump(void) {}

s32 arena_GetFreeSize(void) { return g_arena_free_size; }

/* The least room there has been. Nothing calls it. */
static s32 arena_GetMinFree(void) { return ARENA_SIZE - g_arena_peak; }

/* Nothing calls it. */
static s32 arena_GetFreePtr(void) { return g_arena_free_ptr; }

s32 arena_GetSize(void) { return ARENA_SIZE; }

void *arena_GetBase(void) { return g_arena; }

void arena_Copy(s32 src_addr, s32 dest_addr, s32 byte_count) {
    arena_UpdatePeak(dest_addr + byte_count);
    mem_MoveWords(src_addr, dest_addr, byte_count);
}
