#ifndef COMMON_H
#define COMMON_H

/* What every file starts from: the integer types, NULL, the scratchpad's
 * address, an address held in a number and a point of three words. */

#include "rebushido.h"

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

/* An address held in a number, where a cast makes a pointer of it:
 * `(Fighter *)ADDR(value)`. The game keeps addresses in 32-bit words
 * everywhere, and on the PlayStation, where a pointer is such a word, this
 * is the value as it stands. The native port's pointers are 64 bits wide
 * (docs/PORT.md, "64 bits"): everything the game can point at is below
 * 4 GB there, so the address is the number's low 32 bits with nothing
 * above them, whatever its sign. (A pointer made a number needs nothing:
 * the cast keeps its low 32 bits, which is all of it.) */
#if RB_PORT
/* The guard is the port's own headers' too: the files of the port that
 * talk to a C library and to SDL or EGL are not compiled with the
 * switch on, and declare the type for themselves (port_host.h,
 * port_remaster.h). A typedef may be written twice only in C11, and
 * those files are gnu99. */
#ifndef PORT_ADDRESS_DEFINED
#define PORT_ADDRESS_DEFINED
typedef __UINTPTR_TYPE__ PortAddress;
#endif
#define ADDR(value) ((PortAddress)(u32)(PortAddress)(value))
#else
#define ADDR(value) (value)
#endif

/* A pointer kept in four bytes: a member of a structure whose layout is
 * the PlayStation's and has to stay so in the port, or an entry of a list
 * of pointers in a file (docs/PORT.md, "64 bits"). It is declared
 * `PTR32(SpriteSheet *) sheet;`, read as `PTR(file->sheet)` and written
 * with `SET_PTR(file->sheet, value)`; `NEXT_PTR(slot)` is the slot's
 * pointer, and moves the slot on by one of what it points at, and
 * `ADD_PTR(slot, count)` moves it on by `count`. On the PlayStation these
 * are the type, the slot, an assignment, `slot++` and `slot += count`. In the
 * port a slot is a 32-bit address that carries the type it points at (the array
 * of none, which takes no room), so PTR() gives a pointer of that type, and a
 * use without PTR() does not compile. */
#if RB_PORT
#define PTR32(type)                                                            \
    union {                                                                    \
        u32 address;                                                           \
        __typeof__(type) target[0] __attribute__((packed));                    \
    }
#define PTR(slot) ((__typeof__((slot).target[0]))ADDR((slot).address))
#define SET_PTR(slot, value)                                                   \
    ({                                                                         \
        __typeof__((slot).target[0]) port_value = (value);                     \
        (slot).address = (u32)ADDR(port_value);                                \
    })
#define NEXT_PTR(slot)                                                         \
    ({                                                                         \
        __typeof__((slot).target[0]) port_value = PTR(slot);                   \
        (slot).address += sizeof(*port_value);                                 \
        port_value;                                                            \
    })
#define ADD_PTR(slot, count)                                                   \
    ((slot).address += (count) * sizeof(*(slot).target[0]))
#else
#define PTR32(type) __typeof__(type)
#define PTR(slot) (slot)
#define SET_PTR(slot, value) ((slot) = (value))
#define NEXT_PTR(slot) ((slot)++)
#define ADD_PTR(slot, count) ((slot) += (count))
#endif

/* An offset or address cut down to a multiple of four: a file's sections
 * start on a word. */
#define ALIGN4(x) (((u32)(x) >> 2) << 2)

/* A point or vector in world units. Sony's VECTOR carries a fourth word;
 * where the game keeps exactly three it uses this. */
typedef struct {
    s32 x, y, z;
} Vec3;

#endif /* COMMON_H */
