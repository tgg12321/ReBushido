/* Sound in the native port (docs/PORT.md, "Sound"): what joins Sony's
 * sound libraries to the port's sound chip (spu.c) and to its clock.
 *
 * Sony's libsnd and libspu are compiled into the port as they are: they
 * are plain C that keeps tables and writes the chip's registers. The
 * registers are memory at the console's own address, which this file
 * asks the system for, so that code runs unchanged (libsnd's start-up
 * writes them by that address). One module of libspu is not compiled,
 * the one that talks to the chip itself (SPU: the reset, and transfers
 * to and from sound memory by the DMA controller); what it defines is
 * defined here, on the port's chip:
 *
 *   - the reset sets the registers as Sony's does, without its waits;
 *   - a transfer is done when it is asked for, whole, and the interrupt
 *     that ends one on the console is raised at once: the game waits
 *     for it in a loop with no frame between;
 *   - the interrupts Sony's libraries ask for (InterruptCallback,
 *     VSyncCallback, DMACallback) are kept here. The only one the game
 *     uses is the vertical blank's, on which the sound library's tick
 *     runs: it runs at each blank of the port's clock.
 *
 * At each vertical blank (port_RunSoundBlank, from clock.c) the chip
 * makes the samples up to the blank, the tick runs, which is where the
 * library writes the keys and the voices' registers, and the chip makes
 * a few samples more: the game's code, which in the port takes no time,
 * runs on the console in the first moments after the blank, and asks
 * the chip there which voices are still sounding (BLANK_HEAD_FRAMES).
 * A blank is 263 scan lines of an NTSC console, 15,734.2657 of them a second
 * (4.5 MHz / 286), and the chip makes 44,100 samples a second: 737 and
 * 341/2500 samples a blank. The fraction is carried, so a blank is 737
 * samples or 738 and a run's sound is the same every time. */
#include "common.h"
#include "port_host.h"
#include "port_music.h"
#include "port_spu.h"
#include "src/main/psxsdk/libspu/libspu_internal.h"
/* Sony's two headers declare one function twice between them. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#include "src/main/psxsdk/libsnd/libsnd_i.h"
#pragma GCC diagnostic pop
#include "bb2.h"

/* The console's page of hardware registers, of which the chip's are a
 * part. */
#define IO_PAGE_ADDR 0x1F801000U
#define IO_PAGE_BYTES 0x1000U

/* Samples a blank: a whole number and a fraction (the top of the
 * file). */
#define BLANK_FRAMES 737
#define BLANK_FRACTION 341
#define BLANK_FRACTION_ONE 2500
/* Of a blank's samples, those made after the tick and before the game's
 * code goes on: the game asks which voices are free at the top of its
 * frame (snd_FlushQueue, main.c), a fraction of a millisecond after the
 * blank on the console, when a voice the tick has just keyed on is
 * sounding already and the blank that follows has not yet been played.
 * The number is the port's: a voice is sounding after one sample, and
 * the console's own delay is some tens. With the tick before all of the
 * blank's samples, a sound as long as the time between two of the same
 * (a footstep) had always just ended when the next was asked for, and
 * took its voice, where the console gives the next another voice: the
 * emulator said so (tests/play/port_sound.toml). */
#define BLANK_HEAD_FRAMES 32

/* The DMA controller moves 64 bytes at a time: a transfer is rounded up
 * to that. */
#define DMA_BLOCK_BYTES 64
#define DMA_CHANNELS 7
#define DMA_SPU 4
#define INTERRUPTS 11
#define INTERRUPT_BLANK 0

/* The control register's transfer modes, as Sony's writes them. */
#define TRANSFER_BY_HAND 0x10
#define TRANSFER_WRITE 0x20
#define TRANSFER_READ 0x30

/* The steps of a transfer that Sony's _spu_t takes. */
enum {
    STEP_READ_MODE = 0,
    STEP_WRITE_MODE = 1,
    STEP_SET_ADDRESS = 2,
    STEP_START = 3,
};

