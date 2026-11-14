#ifndef PORT_GTE_H
#define PORT_GTE_H

/* The GTE in C, for the native port (docs/PORT.md): the PlayStation's
 * geometry coprocessor as a set of registers and the operations on them.
 * On the console the game reaches it by coprocessor instructions; in the
 * port every one of those is a call of a function here. The PlayStation
 * builds never see this header. */

#include "common.h"

/* ---- the registers ---- */

/* The data registers, by number (the `$n` of mtc2, mfc2, lwc2 and swc2). */
enum PortGteData {
    PORT_GTE_VXY0, /* vector 0: x in the low half, y in the high */
    PORT_GTE_VZ0,
    PORT_GTE_VXY1,
    PORT_GTE_VZ1,
    PORT_GTE_VXY2,
    PORT_GTE_VZ2,
    PORT_GTE_RGBC, /* a colour and, in the top byte, a primitive's code */
    PORT_GTE_OTZ,  /* the average depth */
    PORT_GTE_IR0,  /* the interpolation factor */
    PORT_GTE_IR1,  /* the 16-bit accumulator: a vector */
    PORT_GTE_IR2,
    PORT_GTE_IR3,
    PORT_GTE_SXY0, /* the last three screen positions, oldest first */
    PORT_GTE_SXY1,
    PORT_GTE_SXY2,
    PORT_GTE_SXYP, /* reads as SXY2; a write pushes onto the three */
    PORT_GTE_SZ0,  /* the last four screen depths, oldest first */
    PORT_GTE_SZ1,
    PORT_GTE_SZ2,
    PORT_GTE_SZ3,
    PORT_GTE_RGB0, /* the last three colours, oldest first */
    PORT_GTE_RGB1,
    PORT_GTE_RGB2,
    PORT_GTE_RES1, /* no use; keeps what is written */
    PORT_GTE_MAC0, /* the 32-bit accumulators: a sum, and a vector */
    PORT_GTE_MAC1,
    PORT_GTE_MAC2,
    PORT_GTE_MAC3,
    PORT_GTE_IRGB, /* IR1 to IR3 as a 15-bit colour: a write sets them */
    PORT_GTE_ORGB, /* the same, read only */
    PORT_GTE_LZCS, /* a number ... */
    PORT_GTE_LZCR, /* ... and how many of its top bits equal its sign */
    PORT_GTE_DATA_COUNT
};

/* The control registers, by number (the `$n` of ctc2 and cfc2). A matrix
 * is nine halfwords in five registers, a row after a row. */
enum PortGteControl {
    PORT_GTE_ROTATION, /* the rotation matrix: five registers */
    PORT_GTE_TRX = 5,  /* the translation */
    PORT_GTE_TRY,
    PORT_GTE_TRZ,
    PORT_GTE_LIGHT,    /* the light matrix: five registers */
    PORT_GTE_RBK = 13, /* the background colour */
    PORT_GTE_GBK,
    PORT_GTE_BBK,
    PORT_GTE_LIGHT_COLOR, /* the light colour matrix: five registers */
    PORT_GTE_RFC = 21,    /* the far colour, the colour of the fog */
    PORT_GTE_GFC,
    PORT_GTE_BFC,
    PORT_GTE_OFX, /* the screen's offset, 16.16 */
    PORT_GTE_OFY,
    PORT_GTE_PLANE, /* the distance to the projection plane */
    PORT_GTE_DQA,   /* the depth cue's slope and offset */
    PORT_GTE_DQB,
    PORT_GTE_ZSF3, /* the scales of the average depth */
    PORT_GTE_ZSF4,
    PORT_GTE_FLAG, /* what the last operation ran out of */
    PORT_GTE_CONTROL_COUNT
};

/* The bits of FLAG. An operation clears the register and then sets the
 * bit of each limit it hit. */
