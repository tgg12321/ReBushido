/* How the probe is linked (tests/play/README.md, "The GTE probe"): the
 * game's PS-X EXE header, whose entry, address and size
 * tools/make_psexe.py fills in, and then everything else in one piece
 * where a PlayStation program loads. */
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
        *(.sdata*);
        *(.sbss*);
        *(.scommon*);
        *(.bss*);
        *(COMMON);
        *(.gnu.linkonce.*);
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