/* Where the reset puts its silent block, in units of 8 bytes: the first
 * place in sound memory that is free. */
#define SILENT_BLOCK_ADDR 0x200
/* What the reset gives every voice and the control register. */
#define RESET_PITCH 0x3FFF
#define RESET_ALL_VOICES_LOW 0xFFFF
#define RESET_ALL_VOICES_HIGH 0xFF
#define RESET_CONTROL 0xC000
#define RESET_DATA_TRANSFER 4
/* Sound memory is addressed in units of 8 bytes: the shift, and the mask
 * of what is less than one. */
#define RESET_ADDRESS_SHIFT 3
#define RESET_ADDRESS_MASK 7
/* What Sony's libcd puts in the chip's volumes and its control register
 * as the drive's library starts (CD_initvol): half of full scale, and the
 * chip on and unmuted with the disc's input taken (bit 0). */
#define SPU_CD_INIT_VOLUME 0x3FFF
#define SPU_CD_INIT_CONTROL 0xC001
/* The keys and switches Sony's library can hold back for an interrupt
 * to write (it never does here). */
#define REQUESTS 10
/* A voice the library plays for the game itself and not for a
 * sequence. */
#define VOICES_LOW 16

/* ---- what Sony's SPU module defines ---- */

SpuUnion *_spu_RXX = (SpuUnion *)PORT_SPU_REGS_ADDR;
u16 _spu_tsa = 0;
s32 _spu_transMode = 0;
s32 _spu_mem_mode_plus = RESET_ADDRESS_SHIFT;
s32 _spu_mem_mode_unitM = RESET_ADDRESS_MASK;
s32 _spu_inTransfer = 1;
void (* volatile _spu_transferCallback)() = 0;
s32 _spu_IRQCallback = 0;
volatile u16 _spu_RQ[REQUESTS];

/* One block of a waveform, silent and marked as looping on itself:
 * every voice plays it until it is given something else (Sony's). */
static u8 g_port_spu_silent_block[16] = {
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7,
};
/* 1 if the transfer being set up reads sound memory. */
static s32 g_port_spu_reading;

/* ---- the port's own ---- */

static s32 g_port_spu_mapped;
static void (*g_port_interrupts[INTERRUPTS])();
static void (*g_port_dma_callbacks[DMA_CHANNELS])();
static void (*g_port_vsync_callback)();
static u32 g_port_sample_count;
static u32 g_port_blank_fraction;
static u32 g_port_sound_blank;
static const char *g_port_spu_dump_path;

u32 port_GetSampleCount(void) { return g_port_sample_count; }

/* A line of a run's log for each voice the chip starts. */
static void port_LogKeyOn(s32 voice, u32 addr, u32 pitch) {
    port_Print("sound key %u voice %d addr 0x%05X pitch 0x%04X\n",
               g_port_sound_blank, voice, addr, pitch);
}

/* The chip's registers, where the console has them, and the chip. */
static void port_MapSpu(void) {
    if (g_port_spu_mapped) {
        return;
    }
    if (!port_HostMapAt(IO_PAGE_ADDR, IO_PAGE_BYTES)) {
        port_Fail("rebushido: the system gave no memory at 0x%08X (%u bytes): "
                  "the sound chip's registers, which Sony's library reaches "
                  "by address\n",
                  IO_PAGE_ADDR, IO_PAGE_BYTES);
    }
    port_AttachSpu((u16 *)PORT_SPU_REGS_ADDR);
    g_port_spu_mapped = 1;
}

/* What Sony's libcd does to the sound chip as the drive's library starts
 * (CD_initvol, src/main/psxsdk/libcd/bios.c, which CdInit reaches through
 * CdReset(1)): the disc's sound at half of full scale and the chip's CD
 * input on, and the main volume at half of full scale as well unless
 * something is in the external input. The port's libcd is its own
 * (src/port/cd.c) and this is the part of Sony's that it has to do, or
 * the chip multiplies the disc's sound by 0 (port_spu.h). */
