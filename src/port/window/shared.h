#ifndef PORT_WINDOW_SHARED_H
#define PORT_WINDOW_SHARED_H

/* What the window's SDL files take from the rest of the port.
 * include/port_platform.h has all of it, but those files cannot include
 * that header: it declares the game's own `main` and the compiler's
 * `memset` in the game's way, and they are compiled with a C library's
 * headers, which declare both another way. So the few names are
 * declared again here, word for word; keymap.c includes this header
 * beside port_platform.h, and the compiler stops there if one of them
 * has come to differ. */

#include "common.h"

#define PORT_EXIT_OK 0
#define PORT_EXIT_FAILED 1
#define PORT_EXIT_USAGE 2

extern void port_Quit(s32 code) __attribute__((noreturn));
extern u32 port_GetBlankCount(void);

extern void (*g_port_input_hook)(u32 blank);
extern u32 (*g_port_device_hook)(s32 pad);
extern void (*g_port_blank_hook)(u32 blank);
extern s32 (*g_port_press_hook)(s32 pad, u32 button, s32 down);
extern void (*g_port_quit_hook)(s32 code);
extern void (*g_port_crash_hook)(void);
extern void (*g_port_disc_hook)(s32 kind, u32 sector);
#define PORT_RECENT_CALLS 32
extern u32 g_port_recent_calls[PORT_RECENT_CALLS];
extern u32 g_port_recent_call_count;
extern const char *port_FindFunction(u32 address, u32 *offset);
extern void port_SetRunAhead(s32 frames);
extern s32 g_port_unseen;
extern void port_ReadDevicesNow(void);
extern void (*g_port_boundary_hook)(void);
extern void (*g_port_ahead_blank_hook)(u32 blank);
extern void (*g_port_ahead_done_hook)(void);

#endif /* PORT_WINDOW_SHARED_H */
