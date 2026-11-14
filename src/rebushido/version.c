/* ReBushido's version, as text in the executable, so that a build can be
 * told from another and from the retail game. The options page shows it
 * (options.c). */
#include "common.h"
#include "bb2.h"

/* The commit the build was made from, which the Makefile gives. */
#ifndef RB_GIT_HASH
#define RB_GIT_HASH "unknown"
#endif

const char g_rb_version[] = "ReBushido " RB_VERSION " (" RB_GIT_HASH ")";
