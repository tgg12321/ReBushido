#ifndef PSX_H
#define PSX_H

/* The console's hardware, where Sony's headers have no name for a value:
 * the words of the GPU's two ports, the ordering table's tags, the DMA
 * channels' control words and two of the CD controller's bits. Sony's library
 * code (src/main/psxsdk/) is written with these; the game's own code has no
 * use for them. */

/* ---- the GPU's commands (GP0, GP1) ---- */

/* GP0 Commands (sent to GPU data port) */
/* GP0(A0h) - Copy rectangle CPU -> VRAM */
#define GP0_COPY_RECT_C2V 0xA0000000

/* GP1 Commands (sent to GPU status/control port) */
/* GP1(04h) - DMA direction / data request */
#define GP1_DMA_DIR 0x04000000
/* GP1(04h) - DMA direction: FIFO (linked list) */
#define GP1_DMA_DIR_FIFO 0x04000002
#define GP1_GPU_INFO 0x10000000 /* GP1(10h) - Get GPU info */

/* Ordering Table constants */
#define OT_TERMINATOR 0x01000000 /* OT chain terminator */
#define OT_ADDR_MASK 0x00FFFFFF  /* OT address (24-bit) mask */
#define OT_TAG_BASE 0x80000000   /* OT tag with KSEG0 high bit */

/* ---- DMA ---- */

/* GPU DMA (channel 2) */
/* GPU DMA: from RAM, linked list mode */
#define DMA_GPU_LINKED_LIST 0x01000401

/* CD-ROM DMA (channel 3) */
#define DMA_CD_TO_RAM 0x11000000 /* CD DMA: normal transfer */
/* CD DMA to RAM: start + trigger, chopping, manual sync */
#define DMA_CD_TO_RAM_CHOPPED 0x11400100
#define DMA_CD_ENABLE 0x00008000 /* CD DMA enable bit */

/* SPU DMA (channel 4) */
#define DMA_CHAN_MASK 0xF0FFFFFF    /* Mask to clear channel bits */
#define DMA_SPU_FROM_RAM 0x20000000 /* SPU DMA: RAM -> SPU */
#define DMA_SPU_TO_RAM 0x22000000   /* SPU DMA: SPU -> RAM */
#define DMA_BUSY 0x01000000         /* DMA busy/active bit */

/* CD-ROM register flags */
/* CD request register (index 0, reg 3): BFRD, want data */
#define CD_REQ_WANT_DATA 0x80
#define CD_STAT_DATA_REQ 0x40 /* CD status: data request */

#endif /* PSX_H */
