/* LIBSN SNDEF: the default stack and RAM sizes the program's entry point
 * reads. A program that wants others defines its own. */
#include "common.h"

u32 _stacksize = 0x00008000;
u32 _ramsize = 0x00800000;
