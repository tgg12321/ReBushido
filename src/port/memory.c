/* The PlayStation's memory in the native port (docs/PORT.md, "Memory").
 *
 * The game's own variables are the PC program's: the compiler and the
 * linker place them, wherever they like. But the game also reaches
 * memory by a number written into its source, and keeps addresses in
 * 32-bit words and in the 24 bits of a primitive's link. So the port,
 * which is a 32-bit program, has the console's memory at the console's
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

/* Both pieces of the console's memory and the game's stack, or the
 * program stops (port_platform.h). */
void port_MapMemory(void) {
    if (!port_HostMapAt(PORT_RAM_ADDR, PORT_RAM_SIZE)) {
        port_Fail("rebushido: the system gave no memory at 0x%08X (%u bytes): "
                  "the PlayStation's RAM, which the game reaches by address\n"
                  "rebushido: the port has to be a 32-bit program, and nothing "
                  "else may be loaded there\n",
                  PORT_RAM_ADDR, PORT_RAM_SIZE);
    }
    if (!port_HostMapAt(SCRATCHPAD, PORT_SCRATCHPAD_SIZE)) {
        port_Fail("rebushido: the system gave no memory at 0x%08X (%u bytes): "
                  "the PlayStation's scratchpad, which the game reaches by "
                  "address\n",
                  (u32)SCRATCHPAD, PORT_SCRATCHPAD_SIZE);
    }
    if (!port_HostMapAt(PORT_STACK_ADDR, PORT_STACK_SIZE)) {
        port_Fail("rebushido: the system gave no memory at 0x%08X (%u bytes): "
                  "the stack the game runs on\n",
                  PORT_STACK_ADDR, PORT_STACK_SIZE);
    }
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