#define PORT_GTE_FLAG_ERROR 0x80000000U        /* one of ERROR_BITS is set */
#define PORT_GTE_FLAG_MAC1_POSITIVE 0x40000000 /* past 43 bits; MAC2, MAC3 */
#define PORT_GTE_FLAG_MAC1_NEGATIVE 0x08000000 /* follow, a bit lower each */
#define PORT_GTE_FLAG_IR1 0x01000000   /* IR1 did not fit; IR2, IR3 follow */
#define PORT_GTE_FLAG_RED 0x00200000   /* a colour's byte; green, blue follow */
#define PORT_GTE_FLAG_DEPTH 0x00040000 /* SZ3 or OTZ did not fit 16 bits */
#define PORT_GTE_FLAG_DIVIDE 0x00020000        /* the division overflowed */
#define PORT_GTE_FLAG_MAC0_POSITIVE 0x00010000 /* MAC0 past 31 bits */
#define PORT_GTE_FLAG_MAC0_NEGATIVE 0x00008000
#define PORT_GTE_FLAG_SX 0x00004000 /* SX2 did not fit 11 bits */
#define PORT_GTE_FLAG_SY 0x00002000
#define PORT_GTE_FLAG_IR0 0x00001000        /* IR0 was not 0 to 0x1000 */
#define PORT_GTE_FLAG_ERROR_BITS 0x7F87E000 /* all but IR3 and the colours */
#define PORT_GTE_FLAG_WRITABLE 0x7FFFF000

/* The fields of an operation's instruction word. */
#define PORT_GTE_OP_MASK 0x3F
#define PORT_GTE_OP_LM 0x400   /* IR1 to IR3 are held at 0, not -0x8000 */
#define PORT_GTE_OP_SF 0x80000 /* the results lose 12 bits of fraction */
#define PORT_GTE_OP_TRANSLATION_SHIFT 13 /* MVMVA: which vector is added */
#define PORT_GTE_OP_VECTOR_SHIFT 15      /* which vector is multiplied */
#define PORT_GTE_OP_MATRIX_SHIFT 17      /* and by which matrix */

/* The operations: the low six bits of the word. */
enum PortGteOp {
    PORT_GTE_RTPS = 0x01,  /* a point to the screen */
    PORT_GTE_NCLIP = 0x06, /* which way a triangle faces */
    PORT_GTE_OP = 0x0C,    /* an outer product */
    PORT_GTE_DPCS = 0x10,  /* a colour faded to the far colour */
    PORT_GTE_INTPL = 0x11, /* a vector faded to the far colour */
    PORT_GTE_MVMVA = 0x12, /* a matrix times a vector, plus a vector */
    PORT_GTE_NCDS = 0x13,  /* a normal lit, coloured and faded */
    PORT_GTE_CDP = 0x14,   /* a light coloured and faded */
    PORT_GTE_NCDT = 0x16,  /* NCDS for three normals */
    PORT_GTE_NCCS = 0x1B,  /* a normal lit and coloured */
    PORT_GTE_CC = 0x1C,    /* a light coloured */
    PORT_GTE_NCS = 0x1E,   /* a normal lit */
    PORT_GTE_NCT = 0x20,   /* NCS for three normals */
    PORT_GTE_SQR = 0x28,   /* a vector's squares */
    PORT_GTE_DCPL = 0x29,  /* a colour lit and faded */
    PORT_GTE_DPCT = 0x2A,  /* DPCS for the three colours */
    PORT_GTE_AVSZ3 = 0x2D, /* the average of three depths */
    PORT_GTE_AVSZ4 = 0x2E, /* and of four */
    PORT_GTE_RTPT = 0x30,  /* three points to the screen */
    PORT_GTE_GPF = 0x3D,   /* a vector scaled */
    PORT_GTE_GPL = 0x3E,   /* a vector scaled and added to the sum */
    PORT_GTE_NCCT = 0x3F   /* NCCS for three normals */
};

/* The coprocessor. The registers are kept as they read back: a 16-bit one
 * already sign-extended or cut to its 16 bits. */
typedef struct {
    u32 data[PORT_GTE_DATA_COUNT];
    u32 control[PORT_GTE_CONTROL_COUNT];
} PortGte;

extern PortGte g_port_gte;

/* ---- what an instruction does ---- */

/* mtc2 and lwc2, mfc2 and swc2: a data register written and read. */
extern void port_GteWriteData(s32 index, u32 value);
extern u32 port_GteReadData(s32 index);
/* ctc2 and cfc2: a control register written and read. */
extern void port_GteWriteControl(s32 index, u32 value);
extern u32 port_GteReadControl(s32 index);
/* An operation, by its instruction word (0x4A180001 is RTPS). */
extern void port_GteCommand(u32 word);

