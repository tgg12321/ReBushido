/* Link order of MOVOVL.EXE. Hand-maintained: every object is listed once per
 * section, in the order the original was linked. build/src/ is the game's
 * code, build/sdk/ the Sony library modules built from the main build's
 * source (../src/main/psxsdk/), build/asm/ the library modules only this
 * program has, which are assembly, and build/asm/data/ their data. */
SECTIONS
{
    _gp = 0x801F65D0;

    .header 0 : AT(0) SUBALIGN(2)
    {
        build/asm/header.o(.data);
    }

    .movovl 0x801D8800 : AT(0x800) SUBALIGN(4)
    {
        FILL(0x00000000);
        build/src/main.o(.rodata);
        build/src/movie.o(.rodata);
        build/sdk/libc2/prnt.o(.rodata);
        build/sdk/libpress/libpress.o(.rodata);
        build/sdk/libgpu/prim.o(.rodata);
        build/sdk/libgpu/sys.o(.rodata);
        build/sdk/libcd/event.o(.rodata);
        build/sdk/libcd/sys.o(.rodata);
        build/sdk/libcd/bios.o(.rodata);
        build/sdk/libc2/puts.o(.rodata);
        build/sdk/libcd/cdread.o(.rodata);
        build/sdk/libcd/c_011.o(.rodata);
        build/sdk/libetc/vsync.o(.rodata);
        build/sdk/libetc/intr.o(.rodata);
        build/sdk/libetc/intr_dma.o(.rodata);

        build/src/main.o(.text);
        build/src/movie.o(.text);
        build/asm/start.o(.text);
        build/sdk/libc2/printf.o(.text);
        build/sdk/libc2/prnt.o(.text);
        build/sdk/libc2/ctype.o(.text);
        build/sdk/libc2/memchr.o(.text);
        build/sdk/libc2/putchar.o(.text);
        build/sdk/libapi/a53.o(.text);
        build/sdk/libc2/strlen.o(.text);
        build/sdk/libpress/libpress.o(.text);
        build/asm/libpress_vlc.o(.text);
        build/sdk/libgpu/ext.o(.text);
        build/sdk/libgpu/prim.o(.text);
        build/sdk/libgpu/sys.o(.text);
        build/sdk/libapi/c73.o(.text);
        build/sdk/libc2/memcpy.o(.text);
        build/sdk/libcd/cdrom.o(.text);
        build/sdk/libcd/event.o(.text);
        build/sdk/libapi/a07.o(.text);
        build/sdk/libcd/sys.o(.text);
        build/sdk/libcd/bios.o(.text);
        build/sdk/libc2/puts.o(.text);
        build/sdk/libcd/cdread.o(.text);
        build/sdk/libcd/cdread2.o(.text);
        build/sdk/libcd/c_002.o(.text);
        build/sdk/libcd/c_003.o(.text);
        build/sdk/libapi/a36.o(.text);
        build/sdk/libapi/a37.o(.text);
        build/sdk/libcd/c_004.o(.text);
        build/sdk/libcd/c_005.o(.text);
        build/sdk/libcd/c_007.o(.text);
        build/sdk/libcd/c_008.o(.text);
        build/sdk/libcd/c_009.o(.text);
        build/sdk/libcd/c_010.o(.text);
        build/sdk/libcd/c_011.o(.text);
        build/sdk/libetc/pad.o(.text);
        build/asm/libapi_a22.o(.text);
        build/sdk/libapi/a91.o(.text);
        build/sdk/libapi/pad.o(.text);
        build/sdk/libapi/a18.o(.text);
        build/sdk/libapi/a19.o(.text);
        build/sdk/libapi/a20.o(.text);
        build/sdk/libapi/a21.o(.text);
        build/sdk/libapi/l02.o(.text);
        build/sdk/libapi/l03.o(.text);
        build/sdk/libapi/patch.o(.text);
        build/sdk/libapi/c68.o(.text);
        build/sdk/libapi/sendpad.o(.text);
        build/sdk/libapi/chclrpad.o(.text);
        build/sdk/libetc/vsync.o(.text);
        build/sdk/libapi/l10.o(.text);
        build/sdk/libetc/intr.o(.text);
        build/sdk/libapi/c114.o(.text);
        build/sdk/libapi/a23.o(.text);
        build/sdk/libapi/a24.o(.text);
        build/sdk/libapi/a25.o(.text);
        build/sdk/libc2/setjmp.o(.text);
        build/sdk/libetc/intr_vb.o(.text);
        build/sdk/libetc/intr_dma.o(.text);
        build/sdk/libetc/vmode.o(.text);
        build/sdk/libds/dscb.o(.text);

        build/sdk/libc2/ctype.o(.data);
        build/sdk/libpress/libpress.o(.data);
        build/asm/data/libpress_vlc.data.o(.data);
        build/sdk/libgpu/sys.o(.data);
        build/sdk/libcd/cdrom.o(.data);
        build/sdk/libcd/sys.o(.data);
        build/sdk/libcd/bios.o(.data);
        build/sdk/libc2/puts.o(.data);
        build/sdk/libcd/cdread.o(.data);
        build/sdk/libcd/c_002.o(.data);
        build/sdk/libcd/c_003.o(.data);
        build/sdk/libcd/c_004.o(.data);
        build/sdk/libcd/c_005.o(.data);
        build/sdk/libcd/c_007.o(.data);
        build/sdk/libcd/c_008.o(.data);
        build/sdk/libcd/c_009.o(.data);
        build/sdk/libcd/c_010.o(.data);
        build/sdk/libcd/c_011.o(.data);
        build/sdk/libapi/pad.o(.data);
        build/sdk/libetc/vsync.o(.data);
        build/sdk/libetc/intr.o(.data);
        build/sdk/libetc/intr_vb.o(.data);
        build/sdk/libetc/intr_dma.o(.data);
        build/sdk/libetc/vmode.o(.data);
        build/src/main.o(.sdata);
        build/src/movie.o(.sdata);
        /* From here to the end of the file is zeroes: variables without an
         * initial value. A module built from the main build's source has
         * each of its variables in a section of its own, placed here; a
         * variable of a module that is still assembly is only a name for
         * its address.
         *
         * First the storage of the libraries' assembly modules: sixteen
         * bytes each, of which a module's code names these (the main
         * program has the same three with the same spaces). */
        build/sdk/libapi/patch.o(.gnu.linkonce.b._patch_pad_ra);
        . += 0x4;
        build/sdk/libapi/patch.o(.gnu.linkonce.b._patch_pad_enable);
        build/sdk/libapi/patch.o(.gnu.linkonce.b._patch_pad_disable);
        build/sdk/libapi/sendpad.o(.gnu.linkonce.b._send_pad_ra);
        build/sdk/libapi/sendpad.o(.gnu.linkonce.b._send_pad_entry);
        . += 0x8;
        build/sdk/libapi/chclrpad.o(.gnu.linkonce.b._remove_ChgclrPAD_ra);
        . += 0xC;
        /* The game code's small variables. */
        build/src/main.o(.sbss);
        build/src/movie.o(.sbss);
        . = ALIGN(., 16);
        /* The libraries' variables, up to the 2048-byte boundary the file
         * ends on. First each module's own, module by module: a module
         * starts on an 8-byte boundary, as in the main program. */
        build/sdk/libc2/putchar.o(.bss);
        . = ALIGN(8);
        build/sdk/libgpu/sys.o(.bss);
        . = ALIGN(8);
        build/sdk/libcd/bios.o(.bss);
        . = ALIGN(8);
        build/sdk/libcd/c_004.o(.bss);
        build/sdk/libcd/c_011.o(.bss);
        . = ALIGN(8);
        build/sdk/libetc/pad.o(.bss);
        . = ALIGN(8);
        build/sdk/libapi/pad.o(.bss);
        /* Then the variables that are not `static`, in the linker's
         * order: the sum of the name's characters and its length, modulo
         * 512 (docs/FAKES.md, "The zeroed variables" and "Names against outside evidence"). It is one walk,
         * which the end of the file happens to cut in two. Sony's names
         * rise through it without an exception, and the game's two movie
         * variables lie inside it, so they are not `static` either. */
        build/sdk/libcd/c_002.o(.gnu.linkonce.b.Stframe_no);
        build/sdk/libcd/c_002.o(.gnu.linkonce.b.Stsector_offset);
        build/sdk/libcd/c_005.o(.gnu.linkonce.b.StRgb24);
        build/sdk/libcd/cdread2.o(.gnu.linkonce.b.StMode);
        build/sdk/libcd/c_005.o(.gnu.linkonce.b.st_frame_callback);
        build/sdk/libcd/c_005.o(.gnu.linkonce.b.StFunc2);
        build/sdk/libcd/c_002.o(.gnu.linkonce.b.StCdIntrFlag);
        build/sdk/libcd/c_005.o(.gnu.linkonce.b.StCHANNEL);
        build/sdk/libcd/c_010.o(.gnu.linkonce.b.StStartFrame);
        build/sdk/libcd/c_005.o(.gnu.linkonce.b.CChannel);
        build/sdk/libetc/pad.o(.gnu.linkonce.b.PadIdentifier);
        build/sdk/libcd/c_002.o(.gnu.linkonce.b.StFinalSector);
    }

    .bss 0x801F6800 (NOLOAD) : SUBALIGN(4)
    {
        build/src/movie.o(.gnu.linkonce.b.g_dec_storage);
        build/src/movie.o(.gnu.linkonce.b.g_movie_file);
        /* The rest of the walk, past the end of the file. */
        build/sdk/libds/dscb.o(.gnu.linkonce.b.ds_callbacks);
        build/sdk/libcd/c_011.o(.gnu.linkonce.b.StEmu_Idx);
        . = ALIGN(8); /* a variable of eight bytes or more is on eight */
        build/sdk/libgpu/sys.o(.gnu.linkonce.b._que);
        build/sdk/libcd/c_002.o(.gnu.linkonce.b.StRingIdx1);
        build/sdk/libcd/c_002.o(.gnu.linkonce.b.StRingIdx2);
        build/sdk/libcd/c_002.o(.gnu.linkonce.b.st_read_slot);
        build/sdk/libcd/c_005.o(.gnu.linkonce.b.StEmu_Addr);
        build/sdk/libcd/c_010.o(.gnu.linkonce.b.StEndFrame);
        build/sdk/libcd/c_010.o(.gnu.linkonce.b.StSTART_FLAG);
        build/sdk/libcd/c_011.o(.gnu.linkonce.b.StRingBase);
        build/sdk/libcd/cdrom.o(.gnu.linkonce.b.StRingAddr);
        build/sdk/libcd/cdrom.o(.gnu.linkonce.b.StRingSize);
    }

    /DISCARD/ :
    {
        *(*);
    }
}
