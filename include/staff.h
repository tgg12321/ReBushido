#ifndef STAFF_H
#define STAFF_H

/* The staff roll (staff.c): the credits scroll up the screen and a last
 * card stays. The screen that runs it is staff_Init / staff_Update. */

#include "common.h"
#include "sprite.h"

/* STAFF.BIN, the staff roll's resource file. After the head, in the same
 * list as the head's own (so it ends in -1): the sheets of the roll, from
 * the top down, the last of them the card the roll ends on. */
typedef struct {
    SpriteFileHead head;
    SpriteSheetSlot sheets[0];
} StaffFile;

/* The staff roll's screen (enum Screen; fight_after.c). */
extern void staff_Init(void);
extern void staff_Update(void);

/* Loads STAFF.BIN to `memory` and starts the roll, over black. */
extern s32 staff_LoadRoll(s32 memory);
/* A frame of it. Returns 1 when the roll is over. */
extern s32 staff_UpdateRoll(void);

#endif /* STAFF_H */