void port_InitCdSpuVolume(void) {
    port_MapSpu();
    if (_spu_RXX->rxx.ex_vol.left == 0 && _spu_RXX->rxx.ex_vol.right == 0) {
        _spu_RXX->rxx.main_vol.left = SPU_CD_INIT_VOLUME;
        _spu_RXX->rxx.main_vol.right = SPU_CD_INIT_VOLUME;
    }
    _spu_RXX->rxx.cd_vol.left = SPU_CD_INIT_VOLUME;
    _spu_RXX->rxx.cd_vol.right = SPU_CD_INIT_VOLUME;
    _spu_RXX->rxx.spucnt = SPU_CD_INIT_CONTROL;
}

/* ---- interrupts ---- */

void *InterruptCallback(s32 irq, void (*func)()) {
    void (*before)();

    if (irq < 0 || irq >= INTERRUPTS) {
        return NULL;
    }
    before = g_port_interrupts[irq];
    g_port_interrupts[irq] = func;
    return (void *)before;
}

void *DMACallback(s32 dma, void (*func)()) {
    void (*before)();

    if (dma < 0 || dma >= DMA_CHANNELS) {
        return NULL;
    }
    before = g_port_dma_callbacks[dma];
    g_port_dma_callbacks[dma] = func;
    return (void *)before;
}

s32 VSyncCallback(void (*func)()) {
    g_port_vsync_callback = func;
    return 0;
}

/* The BIOS's: here it is the SPU's transfer that delivers. */
void DeliverEvent(u32 desc, u32 spec) { port_DeliverEvent(desc, (s32)spec); }

/* The BIOS's waits until the event has come. Here what delivers it has
 * always run already; if it has not, waiting would never end. */
s32 WaitEvent(s32 event) {
    if (!TestEvent(event)) {
        PORT_STUB("WaitEvent (an event that had not come)");
        return 0;
    }
    return 1;
}

/* ---- transfers ---- */

/* `size` bytes between the game's memory and sound memory at the
 * transfer address, rounded up to the DMA's blocks as on the console
 * (so up to 63 bytes past `size` are moved too). Then the interrupt
 * that ends a transfer. */
static void port_TransferSpu(u8 *memory, u32 size, s32 reading) {
    u8 *ram = port_GetSpuRam();
    u32 addr = (u32)_spu_tsa << _spu_mem_mode_plus;
    u32 bytes = (size / DMA_BLOCK_BYTES + (size % DMA_BLOCK_BYTES ? 1 : 0)) *
                DMA_BLOCK_BYTES;
    u32 i;

    for (i = 0; i < bytes; i++) {
        u32 place = (addr + i) & (PORT_SPU_RAM_BYTES - 1);

        if (reading) {
            memory[i] = ram[place];
        } else {
            ram[place] = memory[i];
        }
    }
    if (g_port_dma_callbacks[DMA_SPU] != NULL) {
        g_port_dma_callbacks[DMA_SPU]();
    }
}

/* What the end of a transfer runs on the console (Sony's hangs it on
 * the DMA's interrupt): the chip leaves its transfer mode, and the
 * program is told, by its callback or by an event. */
void _spu_FiDMA(void) {
    _spu_RXX->rxx.spucnt = _spu_RXX->rxx.spucnt & ~PORT_SPU_CONTROL_TRANSFER;
    if (_spu_transferCallback) {
        _spu_transferCallback();
        return;
    }
    DeliverEvent(HwSPU, EvSpCOMP);
}

/* Writes to sound memory by hand, where Sony's feeds the chip 64 bytes
 * at a time. */
static void _spu_FwriteByIO(u8 *addr, u32 size) {
    u8 *ram = port_GetSpuRam();
    u32 at = (u32)_spu_tsa << _spu_mem_mode_plus;
    u32 i;

    _spu_RXX->rxx.trans_addr = _spu_tsa;
    for (i = 0; i < size; i++) {
        ram[(at + i) & (PORT_SPU_RAM_BYTES - 1)] = addr[i];
    }
}

