/* How the probe is linked (tests/play/README.md, "The GTE probe"): the
 * game's PS-X EXE header, whose entry, address and size
 * tools/make_psexe.py fills in, and then everything else in one piece
 * where a PlayStation program loads. */
/* Two places on the scratchpad that the renderer's listings name. */
D_1F800020 = 0x1F800020;
D_1F800110 = 0x1F800110;

SECTIONS
{
    .header 0 : AT(0) SUBALIGN(2)
    {
        *header.o(.data);
    }

    .main 0x80010000 : AT(0x800) SUBALIGN(4)
    {
        *(.text*);
        *(.rodata*);
        *(.data*);
        /* The game's C that is linked in (render.c) reaches the small
         * variables it defines through the global pointer: they are
         * together here, where it points. */
        _gp = .;
        *(.sdata*);
        *(.sbss*);
        *(.scommon*);
        *(.gnu.linkonce.*);
        *(.bss*);
        *(COMMON);
    }

    /DISCARD/ :
    {
        *(.reginfo);
        *(.MIPS.abiflags);
        *(.pdr);
        *(.mdebug*);
        *(.comment);
        *(.gnu.attributes);
    }
}
