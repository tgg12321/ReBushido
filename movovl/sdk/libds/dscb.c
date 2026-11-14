/* LIBDS DSCB: the callback setters of the newer CD library, which the
 * streaming code clears if that library is in use. */
#include "common.h"
#include <psxsdk/libcd.h>
#include <psxsdk/libetc.h>

/* The command-complete and data-ready callbacks. The name is not Sony's. */
CdlCB ds_callbacks[2];

CdlCB DsSyncCallback(CdlCB func) {
    CdlCB old = ds_callbacks[0];
    ds_callbacks[0] = func;
    return old;
}

CdlCB DsReadyCallback(CdlCB func) {
    CdlCB old = ds_callbacks[1];
    ds_callbacks[1] = func;
    return old;
}

void (*DsDataCallback(void (*func)()))() { return DMACallback(3, func); }