/* The steps of a transfer, as Sony's takes them (the enum above). */
s32 _spu_t(s32 mode, ...) {
    PortArgs args;
    u16 control;

    PORT_ARGS_START(args, mode);
    switch (mode) {
    case STEP_SET_ADDRESS:
        _spu_tsa = (u16)(PORT_ARG(args, u32) >> _spu_mem_mode_plus);
        _spu_RXX->rxx.trans_addr = _spu_tsa;
        break;
    case STEP_WRITE_MODE:
        g_port_spu_reading = 0;
        control = _spu_RXX->rxx.spucnt;
        control &= ~PORT_SPU_CONTROL_TRANSFER;
        control |= TRANSFER_WRITE;
        _spu_RXX->rxx.spucnt = control;
        break;
    case STEP_READ_MODE:
        g_port_spu_reading = 1;
        control = _spu_RXX->rxx.spucnt;
        control &= ~PORT_SPU_CONTROL_TRANSFER;
        control |= TRANSFER_READ;
        _spu_RXX->rxx.spucnt = control;
        break;
    case STEP_START: {
        u8 *memory = (u8 *)ADDR(PORT_ARG(args, u32));
        u32 size = PORT_ARG(args, u32);

        port_TransferSpu(memory, size, g_port_spu_reading);
        break;
    }
    default:
        break;
    }
    PORT_ARGS_END(args);
    return 0;
}

s32 _spu_Fw(s32 addr, s32 size) {
    if (_spu_transMode == 0) {
        _spu_t(STEP_SET_ADDRESS, _spu_tsa << _spu_mem_mode_plus);
        _spu_t(STEP_WRITE_MODE);
        _spu_t(STEP_START, addr, size);
    } else {
        _spu_FwriteByIO((u8 *)ADDR(addr), (u32)size);
    }
    return size;
}

s32 _spu_Fr(s32 addr, s32 size) {
    _spu_t(STEP_SET_ADDRESS, _spu_tsa << _spu_mem_mode_plus);
    _spu_t(STEP_READ_MODE);
    _spu_t(STEP_START, addr, size);
    return size;
}

void _spu_FsetRXX(s32 index, u32 value, s32 is_address) {
    if (is_address == 0) {
        _spu_RXX->raw[index] = (u16)value;
        return;
    }
    _spu_RXX->raw[index] = (u16)(value >> _spu_mem_mode_plus);
}

/* An address into a register, in the chip's units of 8 bytes, rounded
 * up to one (Sony's; -1 and -2 only work it out). */
s32 _spu_FsetRXXa(s32 index, s32 value) {
    u32 unit = (u32)_spu_mem_mode_unitM + 1;
    s32 aligned;

    if ((u32)value % unit != 0) {
        value += (s32)unit;
        value &= ~_spu_mem_mode_unitM;
    }
    aligned = (s32)((u32)value >> _spu_mem_mode_plus);
    switch (index) {
    case -1:
        return aligned & 0xFFFF;
    case -2:
        return value;
    default:
        _spu_RXX->raw[index] = (u16)aligned;
        return value;
    }
}

/* The reset, as Sony's does it, less its waits for the chip: the chip
 * off, no volume, every voice on the silent block, keyed on and off
 * again, the chip on. */