/* A matrix in memory (Sony's MATRIX): nine halfwords, which are five
 * words and five registers; with its translation, eight. */
#define PORT_GTE_MATRIX_ELEMENTS 9
#define PORT_GTE_MATRIX_WORDS 5
#define PORT_GTE_MATRIX_AND_TRANSLATION_WORDS 8

/* A run of registers, from `first` on, loaded from words in memory or
 * stored to them: what the assembly does with a row of lwc2, of swc2, or
 * of lw and ctc2. */
extern void port_GteLoadData(s32 first, const void *words, s32 count);
extern void port_GteStoreData(s32 first, void *words, s32 count);
extern void port_GteLoadControl(s32 first, const void *words, s32 count);

/* What the products of two matrices have in common. `matrix` is nine
 * halfwords, a row after a row (a MATRIX's `m`). Each of its columns is
 * loaded as vector 0 and `command` run on it (the rotation in the GTE
 * times that column); `columns` gets the three results, IR1 to IR3 of
 * each, a column after a column. */
extern void port_GteRotateColumns(
    const void *matrix, u32 command, s32 *columns);
/* Such results stored as a matrix's nine halfwords. With `last_as_word`
 * the ninth is stored as a word, as Sony's routines do: its sign goes
 * into the two bytes after the matrix. */
extern void port_GteStoreColumns(
    void *matrix, const s32 *columns, s32 last_as_word);

/* The words of the operations the C beside the assembly runs, by the
 * names the listings have for them (include/gte_macros.inc). */
#define PORT_GTE_CMD_RTPS 0x4A180001U
#define PORT_GTE_CMD_RTPT 0x4A280030U
#define PORT_GTE_CMD_RTV0 0x4A486012U     /* rotation * V0 */
#define PORT_GTE_CMD_RTV0TR 0x4A480012U   /* ... + translation */
#define PORT_GTE_CMD_RTIR 0x4A49E012U     /* rotation * IR */
#define PORT_GTE_CMD_RTIR_SF0 0x4A41E012U /* ... with the fraction kept */
#define PORT_GTE_CMD_LCIRBK 0x4A4DA412U   /* light colour * IR + background */
#define PORT_GTE_CMD_SQR0 0x4AA00428U     /* squares, the fraction kept */
#define PORT_GTE_CMD_SQR12 0x4AA80428U
#define PORT_GTE_CMD_OP0 0x4B70000CU /* outer product, fraction kept */
#define PORT_GTE_CMD_OP12 0x4B78000CU
#define PORT_GTE_CMD_GPF0 0x4B90003DU /* IR * IR0, the fraction kept */
#define PORT_GTE_CMD_GPF12 0x4B98003DU
#define PORT_GTE_CMD_GPL0 0x4BA0003EU /* MAC + IR * IR0, fraction kept */
#define PORT_GTE_CMD_GPL12 0x4BA8003EU
#define PORT_GTE_CMD_AVSZ3 0x4B58002DU
#define PORT_GTE_CMD_AVSZ4 0x4B68002EU
#define PORT_GTE_CMD_DPCL 0x4A680029U
#define PORT_GTE_CMD_DPCT 0x4AF8002AU
#define PORT_GTE_CMD_INTPL 0x4A980011U
#define PORT_GTE_CMD_NCLIP 0x4B400006U /* which way a triangle faces */
#define PORT_GTE_CMD_NCS 0x4AC8041EU   /* a normal lit */
#define PORT_GTE_CMD_NCT 0x4AD80420U   /* three normals lit */
#define PORT_GTE_CMD_NCCS 0x4B08041BU  /* a normal lit and coloured */

/* ---- what Sony's macros are made of in the port ---- */

/* tools/port_gte_macros.py writes each macro of Sony's inline_c.h and
 * inline_o.h again as C: its instructions on the functions above, and on
 * these. A general register of a macro: it may hold an address, so it is
 * as wide as one. */
typedef __UINTPTR_TYPE__ PortWord;

/* What a load or a store of a macro reaches, at an address in a
 * PortWord. */
#define PORT_GTE_U32_AT(address) (*(u32 *)(address))
#define PORT_GTE_U16_AT(address) (*(u16 *)(address))
#define PORT_GTE_S16_AT(address) (*(s16 *)(address))
#define PORT_GTE_U8_AT(address) (*(u8 *)(address))

#endif /* PORT_GTE_H */
