#ifndef _LIBCOMB_H_
#define _LIBCOMB_H_

/* Sony's libcomb.h (Run-time Library Release 4.0): the link cable. The
 * macros for _comb_control are those of the later releases. */

#include "common.h"

/* status bits */
#define COMB_CTS 0x100
#define COMB_DSR 0x80
#define COMB_FE 0x20
#define COMB_OE 0x10
#define COMB_PERROR 0x8
#define COMB_TXU 0x4
#define COMB_RXRDY 0x2
#define COMB_TXRDY 0x1

/* control bits */
#define COMB_BIT_DTR 0x1
#define COMB_BIT_RTS 0x2

#define CombSioStatus() _comb_control(0, 0, 0)
#define CombSetControl(a) _comb_control(1, 1, a)
#define CombSetBPS(a) _comb_control(1, 3, a)
#define CombSetPacketSize(a) _comb_control(1, 4, a)
#define CombReset() _comb_control(2, 0, 0)
#define CombSetRTS(a) _comb_control(3, 0, a)
#define CombCTS() _comb_control(3, 1, 0)
#define CombWaitCallback(a) _comb_control(4, 0, a)

/*
 * Prototypes
 */
extern void AddCOMB(void);
extern void DelCOMB(void);
extern void ChangeClearSIO(s32);
extern s32 _comb_control(u32, u32, u32);

#endif /* _LIBCOMB_H_ */