s32 _spu_init(s32 mode) {
    s32 voice;
    s32 i;

    port_MapSpu();
    _spu_transMode = 0;
    _spu_tsa = 0;
    _spu_RXX->rxx.main_vol.left = 0;
    _spu_RXX->rxx.main_vol.right = 0;
    _spu_RXX->rxx.spucnt = 0;
    _spu_mem_mode_plus = RESET_ADDRESS_SHIFT;
    _spu_mem_mode_unitM = RESET_ADDRESS_MASK;
    _spu_RXX->rxx.data_trans = RESET_DATA_TRANSFER;
    _spu_RXX->rxx.rev_vol.left = 0;
    _spu_RXX->rxx.rev_vol.right = 0;
    _spu_RXX->rxx.key_off[0] = RESET_ALL_VOICES_LOW;
    _spu_RXX->rxx.key_off[1] = RESET_ALL_VOICES_LOW;
    port_TakeSpuKeys();
    _spu_RXX->rxx.rev_mode[0] = 0;
    _spu_RXX->rxx.rev_mode[1] = 0;
    for (i = 0; i < REQUESTS; i++) {
        _spu_RQ[i] = 0;
    }
    if (mode == 0) {
        _spu_tsa = SILENT_BLOCK_ADDR;
        _spu_RXX->rxx.chan_fm[0] = 0;
        _spu_RXX->rxx.chan_fm[1] = 0;
        _spu_RXX->rxx.noise_mode[0] = 0;
        _spu_RXX->rxx.noise_mode[1] = 0;
        _spu_RXX->rxx.cd_vol.left = 0;
        _spu_RXX->rxx.cd_vol.right = 0;
        _spu_RXX->rxx.ex_vol.left = 0;
        _spu_RXX->rxx.ex_vol.right = 0;
        _spu_FwriteByIO(
            g_port_spu_silent_block, sizeof(g_port_spu_silent_block));
        for (voice = 0; voice < PORT_SPU_VOICES; voice++) {
            _spu_RXX->rxx.voice[voice].volume.left = 0;
            _spu_RXX->rxx.voice[voice].volume.right = 0;
            _spu_RXX->rxx.voice[voice].pitch = RESET_PITCH;
            _spu_RXX->rxx.voice[voice].addr = SILENT_BLOCK_ADDR;
            _spu_RXX->rxx.voice[voice].adsr[0] = 0;
            _spu_RXX->rxx.voice[voice].adsr[1] = 0;
        }
        /* the chip takes each of the two before the next is written:
         * Sony's waits between them */
        _spu_RXX->rxx.key_on[0] = RESET_ALL_VOICES_LOW;
        _spu_RXX->rxx.key_on[1] = RESET_ALL_VOICES_HIGH;
        port_TakeSpuKeys();
        _spu_RXX->rxx.key_off[0] = RESET_ALL_VOICES_LOW;
        _spu_RXX->rxx.key_off[1] = RESET_ALL_VOICES_HIGH;
        port_TakeSpuKeys();
    }
    _spu_inTransfer = 1;
    _spu_RXX->rxx.spucnt = RESET_CONTROL;
    _spu_transferCallback = 0;
    _spu_IRQCallback = 0;
    return 0;
}

/* ---- a vertical blank ---- */

/* A line of a run's log for each voice the library is about to key on:
 * what the game asked for since the last tick (SsUtKeyOnV), by the
 * library's own record of the voice. */
static void port_LogAsked(void) {
    u32 asked = _svm_okon1 | ((u32)(_svm_okon2 & 0xFF) << VOICES_LOW);
    s32 voice;

    for (voice = 0; voice < PORT_SPU_VOICES; voice++) {
        if ((asked >> voice) & 1) {
            port_Print("sound ask %u voice %d vab %d program %d tone %d "
                       "note %d\n",
                       g_port_sound_blank, voice, _svm_voice[voice].vabId,
                       _svm_voice[voice].prog, _svm_voice[voice].tone,
                       _svm_voice[voice].note);
        }
    }
}

/* The window's sound card, and what it is given of a step run again
 * (port_spu.h): nothing in the build without one. */
void (*g_port_sound_hook)(const s16 *samples, u32 frames);
void (*g_port_rerun_sound_hook)(const s16 *samples, u32 frames);

/* The next `frames` samples: the drive goes on, the disc's sound and
 * the chip's are made, and kept if the run keeps its sound or a sound
 * card is playing it. */
