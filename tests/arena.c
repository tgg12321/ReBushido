/* The arena's bookkeeping (arena.c) through a script of opens and frees. */
#include "src/main/arena.c"
#include "runtime/runtime.h"
#include "stubs/sys.c"

/* Stand-ins: the arena waits for the drawing to finish before it moves
 * anything, and tells the weapons' loader when it is emptied. Neither
 * matters here. */
s32 DrawSync(s32 mode) { return 0; }

void resource_ForgetWeapons(void) {}

/* What the blocks' owners were told: the last move of each block, and how
 * many there were. */
static s32 g_test_moved_by[ARENA_BLOCK_COUNT + 1];
static s32 g_test_move_count;

static void test_OnMove(s32 block_id, s32 delta) {
    g_test_moved_by[block_id] = delta;
    g_test_move_count++;
}

/* Opens a block, fills it with `size` bytes that say whose they are, and
 * closes it. Returns where it is. */
static u8 *test_Fill(s32 block_id, s32 size) {
    u8 *start = (u8 *)arena_OpenBlock(block_id);
    s32 i;

    for (i = 0; i < size; i++) {
        start[i] = (u8)(block_id * 16 + i % 13);
    }
    arena_CloseBlock(block_id, (s32)start + size);
    return start;
}

/* Whether a block is where it should be, with its bytes as they were
 * written. */
static s32 test_Holds(s32 block_id, u8 *where, s32 size) {
    ArenaBlock *block = arena_FindBlock(block_id);
    s32 i;

    if (!block || block->start != (s32)where || block->size != size ||
        (u8 *)arena_GetBlockAddr(block_id) != where) {
        return 0;
    }
    for (i = 0; i < size; i++) {
        if (where[i] != (u8)(block_id * 16 + i % 13)) {
            return 0;
        }
    }
    return 1;
}

/* Three blocks are laid end to end; the first grows, the middle one is
 * freed, the first is opened again: each time the blocks after it move,
 * bytes and all, and their owners are told by how much. Then the two ways
 * the arena stops the game. */
void test_Main(void) {
    u8 *base;

    arena_Reset();
    base = arena_GetBase();
    CHECK_EQ(arena_GetFreeSize(), ARENA_SIZE);
    CHECK_EQ(arena_GetSize(), ARENA_SIZE);
    CHECK(arena_FindBlock(ARENA_STAGE) == NULL);
    CHECK(arena_GetBlockAddr(ARENA_STAGE) == NULL);

    /* a model's data, the stage and a tiny model, one after another */
    CHECK(test_Fill(ARENA_MODEL_DATA, 0x100) == base);
    CHECK(test_Fill(ARENA_STAGE, 0x200) == base + 0x100);
    CHECK(test_Fill(ARENA_TINY_MODEL, 0x80) == base + 0x300);
    CHECK_EQ(arena_GetFreeSize(), ARENA_SIZE - 0x380);
    CHECK_EQ(arena_GetFreePtr(), (s32)base + 0x380);
    arena_SetMoveCallback(ARENA_STAGE, test_OnMove);
    arena_SetMoveCallback(ARENA_TINY_MODEL, test_OnMove);

    /* the first grows by 0x80: the two after it move up */
    arena_Resize(ARENA_MODEL_DATA, 0x180);
    CHECK_EQ(g_test_move_count, 2);
    CHECK_EQ(g_test_moved_by[ARENA_STAGE], 0x80);
    CHECK_EQ(g_test_moved_by[ARENA_TINY_MODEL], 0x80);
    CHECK(test_Holds(ARENA_STAGE, base + 0x180, 0x200));
    CHECK(test_Holds(ARENA_TINY_MODEL, base + 0x380, 0x80));
    CHECK_EQ(arena_GetFreeSize(), ARENA_SIZE - 0x400);
    /* to the size it has: nothing happens */
    arena_Resize(ARENA_MODEL_DATA, 0x180);
    CHECK_EQ(g_test_move_count, 2);

    /* the stage is freed: the tiny model moves down over it */
    arena_Free(ARENA_STAGE);
    CHECK_EQ(g_test_move_count, 3);
    CHECK_EQ(g_test_moved_by[ARENA_TINY_MODEL], -0x200);
    CHECK(arena_FindBlock(ARENA_STAGE) == NULL);
    CHECK(test_Holds(ARENA_TINY_MODEL, base + 0x180, 0x80));
    CHECK_EQ(arena_GetFreeSize(), ARENA_SIZE - 0x200);
    /* freeing what is not there does nothing */
    arena_Free(ARENA_STAGE);
    CHECK_EQ(g_test_move_count, 3);

    /* opening a block that is open frees the old one first: the tiny
     * model moves to the bottom and the new block comes after it */
    CHECK(test_Fill(ARENA_MODEL_DATA, 0x40) == base + 0x80);
    CHECK_EQ(g_test_moved_by[ARENA_TINY_MODEL], -0x180);
    CHECK(test_Holds(ARENA_TINY_MODEL, base, 0x80));
    CHECK(test_Holds(ARENA_MODEL_DATA, base + 0x80, 0x40));
    CHECK_EQ(arena_GetFreeSize(), ARENA_SIZE - 0xC0);

    /* an owner that asks not to be told any more is not */
    arena_ClearMoveCallback(ARENA_MODEL_DATA);
    arena_ClearMoveCallback(ARENA_TINY_MODEL);
    arena_Resize(ARENA_TINY_MODEL, 0x60);
    CHECK_EQ(g_test_move_count, 4);
    CHECK(test_Holds(ARENA_MODEL_DATA, base + 0x60, 0x40));

    /* the most that was ever in use is only what arena_UpdatePeak has been
     * shown: nothing yet, though 0x400 bytes were in blocks a while ago.
     * Shown nothing, it takes the end of the last block. arena_Copy shows
     * it where it writes */
    CHECK_EQ(arena_GetMinFree(), ARENA_SIZE);
    arena_UpdatePeak(0);
    CHECK_EQ(arena_GetMinFree(), ARENA_SIZE - 0xA0);
    arena_UpdatePeak((s32)base + 0x400);
    CHECK_EQ(arena_GetMinFree(), ARENA_SIZE - 0x400);
    arena_Copy((s32)base, (s32)base + 0x1000, 0x20);
    CHECK_EQ(arena_GetMinFree(), ARENA_SIZE - 0x1020);
    CHECK(test_BytesEqual(base, base + 0x1000, 0x20));

    /* the game stops when a block that is not open is closed, and when
     * the arena is used past its end */
    CHECK_EQ(g_test_traps, 0);
    arena_CloseBlock(ARENA_CUTSCENE, (s32)base);
    CHECK_EQ(g_test_traps, 1);
    arena_UpdatePeak((s32)base + ARENA_SIZE);
    CHECK_EQ(g_test_traps, 2);

    arena_Reset();
    CHECK_EQ(arena_GetFreeSize(), ARENA_SIZE);
    CHECK(arena_FindBlock(ARENA_TINY_MODEL) == NULL);
}
