#ifndef ARENA_H
#define ARENA_H

/* The arena: the one buffer that holds everything loaded for a fight. It is
 * cut into blocks laid end to end in the order they were opened. Freeing or
 * resizing a block slides every later block along, and each block's owner is
 * told through a callback so it can fix its pointers (arena.c). */

#include "common.h"

#define ARENA_SIZE 0x45000
#define ARENA_BLOCK_COUNT 10

/* Who owns each block. A model has two: its Model record and the model data
 * read from the archive. */
enum ArenaBlockId {
    ARENA_MODEL = 0,      /* + model slot */
    ARENA_MODEL_DATA = 3, /* + model slot */
    ARENA_WEAPONS = 6,
    ARENA_STAGE = 7,
    ARENA_TINY_MODEL = 8,
    ARENA_SPARE = 9, /* nothing in the game opens it */
    ARENA_CUTSCENE = 10,
};

#define ARENA_MODEL_SLOTS 3
/* The id of an entry of the block table that is not in use. */
#define ARENA_NO_BLOCK (-1)

/* Called after a block has been moved by `delta` bytes. */
typedef void (*ArenaMoveFn)(s32 block_id, s32 delta);

/* A block: whose it is, where it is and how long. */
typedef struct {
    s16 id; /* enum ArenaBlockId, or ARENA_NO_BLOCK */
    s32 start;
    s32 size;
    ArenaMoveFn on_move;
} ArenaBlock;

extern void arena_Reset(void);

/* Opening a block: arena_OpenBlock returns where it starts (the end of the
 * last block), the caller fills it, and arena_CloseBlock says where it ends.
 * Opening an id that is already open frees the old block first. */
extern s32 *arena_OpenBlock(s32 block_id);
extern void arena_CloseBlock(s32 block_id, s32 end_addr);
extern void arena_Free(s32 block_id);
extern void arena_Resize(s32 block_id, s32 new_size);
/* The callback is a function taking (block id, delta), passed as an address
 * because the owners declare the block id with different types. */
extern void arena_SetMoveCallback(s32 block_id, ArenaMoveFn on_move);
extern void arena_ClearMoveCallback(s32 block_id);

extern ArenaBlock *arena_FindBlock(s32 block_id);
/* Where the block starts, or 0 if it is not open. */
extern s32 *arena_GetBlockAddr(s32 block_id);

/* Notes that the arena is in use up to `end_addr` (0: up to the end of the
 * last block), for the high-water mark. Stops the game if that is past the
 * end of the arena. */
extern void arena_UpdatePeak(s32 end_addr);
/* Copies into the arena, noting the use. */
extern void arena_Copy(s32 src_addr, s32 dest_addr, s32 byte_count);

extern s32 arena_GetFreeSize(void);
extern s32 arena_GetSize(void);
extern void *arena_GetBase(void);

#endif /* ARENA_H */