static void port_MixSound(u32 frames) {
    static s16 cd[(BLANK_FRAMES + 1) * 2];
    static s16 out[(BLANK_FRAMES + 1) * 2];

    s16 *heard = NULL;
    const s16 *music;

    port_PlayDisc(frames);
    port_ReadCdAudio(cd, frames);
    /* a mod's music is heard in place of the disc's (port_music.h), in
     * samples of its own: `out` is the game's state, and stays what the
     * chip made */
    music = port_PlayMusic(frames, &heard);
    port_RunSpu(cd, music, out, heard, frames);
    g_port_sample_count += frames;
    /* a step run again is mixed and not heard (port_platform.h), but for
     * what the window takes of it in place of what it queued and has
     * not played yet */
    if (g_port_rerun) {
        if (g_port_rerun_sound_hook != NULL) {
            g_port_rerun_sound_hook(out, frames);
        }
        return;
    }
    if (music == NULL) {
        heard = out;
    }
    if (port_IsWavOn()) {
        port_AppendWav(heard, frames);
    }
    if (g_port_sound_hook != NULL) {
        g_port_sound_hook(heard, frames);
    }
}

/* A vertical blank (port_spu.h). */
void port_RunSoundBlank(u32 blank) {
    u32 frames = BLANK_FRAMES;

    g_port_sound_blank = blank;
    g_port_blank_fraction += BLANK_FRACTION;
    if (g_port_blank_fraction >= BLANK_FRACTION_ONE) {
        g_port_blank_fraction -= BLANK_FRACTION_ONE;
        frames++;
    }
    port_MixSound(frames - BLANK_HEAD_FRAMES);
    if (port_IsWavOn()) {
        g_port_spu_key_on_hook = port_LogKeyOn;
        port_LogAsked();
    }
    if (g_port_interrupts[INTERRUPT_BLANK] != NULL) {
        g_port_interrupts[INTERRUPT_BLANK]();
    }
    if (g_port_vsync_callback != NULL) {
        g_port_vsync_callback();
    }
    port_MixSound(BLANK_HEAD_FRAMES);
}

/* ---- the end of a run ---- */

void port_PrintSoundSummary(void) {
    const PortSpuCounts *counts = port_GetSpuCounts();
    s32 voice;

    port_Print("sound samples %u\n", counts->samples);
    port_Print("sound keys_on %u keys_off %u ended %u\n", counts->key_ons,
               counts->key_offs, counts->ends);
    port_Print("sound noise_samples %u modulated_samples %u swept_samples %u\n",
               counts->noise_samples, counts->modulated, counts->swept);
    port_Print("sound reverb_samples %u clipped_samples %u\n",
               counts->reverb_samples, counts->clipped);
    port_Print("sound xa_sectors %u\n", port_GetXaSectorCount());
    if (!g_port_spu_mapped) {
        return;
    }
    for (voice = 0; voice < PORT_SPU_VOICES; voice++) {
        u32 level = _spu_RXX->rxx.voice[voice].volumex;

        if (level != 0) {
            port_Print(
                "sound still_sounding voice %d level 0x%04X\n", voice, level);
        }
    }
}

void port_SetSpuDump(const char *path) { g_port_spu_dump_path = path; }

/* Sound memory, then the registers, to a file that is open
 * (port_spu.h). */
s32 port_WriteSpu(s32 file) {
    static const u16 no_registers[PORT_SPU_REG_COUNT];
    const void *registers =
        g_port_spu_mapped ? (const void *)PORT_SPU_REGS_ADDR : no_registers;

    return port_HostAppend(file, port_GetSpuRam(), PORT_SPU_RAM_BYTES) &&
           port_HostAppend(file, registers, PORT_SPU_REG_COUNT * sizeof(u16));
}

/* The same when the run ends, to the file --spu named. */
void port_WriteSpuDump(void) {
    s32 file;

    if (g_port_spu_dump_path == NULL) {
        return;
    }
    file = port_HostCreate(g_port_spu_dump_path);
    if (file < 0 || !port_WriteSpu(file)) {
        port_Log("rebushido: %s cannot be written\n", g_port_spu_dump_path);
    }
    if (file >= 0) {
        port_HostClose(file);
    }
}
