#ifndef LIBETC_INTERNAL_H
#define LIBETC_INTERNAL_H

/* What LIBETC's modules share: INTR_VB and INTR_DMA each have a start
 * function, which INTR calls and which returns the function that sets that
 * module's callbacks. */

#include <psxsdk/libetc.h>

extern s32 startIntrVSync(void);
extern s32 startIntrDMA(void);

#endif /* LIBETC_INTERNAL_H */
