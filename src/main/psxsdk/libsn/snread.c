/* LIBSN SNREAD: _SN_read, one read request to the development host. Assembly
 * in SN Systems' library: the request is a break instruction with its
 * arguments in registers, which has no C form. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", _SN_read);
