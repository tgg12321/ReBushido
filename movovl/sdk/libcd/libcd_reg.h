/* The CD-ROM controller's four registers. Every module of the streaming
 * library but CDREAD2 has its own copy of these pointers, used or not. */
#ifndef LIBCD_REG_H
#define LIBCD_REG_H

#include "common.h"

static volatile u8 *cd_reg0 = (u8 *)0x1F801800;
static volatile u8 *cd_reg1 = (u8 *)0x1F801801;
static volatile u8 *cd_reg2 = (u8 *)0x1F801802;
static volatile u8 *cd_reg3 = (u8 *)0x1F801803;

#endif
