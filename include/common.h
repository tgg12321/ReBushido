#ifndef COMMON_H
#define COMMON_H

/* What every file starts from: the integer types, NULL, the scratchpad's
 * address and a point of three words. */

/* The integer types, as GCC 2.7.2 has them on this machine. */
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;
typedef unsigned long long u64;
typedef signed long long s64;

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

/* The scratchpad: a kilobyte of fast memory at an address of its own, which
 * the game uses for whatever it is working on at the moment. Each subsystem
 * lays its own things out in it (RENDER_..., SPAD in fighter.h, EFF_SPAD,
 * CLOTH_SPAD, the camera's): what one puts there another writes over. */
#define SCRATCHPAD 0x1F800000

/* An offset or address cut down to a multiple of four: a file's sections
 * start on a word. */
#define ALIGN4(x) (((u32)(x) >> 2) << 2)

/* A point or vector in world units. Sony's VECTOR carries a fourth word;
 * where the game keeps exactly three it uses this. */
typedef struct {
    s32 x, y, z;
} Vec3;

#endif /* COMMON_H */
