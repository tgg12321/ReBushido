/* The PlayStation's memory in the native port (docs/PORT.md, "Memory").
 *
 * The game's own variables are the PC program's: the compiler and the
 * linker place them, wherever they like below 4 GB. But the game also
 * reaches memory by a number written into its source, and keeps
 * addresses in 32-bit words and in the 24 bits of a primitive's link. So
 * the port, whether its own pointers are 32 bits wide or 64
 * (docs/PORT.md, "64 bits"), has the console's memory at the console's
 * addresses:
 *
 *   PORT_RAM_ADDR, 2 MB   the RAM. What the game reaches there by a
 *                      constant is all above the PlayStation program's
 *                      own place (include/sys.h, motion.h): the common
 *                      sound bank (SYS_SOUND_VAB_ADDR), the effects
 *                      (SYS_EFFECTS_ADDR), the movie's work area
 *                      (SYS_MOVIE_WORK_ADDR), the load area
 *                      (SYS_LOAD_ADDR) and the three places in it
 *                      motion files are put (MOTION_WIN_FRAMES_2_ADDR,
 *                      MOTION_COMMON_FILE_ADDR,
 *                      MOTION_FIGHTER_FILE_ADDR), the staging area
 *                      (SYS_STAGING_ADDR), the two buffers of
 *                      primitives (SYS_PRIM_BUFFER_0,
 *                      SYS_PRIM_BUFFER_1), and the stack's top
 *                      (SYS_STACK_TOP), which the port gives SetSp and
 *                      does not use: the game's stack is elsewhere
 *                      (PORT_STACK_ADDR, port_platform.h).
 *                      Below the sound bank nothing is read or
 *                      written: the program that lives there on the
 *                      console is this one, elsewhere.
 *                      A save file may carry patches, words to write
 *                      anywhere in the RAM (memcard_save.c): on the
 *                      port those land in this memory too, and below
 *                      the sound bank they change nothing.
 *   SCRATCHPAD, 1 KB   the scratchpad.
 *
 * tests/port/memory.py holds the source to this list: every address
 * written into the game's C is in one of the two, and is one of these
 * names.
 *
 * What the port cannot give is address 0. The console has its kernel's
 * memory there, so a read or a write through a pointer that was never
 * set does no harm on it, and the original has a few. A PC stops a
 * program that does either. Each place found is given somewhere else to
 * read or write, under RB_PORT, in the game's file with a note; the
 * ones not found yet stop the port, which says where (crash.c). */
#include "common.h"
#include "port_host.h"
#include "bb2.h"

const u32 g_port_low_memory[PORT_LOW_MEMORY_WORDS] = {0};

/* Every piece of memory at a fixed address (port_platform.h), and what
 * it is for, as the program says when the system will not give it. */
typedef struct {
    u32 address;
    u32 size;
    const char *what;
} FixedMemory;

static const FixedMemory g_port_fixed_memory[] = {
    {PORT_RAM_ADDR, PORT_RAM_SIZE,
     "the PlayStation's RAM, which the game reaches by address"},
    {SCRATCHPAD, PORT_SCRATCHPAD_SIZE,
     "the PlayStation's scratchpad, which the game reaches by address"},
    {PORT_IO_PAGE_ADDR, PORT_IO_PAGE_SIZE,
     "the sound chip's registers, which Sony's library reaches by address"},
    {PORT_STACK_ADDR, PORT_STACK_SIZE, "the stack the game runs on"},
    {PORT_PRIMS_ADDR, PORT_PRIMS_SIZE,
     "the primitives of a frame drawn further than the console's"},
};

#define FIXED_MEMORY_COUNT                                                     \
    ((s32)(sizeof(g_port_fixed_memory) / sizeof(g_port_fixed_memory[0])))

static s32 g_port_fixed_mapped;

/* All of it at once, before anything else of the program can take an
 * address of it (port_platform.h). */
void port_MapFixedMemory(void) {
    s32 i;

    if (g_port_fixed_mapped) {
        return;
    }
    for (i = 0; i < FIXED_MEMORY_COUNT; i++) {
        const FixedMemory *one = &g_port_fixed_memory[i];

        if (!port_HostMapAt(one->address, one->size)) {
            port_Fail("rebushido: the system gave no memory at 0x%08X (%u "
                      "bytes): %s\n"
                      "rebushido: nothing else may be loaded there, and the "
                      "system has to allow a program memory at that address\n",
                      one->address, one->size, one->what);
        }
    }
    g_port_fixed_mapped = 1;
}

void port_MapMemory(void) { port_MapFixedMemory(); }

/* Memory at a fixed address a part of the port asks for later
 * (port_platform.h): inside a piece given already, or the system's. */
s32 port_MapFixed(u32 address, u32 size) {
    s32 i;

    for (i = 0; i < FIXED_MEMORY_COUNT && g_port_fixed_mapped; i++) {
        const FixedMemory *one = &g_port_fixed_memory[i];

        if (address >= one->address && size <= one->size &&
            address - one->address <= one->size - size) {
            return 1;
        }
    }
    return port_HostMapAt(address, size);
}

s32 port_IsMapped(u32 address, u32 count) {
    if (address >= PORT_RAM_ADDR && address - PORT_RAM_ADDR <= PORT_RAM_SIZE &&
        count <= PORT_RAM_SIZE - (address - PORT_RAM_ADDR)) {
        return 1;
    }
    return address >= SCRATCHPAD &&
           address - SCRATCHPAD <= PORT_SCRATCHPAD_SIZE &&
           count <= PORT_SCRATCHPAD_SIZE - (address - SCRATCHPAD);
}
