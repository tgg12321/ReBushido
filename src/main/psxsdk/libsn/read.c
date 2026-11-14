/* LIBSN READ: PCread, reading from a file on the development host, at most
 * 0x8000 bytes a request. */
#include "common.h"
#include <psxsdk/libsn.h>

extern s32 _SN_read(s32, s32, s32, char *);

int PCread(int fd, char *buff, int len) {
    s32 total;
    s32 chunk;
    s32 result;

    total = 0;
    if (len != 0) {
        do {
            chunk = len;
            if ((u32)0x8000 < (u32)len) {
                chunk = 0x8000;
            }
            result = _SN_read(0, fd, chunk, buff);
            total += result;
            if (result == -1) {
                return -1;
            }
            buff += result;
            len -= result;
            if (result < chunk) {
                return total;
            }
        } while (len != 0);
    }
    return total;
}
