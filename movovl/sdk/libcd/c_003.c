/* LIBCD C_003: StUnSetRing, which takes the streaming library's callbacks
 * off the CD interrupt. */
#include "libcd_st.h"
#include "libcd_reg.h"

extern s32 DS_active;
void (*DsDataCallback(void (*func)()))();
void (*DsReadyCallback(void (*func)()))();

void StUnSetRing(void) {
    EnterCriticalSection();
    if (DS_active == 1) {
        DsDataCallback(NULL);
        DsReadyCallback(NULL);
    } else {
        CdDataCallback(NULL);
        CdReadyCallback(NULL);
    }
    *cd_reg0 = 0;
    *cd_reg3 = 0;
    ExitCriticalSection();
}
