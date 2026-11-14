#ifndef LINK_H
#define LINK_H

/* Link-cable play: two consoles joined by the serial cable, each sending the
 * other its pad every frame (link.c). */

#include "common.h"
#include "pad.h"

/* Which of the two consoles this one is (0 or 1); decided when connecting. */
extern s32 g_link_side;
/* Set when the link was dropped because the other console stopped answering. */
extern u8 g_link_aborted;
/* The other console's unlocks, received while connecting. */
extern s32 g_link_peer_unlock_mask;

/* Connects to the other console. Returns 1 once linked, -1 if cancelled or
 * nothing answered. */
extern s32 link_Connect(void);
extern void link_RequestClose(void);

/* Sends this console's pad and replaces pad->held / pad->type with both
 * consoles' pads. Call once a frame while linked. */
extern void link_ExchangePad(PadState *pad);
/* The same with no buttons, to keep the link alive when no input is read. */
extern void link_ExchangeIdle(void);

/* The unlock mask squeezed into the sixteen bits the link sends at a time,
 * and back (only the fighters that can be locked have a bit). */
extern s32 link_PackUnlockMask(s32 unlock_mask);
extern s32 link_UnpackUnlockMask(s32 packed);

/* Bracket a stretch where this console cannot exchange every frame (loading):
 * the other console is told to wait. */
extern void link_BeginWait(void);
extern void link_EndWait(void);

/* Used only inside link.c. */

/* The check words of what was last sent, of what was sent before it and of
 * what was last received (globals.c has them: the code reaches them by
 * their full address). */
extern s32 g_link_send_check_word;
extern s32 g_link_prev_send_check_word;
extern s32 g_link_recv_check_word;

#endif /* LINK_H */
