/* LIBSPU S_M: the state of the sound memory allocator: how many blocks its
 * list has room for, how many are in use, and the list. */
#include "common.h"
#include "libspu_internal.h"

s32 _spu_AllocBlockNum = 0;
s32 _spu_AllocLastNum = 0;
SpuMemRec *_spu_memList = NULL;
