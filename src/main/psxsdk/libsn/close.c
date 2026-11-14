/* LIBSN CLOSE: PCclose, closing a file on the development host. Assembly in
 * SN Systems' library: the request is a break instruction with its arguments
 * in registers, which has no C form. */
#include "include_asm.h"

INCLUDE_ASM("asm/funcs", PCclose);
