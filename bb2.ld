/*
 * The link of the main executable: every object's sections, in the order
 * the original has them. Written and maintained by hand: no tool writes
 * this file.
 */
SECTIONS
{
    __romPos = 0;
    header_ROM_START = __romPos;
    header_VRAM = ADDR(.header);
    .header : AT(header_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        header_DATA_START = .;
        build/asm/header.o(.data);
        header_DATA_END = .;
        header_DATA_SIZE = ABSOLUTE(header_DATA_END - header_DATA_START);
    }
    __romPos += SIZEOF(.header);
    header_ROM_END = __romPos;
    header_VRAM_END = .;

    /* .main is SUBALIGN(2): the objects declare 4- or 16-byte section
     * alignment, which the original layout does not follow. `. = ALIGN(4);` stands
     * where a word-aligned module section follows one ending at 2 mod 4. */
    main_ROM_START = __romPos;
    main_VRAM = ADDR(.main);
    .main 0x80010000 : AT(main_ROM_START) SUBALIGN(2)
    {
        FILL(0x00000000);
        main_RODATA_START = .;
        build/src/main/main.o(.rodata);
        build/src/main/cloth.o(.rodata);
        build/src/main/pad.o(.rodata);
        build/src/main/motion_decode.o(.rodata);
        build/src/main/camera.o(.rodata);
        build/src/main/fighter_pad.o(.rodata);
        build/src/main/fight.o(.rodata);
        build/src/main/fighter_pose.o(.rodata);
        build/src/main/fighter.o(.rodata);
        build/src/main/fighter_hit.o(.rodata);
        build/src/main/hit_test.o(.rodata);
        build/src/main/object.o(.rodata);
        build/src/main/fight_event.o(.rodata);
        build/src/main/fight_modes.o(.rodata);
        build/src/main/fight_start.o(.rodata);
        build/src/main/menu_screen.o(.rodata);
        build/src/main/cdrom.o(.rodata);
        build/src/main/memcard.o(.rodata);
        build/src/main/memcard_save.o(.rodata);
        build/src/main/replay.o(.rodata);
        build/src/main/link.o(.rodata);
        build/src/main/story.o(.rodata);
        build/src/main/fight_after.o(.rodata);
        build/src/main/font.o(.rodata);
        build/src/main/bitstream.o(.rodata);
        build/src/main/stage.o(.rodata);
        build/src/main/cloth_model.o(.rodata);
        build/src/main/model_face.o(.rodata);
        build/src/main/model.o(.rodata);
        build/src/main/light.o(.rodata);
        build/src/main/math.o(.rodata);
        build/src/main/render.o(.rodata);
        build/src/main/stage_props.o(.rodata);
        build/src/main/math_sin.o(.rodata);
        build/src/main/ndata.o(.rodata);
        build/src/main/arena.o(.rodata);
        build/src/main/resource.o(.rodata);
        build/src/main/scene_frame.o(.rodata);
        build/src/main/stage_hooks.o(.rodata);
        build/src/main/wave.o(.rodata);
        build/src/main/tex.o(.rodata);
        build/src/main/model_tiny.o(.rodata);
        build/src/main/tex_scroll.o(.rodata);
        build/src/main/resource_weapon.o(.rodata);
        build/src/main/scene.o(.rodata);
        build/src/main/render_asm.o(.rodata);
        build/src/main/math_asm.o(.rodata);
        build/src/main/collision.o(.rodata);
        build/src/main/render_skin_asm.o(.rodata);
        build/src/main/cutscene.o(.rodata);
        build/src/main/cpu.o(.rodata);
        build/src/main/snd.o(.rodata);
        build/src/main/hud.o(.rodata);
        build/src/main/eff.o(.rodata);
        build/src/main/menu_pages.o(.rodata);
        build/src/main/select.o(.rodata);
        build/src/main/select_team.o(.rodata);
        build/src/main/menu_run.o(.rodata);
        build/src/main/narration.o(.rodata);
        build/src/main/staff.o(.rodata);
        build/src/main/psxsdk/libc2/prnt.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libc2/sprintf.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libgpu/prim.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libgpu/sys.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libcd/event.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libcd/sys.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libcd/bios.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libc2/puts.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libcd/cdread.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libetc/vsync.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libetc/intr.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libetc/intr_dma.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libsnd/sstick.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libspu/spu.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libspu/s_sca.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libspu/s_sva.o(.rodata);
        . = ALIGN(4);
        build/src/main/psxsdk/libcomb/comb.o(.rodata);
        . = ALIGN(4);
        main_RODATA_END = .;
        main_RODATA_SIZE = ABSOLUTE(main_RODATA_END - main_RODATA_START);
        main_TEXT_START = .;
        build/src/main/main.o(.text);
        build/src/main/cloth.o(.text);
        build/src/main/pad.o(.text);
        build/src/main/motion_decode.o(.text);
        build/src/main/camera.o(.text);
        build/src/main/fighter_pad.o(.text);
        build/src/main/fight.o(.text);
        build/src/main/fighter_pose.o(.text);
        build/src/main/fighter.o(.text);
        build/src/main/fighter_hit.o(.text);
        build/src/main/hit_test.o(.text);
        build/src/main/object.o(.text);
        build/src/main/fight_event.o(.text);
        build/src/main/fight_modes.o(.text);
        build/src/main/fight_start.o(.text);
        build/src/main/menu_screen.o(.text);
        build/src/main/cdrom.o(.text);
        build/src/main/memcard.o(.text);
        build/src/main/memcard_save.o(.text);
        build/src/main/replay.o(.text);
        build/src/main/link.o(.text);
        build/src/main/story.o(.text);
        build/src/main/fight_after.o(.text);
        build/src/main/font.o(.text);
        build/src/main/bitstream.o(.text);
        build/src/main/stage.o(.text);
        build/src/main/cloth_model.o(.text);
        build/src/main/model_face.o(.text);
        build/src/main/model.o(.text);
        build/src/main/light.o(.text);
        build/src/main/math.o(.text);
        build/src/main/render.o(.text);
        build/src/main/stage_props.o(.text);
        build/src/main/math_sin.o(.text);
        build/src/main/ndata.o(.text);
        build/src/main/arena.o(.text);
        build/src/main/resource.o(.text);
        build/src/main/scene_frame.o(.text);
        build/src/main/stage_hooks.o(.text);
        build/src/main/wave.o(.text);
        build/src/main/tex.o(.text);
        build/src/main/model_tiny.o(.text);
        build/src/main/tex_scroll.o(.text);
        build/src/main/resource_weapon.o(.text);
        build/src/main/scene.o(.text);
        build/src/main/render_asm.o(.text);
        build/src/main/math_asm.o(.text);
        build/src/main/collision.o(.text);
        build/src/main/render_skin_asm.o(.text);
        build/src/main/cutscene.o(.text);
        build/src/main/cpu.o(.text);
        build/src/main/snd.o(.text);
        build/src/main/hud.o(.text);
        build/src/main/eff.o(.text);
        build/src/main/menu_pages.o(.text);
        build/src/main/select.o(.text);
        build/src/main/select_team.o(.text);
        build/src/main/menu_run.o(.text);
        build/src/main/narration.o(.text);
        build/src/main/staff.o(.text);
        build/src/main/psxsdk/libapi/c67.o(.text);
        build/src/main/psxsdk/libapi/c112.o(.text);
        build/src/main/psxsdk/libapi/c159.o(.text);
        build/src/main/psxsdk/libapi/a08.o(.text);
        build/src/main/psxsdk/libapi/a09.o(.text);
        build/src/main/psxsdk/libapi/a11.o(.text);
        build/src/main/psxsdk/libapi/a12.o(.text);
        build/src/main/psxsdk/libapi/a36.o(.text);
        build/src/main/psxsdk/libapi/a37.o(.text);
        build/src/main/psxsdk/libapi/a39.o(.text);
        build/src/main/psxsdk/libapi/a50.o(.text);
        build/src/main/psxsdk/libapi/a52.o(.text);
        build/src/main/psxsdk/libapi/a53.o(.text);
        build/src/main/psxsdk/libapi/a54.o(.text);
        build/src/main/psxsdk/libapi/a65.o(.text);
        build/src/main/psxsdk/libapi/a66.o(.text);
        build/src/main/psxsdk/libapi/a67.o(.text);
        build/src/main/psxsdk/libapi/a91.o(.text);
        build/src/main/psxsdk/libapi/counter.o(.text);
        build/src/main/psxsdk/libapi/pad.o(.text);
        build/src/main/psxsdk/libapi/a18.o(.text);
        build/src/main/psxsdk/libapi/a19.o(.text);
        build/src/main/psxsdk/libapi/a20.o(.text);
        build/src/main/psxsdk/libapi/a21.o(.text);
        build/src/main/psxsdk/libapi/l02.o(.text);
        build/src/main/psxsdk/libapi/l03.o(.text);
        build/src/main/psxsdk/libapi/patch.o(.text);
        build/src/main/psxsdk/libapi/c68.o(.text);
        build/src/main/psxsdk/libapi/sendpad.o(.text);
        build/src/main/psxsdk/libapi/chclrpad.o(.text);
        build/src/main/psxsdk/libc2/memcpy.o(.text);
        build/src/main/psxsdk/libc2/rand.o(.text);
        build/src/main/psxsdk/libc2/strcpy.o(.text);
        build/src/main/psxsdk/libc2/strlen.o(.text);
        build/src/main/psxsdk/libc2/printf.o(.text);
        build/src/main/psxsdk/libc2/prnt.o(.text);
        build/src/main/psxsdk/libc2/ctype.o(.text);
        build/src/main/psxsdk/libc2/memchr.o(.text);
        build/src/main/psxsdk/libc2/putchar.o(.text);
        build/src/main/psxsdk/libc2/sprintf.o(.text);
        build/src/main/psxsdk/libc2/memmove.o(.text);
        build/src/main/psxsdk/libcard/c171.o(.text);
        build/src/main/psxsdk/libcard/c172.o(.text);
        build/src/main/psxsdk/libcard/card.o(.text);
        build/src/main/psxsdk/libcard/a78.o(.text);
        build/src/main/psxsdk/libcard/a80.o(.text);
        build/src/main/psxsdk/libcard/init.o(.text);
        build/src/main/psxsdk/libcard/a74.o(.text);
        build/src/main/psxsdk/libcard/a75.o(.text);
        build/src/main/psxsdk/libcard/a76.o(.text);
        build/src/main/psxsdk/libcard/end.o(.text);
        build/src/main/psxsdk/libgpu/ext.o(.text);
        build/src/main/psxsdk/libgpu/prim.o(.text);
        build/src/main/psxsdk/libgpu/sys.o(.text);
        build/src/main/psxsdk/libapi/c73.o(.text);
        build/src/main/psxsdk/libgte/geo_00.o(.text);
        build/src/main/psxsdk/libgte/geo_01.o(.text);
        build/src/main/psxsdk/libgte/msc00.o(.text);
        build/src/main/psxsdk/libgte/msc01.o(.text);
        build/src/main/psxsdk/libgte/msc06.o(.text);
        build/src/main/psxsdk/libgte/msc09.o(.text);
        build/src/main/psxsdk/libgte/mtx_000.o(.text);
        build/src/main/psxsdk/libgte/mtx_003.o(.text);
        build/src/main/psxsdk/libgte/mtx_004.o(.text);
        build/src/main/psxsdk/libgte/mtx_005.o(.text);
        build/src/main/psxsdk/libgte/mtx_00a.o(.text);
        build/src/main/psxsdk/libgte/mtx_01.o(.text);
        build/src/main/psxsdk/libgte/mtx_03.o(.text);
        build/src/main/psxsdk/libgte/mtx_04.o(.text);
        build/src/main/psxsdk/libgte/mtx_05.o(.text);
        build/src/main/psxsdk/libgte/mtx_08.o(.text);
        build/src/main/psxsdk/libgte/mtx_09.o(.text);
        build/src/main/psxsdk/libgte/mtx_11.o(.text);
        build/src/main/psxsdk/libgte/mtx_12.o(.text);
        build/src/main/psxsdk/libgte/reg04.o(.text);
        build/src/main/psxsdk/libgte/reg09.o(.text);
        build/src/main/psxsdk/libgte/reg10.o(.text);
        build/src/main/psxsdk/libgte/reg11.o(.text);
        build/src/main/psxsdk/libgte/reg12.o(.text);
        build/src/main/psxsdk/libgte/reg13.o(.text);
        build/src/main/psxsdk/libgte/smp_00.o(.text);
        build/src/main/psxsdk/libgte/smp_02.o(.text);
        build/src/main/psxsdk/libgte/smp_03.o(.text);
        build/src/main/psxsdk/libgte/smp_04.o(.text);
        build/src/main/psxsdk/libgte/cmb_00.o(.text);
        build/src/main/psxsdk/libgte/fgo_01.o(.text);
        build/src/main/psxsdk/libgte/fgo_03.o(.text);
        build/src/main/psxsdk/libgte/fgo_04.o(.text);
        build/src/main/psxsdk/libgte/fgo_05.o(.text);
        build/src/main/psxsdk/libgte/fgo_06.o(.text);
        build/src/main/psxsdk/libgte/ratan.o(.text);
        build/src/main/psxsdk/libgte/patchgte.o(.text);
        build/src/main/psxsdk/libcd/event.o(.text);
        build/src/main/psxsdk/libapi/a07.o(.text);
        build/src/main/psxsdk/libcd/sys.o(.text);
        build/src/main/psxsdk/libcd/bios.o(.text);
        build/src/main/psxsdk/libc2/puts.o(.text);
        build/src/main/psxsdk/libcd/cdread.o(.text);
        build/src/main/psxsdk/libetc/vsync.o(.text);
        build/src/main/psxsdk/libapi/l10.o(.text);
        build/src/main/psxsdk/libetc/intr.o(.text);
        build/src/main/psxsdk/libapi/c114.o(.text);
        build/src/main/psxsdk/libapi/a23.o(.text);
        build/src/main/psxsdk/libapi/a24.o(.text);
        build/src/main/psxsdk/libapi/a25.o(.text);
        build/src/main/psxsdk/libc2/setjmp.o(.text);
        build/src/main/psxsdk/libetc/intr_vb.o(.text);
        build/src/main/psxsdk/libetc/intr_dma.o(.text);
        build/src/main/psxsdk/libetc/vmode.o(.text);
        build/src/main/psxsdk/libsn/open.o(.text);
        build/src/main/psxsdk/libsn/close.o(.text);
        build/src/main/psxsdk/libsn/lseek.o(.text);
        build/src/main/psxsdk/libsn/snmain.o(.text);
        build/src/main/psxsdk/libsn/sndef.o(.text);
        build/src/main/psxsdk/libapi/c57.o(.text);
        build/src/main/psxsdk/libsn/read.o(.text);
        build/src/main/psxsdk/libsn/snread.o(.text);
        build/src/main/psxsdk/libsnd/ssend.o(.text);
        build/src/main/psxsdk/libsnd/ssinit_c.o(.text);
        build/src/main/psxsdk/libsnd/ssinit.o(.text);
        build/src/main/psxsdk/libsnd/ssquit.o(.text);
        build/src/main/psxsdk/libsnd/scssattr.o(.text);
        build/src/main/psxsdk/libsnd/scsmvol.o(.text);
        build/src/main/psxsdk/libsnd/ssstart.o(.text);
        build/src/main/psxsdk/libsnd/sscall.o(.text);
        build/src/main/psxsdk/libsnd/cres.o(.text);
        build/src/main/psxsdk/libsnd/decre.o(.text);
        build/src/main/psxsdk/libsnd/pause.o(.text);
        build/src/main/psxsdk/libsnd/play.o(.text);
        build/src/main/psxsdk/libsnd/midiread.o(.text);
        build/src/main/psxsdk/libsnd/miditime.o(.text);
        build/src/main/psxsdk/libsnd/next.o(.text);
        build/src/main/psxsdk/libsnd/replay.o(.text);
        build/src/main/psxsdk/libsnd/stop.o(.text);
        build/src/main/psxsdk/libsnd/scssvol.o(.text);
        build/src/main/psxsdk/libsnd/sstick.o(.text);
        build/src/main/psxsdk/libsnd/tempo.o(.text);
        build/src/main/psxsdk/libsnd/ut_ako.o(.text);
        build/src/main/psxsdk/libsnd/ut_gvba.o(.text);
        build/src/main/psxsdk/libsnd/ut_keyv.o(.text);
        build/src/main/psxsdk/libsnd/ut_rdep.o(.text);
        build/src/main/psxsdk/libsnd/ut_rev.o(.text);
        build/src/main/psxsdk/libsnd/ut_roff.o(.text);
        build/src/main/psxsdk/libsnd/ut_ron.o(.text);
        build/src/main/psxsdk/libsnd/ut_vvol.o(.text);
        build/src/main/psxsdk/libsnd/vm_aloc2.o(.text);
        build/src/main/psxsdk/libsnd/vm_doff.o(.text);
        build/src/main/psxsdk/libsnd/vm_f.o(.text);
        build/src/main/psxsdk/libsnd/vm_init.o(.text);
        build/src/main/psxsdk/libsnd/vm_n2p.o(.text);
        build/src/main/psxsdk/libsnd/vm_no1.o(.text);
        build/src/main/psxsdk/libsnd/vm_nowof.o(.text);
        build/src/main/psxsdk/libsnd/vm_nowon.o(.text);
        build/src/main/psxsdk/libsnd/vm_seq.o(.text);
        build/src/main/psxsdk/libsnd/vm_vsu.o(.text);
        build/src/main/psxsdk/libsnd/vs_auto.o(.text);
        build/src/main/psxsdk/libsnd/vs_mono.o(.text);
        build/src/main/psxsdk/libsnd/vs_srv.o(.text);
        build/src/main/psxsdk/libsnd/vs_vab.o(.text);
        build/src/main/psxsdk/libsnd/vs_vfb.o(.text);
        build/src/main/psxsdk/libsnd/vs_vh.o(.text);
        build/src/main/psxsdk/libsnd/vs_vtb.o(.text);
        build/src/main/psxsdk/libsnd/vs_vtc.o(.text);
        build/src/main/psxsdk/libspu/s_i.o(.text);
        build/src/main/psxsdk/libspu/s_ini.o(.text);
        build/src/main/psxsdk/libspu/spu.o(.text);
        build/src/main/psxsdk/libspu/s_dcb.o(.text);
        build/src/main/psxsdk/libspu/s_q.o(.text);
        build/src/main/psxsdk/libapi/a13.o(.text);
        build/src/main/psxsdk/libspu/s_m_init.o(.text);
        build/src/main/psxsdk/libspu/s_m_m.o(.text);
        build/src/main/psxsdk/libspu/s_m_int.o(.text);
        build/src/main/psxsdk/libspu/s_m_f.o(.text);
        build/src/main/psxsdk/libspu/s_snv.o(.text);
        build/src/main/psxsdk/libspu/s_sav.o(.text);
        build/src/main/psxsdk/libspu/s_snc.o(.text);
        build/src/main/psxsdk/libspu/s_sr.o(.text);
        build/src/main/psxsdk/libspu/s_m_util.o(.text);
        build/src/main/psxsdk/libspu/s_srmp.o(.text);
        build/src/main/psxsdk/libspu/s_sra.o(.text);
        build/src/main/psxsdk/libspu/s_srv.o(.text);
        build/src/main/psxsdk/libspu/s_crwa.o(.text);
        build/src/main/psxsdk/libapi/a10.o(.text);
        build/src/main/psxsdk/libspu/s_sk.o(.text);
        build/src/main/psxsdk/libspu/s_gks.o(.text);
        build/src/main/psxsdk/libspu/s_r.o(.text);
        build/src/main/psxsdk/libspu/s_w.o(.text);
        build/src/main/psxsdk/libspu/s_stsa.o(.text);
        build/src/main/psxsdk/libspu/s_stm.o(.text);
        build/src/main/psxsdk/libspu/s_itc.o(.text);
        build/src/main/psxsdk/libspu/s_it.o(.text);
        build/src/main/psxsdk/libspu/s_sca.o(.text);
        build/src/main/psxsdk/libspu/sr_gaks.o(.text);
        build/src/main/psxsdk/libspu/s_sva.o(.text);
        build/src/main/psxsdk/libspu/s_n2p.o(.text);
        build/src/main/psxsdk/libspu/s_gvv.o(.text);
        build/src/main/psxsdk/libspu/s_gvex.o(.text);
        build/src/main/psxsdk/libcomb/comb.o(.text);
        build/src/main/psxsdk/libapi/a71.o(.text);
        build/src/main/psxsdk/libapi/a72.o(.text);
        main_TEXT_END = .;
        main_TEXT_SIZE = ABSOLUTE(main_TEXT_END - main_TEXT_START);
        main_DATA_START = .;
        build/src/main/main.o(.data);
        build/src/main/cloth.o(.data);
        build/src/main/pad.o(.data);
        build/src/main/motion_decode.o(.data);
        build/src/main/camera.o(.data);
        build/src/main/fighter_pad.o(.data);
        build/src/main/fight.o(.data);
        build/src/main/fighter_pose.o(.data);
        build/src/main/fighter.o(.data);
        build/src/main/fighter_hit.o(.data);
        build/src/main/hit_test.o(.data);
        build/src/main/object.o(.data);
        build/src/main/fight_event.o(.data);
        build/src/main/fight_modes.o(.data);
        build/src/main/fight_start.o(.data);
        build/src/main/menu_screen.o(.data);
        build/src/main/fight_data.o(.data);
        build/src/main/cdrom.o(.data);
        build/src/main/memcard.o(.data);
        build/src/main/memcard_save.o(.data);
        build/src/main/replay.o(.data);
        build/src/main/link.o(.data);
        build/src/main/story.o(.data);
        build/src/main/fight_after.o(.data);
        . = ALIGN(4);
        build/src/main/font.o(.data);
        build/src/main/bitstream.o(.data);
        build/src/main/stage.o(.data);
        build/src/main/cloth_model.o(.data);
        build/src/main/stage_data.o(.data);
        build/src/main/model_face.o(.data);
        build/src/main/model.o(.data);
        build/src/main/light.o(.data);
        build/src/main/math.o(.data);
        build/src/main/render.o(.data);
        build/src/main/stage_props.o(.data);
        build/src/main/ndata.o(.data);
        build/src/main/math_sin.o(.data);
        build/src/main/arena.o(.data);
        build/src/main/resource.o(.data);
        build/src/main/scene_frame.o(.data);
        build/src/main/stage_hooks.o(.data);
        build/src/main/wave.o(.data);
        build/src/main/tex.o(.data);
        build/src/main/model_tiny.o(.data);
        build/src/main/tex_scroll.o(.data);
        build/src/main/resource_weapon.o(.data);
        build/src/main/scene.o(.data);
        build/src/main/render_asm.o(.data);
        build/src/main/math_asm.o(.data);
        build/src/main/resource_data.o(.data);
        build/src/main/collision.o(.data);
        build/src/main/render_skin_asm.o(.data);
        build/src/main/cutscene.o(.data);
        build/src/main/cpu.o(.data);
        build/src/main/cpu_data.o(.data);
        build/src/main/snd_data.o(.data);
        build/src/main/snd.o(.data);
        build/src/main/hud.o(.data);
        build/src/main/hud_data.o(.data);
        build/src/main/eff_data.o(.data);
        build/src/main/eff.o(.data);
        build/src/main/menu_pages.o(.data);
        build/src/main/select_data.o(.data);
        build/src/main/select.o(.data);
        build/src/main/select_team.o(.data);
        build/src/main/menu_data.o(.data);
        build/src/main/menu_run.o(.data);
        build/src/main/narration.o(.data);
        build/src/main/staff.o(.data);
        build/src/main/psxsdk/libapi/c67.o(.data);
        build/src/main/psxsdk/libapi/c112.o(.data);
        build/src/main/psxsdk/libapi/c159.o(.data);
        build/src/main/psxsdk/libapi/a08.o(.data);
        build/src/main/psxsdk/libapi/a09.o(.data);
        build/src/main/psxsdk/libapi/a11.o(.data);
        build/src/main/psxsdk/libapi/a12.o(.data);
        build/src/main/psxsdk/libapi/a36.o(.data);
        build/src/main/psxsdk/libapi/a37.o(.data);
        build/src/main/psxsdk/libapi/a39.o(.data);
        build/src/main/psxsdk/libapi/a50.o(.data);
        build/src/main/psxsdk/libapi/a52.o(.data);
        build/src/main/psxsdk/libapi/a53.o(.data);
        build/src/main/psxsdk/libapi/a54.o(.data);
        build/src/main/psxsdk/libapi/a65.o(.data);
        build/src/main/psxsdk/libapi/a66.o(.data);
        build/src/main/psxsdk/libapi/a67.o(.data);
        build/src/main/psxsdk/libapi/a91.o(.data);
        build/src/main/psxsdk/libapi/counter.o(.data);
        build/src/main/psxsdk/libapi/pad.o(.data);
        build/src/main/psxsdk/libapi/a18.o(.data);
        build/src/main/psxsdk/libapi/a19.o(.data);
        build/src/main/psxsdk/libapi/a20.o(.data);
        build/src/main/psxsdk/libapi/a21.o(.data);
        build/src/main/psxsdk/libapi/l02.o(.data);
        build/src/main/psxsdk/libapi/l03.o(.data);
        build/src/main/psxsdk/libapi/patch.o(.data);
        build/src/main/psxsdk/libapi/c68.o(.data);
        build/src/main/psxsdk/libapi/sendpad.o(.data);
        build/src/main/psxsdk/libapi/chclrpad.o(.data);
        build/src/main/psxsdk/libc2/memcpy.o(.data);
        build/src/main/psxsdk/libc2/rand.o(.data);
        build/src/main/psxsdk/libc2/strcpy.o(.data);
        build/src/main/psxsdk/libc2/strlen.o(.data);
        build/src/main/psxsdk/libc2/printf.o(.data);
        build/src/main/psxsdk/libc2/prnt.o(.data);
        build/src/main/psxsdk/libc2/ctype.o(.data);
        . = ALIGN(4);
        build/src/main/psxsdk/libc2/memchr.o(.data);
        build/src/main/psxsdk/libc2/putchar.o(.data);
        build/src/main/psxsdk/libc2/sprintf.o(.data);
        build/src/main/psxsdk/libc2/memmove.o(.data);
        build/src/main/psxsdk/libcard/c171.o(.data);
        build/src/main/psxsdk/libcard/c172.o(.data);
        build/src/main/psxsdk/libcard/card.o(.data);
        build/src/main/psxsdk/libcard/a78.o(.data);
        build/src/main/psxsdk/libcard/a80.o(.data);
        build/src/main/psxsdk/libcard/init.o(.data);
        build/src/main/psxsdk/libcard/a74.o(.data);
        build/src/main/psxsdk/libcard/a75.o(.data);
        build/src/main/psxsdk/libcard/a76.o(.data);
        build/src/main/psxsdk/libcard/end.o(.data);
        build/src/main/psxsdk/libgpu/ext.o(.data);
        build/src/main/psxsdk/libgpu/prim.o(.data);
        build/src/main/psxsdk/libgpu/sys.o(.data);
        build/src/main/psxsdk/libapi/c73.o(.data);
        build/src/main/psxsdk/libgte/geo_00.o(.data);
        build/src/main/psxsdk/libgte/geo_01.o(.data);
        build/src/main/psxsdk/libgte/msc01.o(.data);
        build/src/main/psxsdk/libgte/msc06.o(.data);
        build/src/main/psxsdk/libgte/msc09.o(.data);
        build/src/main/psxsdk/libgte/mtx_000.o(.data);
        build/src/main/psxsdk/libgte/mtx_003.o(.data);
        build/src/main/psxsdk/libgte/mtx_004.o(.data);
        build/src/main/psxsdk/libgte/mtx_005.o(.data);
        build/src/main/psxsdk/libgte/mtx_00a.o(.data);
        build/src/main/psxsdk/libgte/mtx_01.o(.data);
        build/src/main/psxsdk/libgte/mtx_03.o(.data);
        build/src/main/psxsdk/libgte/mtx_04.o(.data);
        build/src/main/psxsdk/libgte/mtx_05.o(.data);
        build/src/main/psxsdk/libgte/mtx_08.o(.data);
        build/src/main/psxsdk/libgte/mtx_09.o(.data);
        build/src/main/psxsdk/libgte/mtx_11.o(.data);
        build/src/main/psxsdk/libgte/mtx_12.o(.data);
        build/src/main/psxsdk/libgte/reg04.o(.data);
        build/src/main/psxsdk/libgte/reg09.o(.data);
        build/src/main/psxsdk/libgte/reg10.o(.data);
        build/src/main/psxsdk/libgte/reg11.o(.data);
        build/src/main/psxsdk/libgte/reg12.o(.data);
        build/src/main/psxsdk/libgte/reg13.o(.data);
        build/src/main/psxsdk/libgte/smp_00.o(.data);
        build/src/main/psxsdk/libgte/smp_02.o(.data);
        build/src/main/psxsdk/libgte/smp_03.o(.data);
        build/src/main/psxsdk/libgte/smp_04.o(.data);
        build/src/main/psxsdk/libgte/cmb_00.o(.data);
        build/src/main/psxsdk/libgte/fgo_01.o(.data);
        build/src/main/psxsdk/libgte/fgo_03.o(.data);
        build/src/main/psxsdk/libgte/fgo_04.o(.data);
        build/src/main/psxsdk/libgte/fgo_05.o(.data);
        build/src/main/psxsdk/libgte/fgo_06.o(.data);
        build/src/main/psxsdk/libgte/sincos.o(.data);
        . = ALIGN(4);
        build/src/main/psxsdk/libgte/msc00.o(.data);
        build/src/main/psxsdk/libgte/sqrtbl.o(.data);
        build/src/main/psxsdk/libgte/cstbl.o(.data);
        build/src/main/psxsdk/libgte/ratan.o(.data);
        . = ALIGN(4);
        build/src/main/psxsdk/libgte/patchgte.o(.data);
        build/src/main/psxsdk/libcd/event.o(.data);
        build/src/main/psxsdk/libapi/a07.o(.data);
        build/src/main/psxsdk/libcd/sys.o(.data);
        /* libcd bios.c: Intr (static volatile CD_intr, defined in src/main/psxsdk/libcd/bios.c) at 0x800A1494 */
        build/src/main/psxsdk/libcd/bios.o(.data);
        build/src/main/psxsdk/libc2/puts.o(.data);
        build/src/main/psxsdk/libcd/cdread.o(.data);
        build/src/main/psxsdk/libetc/vsync.o(.data);
        build/src/main/psxsdk/libapi/l10.o(.data);
        build/src/main/psxsdk/libetc/intr.o(.data);
        build/src/main/psxsdk/libapi/c114.o(.data);
        build/src/main/psxsdk/libapi/a23.o(.data);
        build/src/main/psxsdk/libapi/a24.o(.data);
        build/src/main/psxsdk/libapi/a25.o(.data);
        build/src/main/psxsdk/libc2/setjmp.o(.data);
        build/src/main/psxsdk/libetc/intr_vb.o(.data);
        build/src/main/psxsdk/libetc/intr_dma.o(.data);
        build/src/main/psxsdk/libetc/vmode.o(.data);
        build/src/main/psxsdk/libsn/open.o(.data);
        build/src/main/psxsdk/libsn/close.o(.data);
        build/src/main/psxsdk/libsn/lseek.o(.data);
        build/src/main/psxsdk/libsn/snmain.o(.data);
        build/src/main/psxsdk/libsn/sndef.o(.data);
        build/src/main/psxsdk/libapi/c57.o(.data);
        build/src/main/psxsdk/libsn/read.o(.data);
        build/src/main/psxsdk/libsn/snread.o(.data);
        build/src/main/psxsdk/libsnd/ssend.o(.data);
        build/src/main/psxsdk/libsnd/ssinit_c.o(.data);
        build/src/main/psxsdk/libsnd/ssinit.o(.data);
        build/src/main/psxsdk/libsnd/ssquit.o(.data);
        build/src/main/psxsdk/libsnd/scssattr.o(.data);
        build/src/main/psxsdk/libsnd/scsmvol.o(.data);
        build/src/main/psxsdk/libsnd/ssstart.o(.data);
        build/src/main/psxsdk/libsnd/sscall.o(.data);
        build/src/main/psxsdk/libsnd/cres.o(.data);
        build/src/main/psxsdk/libsnd/decre.o(.data);
        build/src/main/psxsdk/libsnd/pause.o(.data);
        build/src/main/psxsdk/libsnd/play.o(.data);
        build/src/main/psxsdk/libsnd/midiread.o(.data);
        build/src/main/psxsdk/libsnd/miditime.o(.data);
        build/src/main/psxsdk/libsnd/next.o(.data);
        build/src/main/psxsdk/libsnd/replay.o(.data);
        build/src/main/psxsdk/libsnd/stop.o(.data);
        build/src/main/psxsdk/libsnd/scssvol.o(.data);
        build/src/main/psxsdk/libsnd/sstick.o(.data);
        build/src/main/psxsdk/libsnd/tempo.o(.data);
        build/src/main/psxsdk/libsnd/ut_ako.o(.data);
        build/src/main/psxsdk/libsnd/ut_gvba.o(.data);
        build/src/main/psxsdk/libsnd/ut_keyv.o(.data);
        build/src/main/psxsdk/libsnd/ut_rdep.o(.data);
        build/src/main/psxsdk/libsnd/ut_rev.o(.data);
        build/src/main/psxsdk/libsnd/ut_roff.o(.data);
        build/src/main/psxsdk/libsnd/ut_ron.o(.data);
        build/src/main/psxsdk/libsnd/ut_vvol.o(.data);
        build/src/main/psxsdk/libsnd/vm_aloc2.o(.data);
        build/src/main/psxsdk/libsnd/vm_doff.o(.data);
        build/src/main/psxsdk/libsnd/vm_f.o(.data);
        build/src/main/psxsdk/libsnd/vm_init.o(.data);
        build/src/main/psxsdk/libsnd/vm_n2p.o(.data);
        . = ALIGN(4);
        build/src/main/psxsdk/libsnd/vm_no1.o(.data);
        build/src/main/psxsdk/libsnd/vm_nowof.o(.data);
        build/src/main/psxsdk/libsnd/vm_nowon.o(.data);
        build/src/main/psxsdk/libsnd/vm_seq.o(.data);
        build/src/main/psxsdk/libsnd/vm_vsu.o(.data);
        build/src/main/psxsdk/libsnd/vs_auto.o(.data);
        build/src/main/psxsdk/libsnd/vs_mono.o(.data);
        build/src/main/psxsdk/libsnd/vs_srv.o(.data);
        build/src/main/psxsdk/libsnd/vs_vab.o(.data);
        build/src/main/psxsdk/libsnd/vs_vfb.o(.data);
        build/src/main/psxsdk/libsnd/vs_vh.o(.data);
        build/src/main/psxsdk/libsnd/vs_vtb.o(.data);
        build/src/main/psxsdk/libsnd/vs_vtc.o(.data);
        build/src/main/psxsdk/libspu/s_i.o(.data);
        build/src/main/psxsdk/libspu/s_ini.o(.data);
        build/src/main/psxsdk/libspu/spu.o(.data);
        build/src/main/psxsdk/libspu/s_m.o(.data);
        build/src/main/psxsdk/libspu/s_rmp.o(.data);
        build/src/main/psxsdk/libspu/s_dcb.o(.data);
        build/src/main/psxsdk/libspu/s_q.o(.data);
        build/src/main/psxsdk/libapi/a13.o(.data);
        build/src/main/psxsdk/libspu/s_m_init.o(.data);
        build/src/main/psxsdk/libspu/s_m_m.o(.data);
        build/src/main/psxsdk/libspu/s_m_int.o(.data);
        build/src/main/psxsdk/libspu/s_m_f.o(.data);
        build/src/main/psxsdk/libspu/s_snv.o(.data);
        build/src/main/psxsdk/libspu/s_sav.o(.data);
        build/src/main/psxsdk/libspu/s_snc.o(.data);
        build/src/main/psxsdk/libspu/s_sr.o(.data);
        build/src/main/psxsdk/libspu/s_m_util.o(.data);
        build/src/main/psxsdk/libspu/s_srmp.o(.data);
        build/src/main/psxsdk/libspu/s_sra.o(.data);
        build/src/main/psxsdk/libspu/s_srv.o(.data);
        build/src/main/psxsdk/libspu/s_crwa.o(.data);
        build/src/main/psxsdk/libapi/a10.o(.data);
        build/src/main/psxsdk/libspu/s_sk.o(.data);
        build/src/main/psxsdk/libspu/s_gks.o(.data);
        build/src/main/psxsdk/libspu/s_r.o(.data);
        build/src/main/psxsdk/libspu/s_w.o(.data);
        build/src/main/psxsdk/libspu/s_stsa.o(.data);
        build/src/main/psxsdk/libspu/s_stm.o(.data);
        build/src/main/psxsdk/libspu/s_itc.o(.data);
        build/src/main/psxsdk/libspu/s_it.o(.data);
        build/src/main/psxsdk/libspu/s_sca.o(.data);
        build/src/main/psxsdk/libspu/sr_gaks.o(.data);
        build/src/main/psxsdk/libspu/s_sva.o(.data);
        build/src/main/psxsdk/libspu/s_n2p.o(.data);
        build/src/main/psxsdk/libspu/s_gvv.o(.data);
        build/src/main/psxsdk/libspu/s_gvex.o(.data);
        build/src/main/psxsdk/libcomb/comb.o(.data);
        build/src/main/psxsdk/libapi/a71.o(.data);
        build/src/main/psxsdk/libapi/a72.o(.data);
        /* The small data, which the code reaches through gp. Sony's start-up
         * code (snmain.c) takes .data to end here. */
        _gp = .;
        main_DATA_LEN = ABSOLUTE(_gp - main_DATA_START);
        build/src/main/main.o(.sdata);
        build/src/main/cloth.o(.sdata);
        . = ALIGN(4);
        build/src/main/motion_decode.o(.sdata);
        build/src/main/camera.o(.sdata);
        build/src/main/fighter_pad.o(.sdata);
        build/src/main/fight.o(.sdata);
        build/src/main/fighter_pose.o(.sdata);
        build/src/main/fighter.o(.sdata);
        . = ALIGN(4);
        build/src/main/fight_sdata.o(.data);
        build/src/main/fighter_hit.o(.sdata);
        build/src/main/hit_test.o(.sdata);
        build/src/main/object.o(.sdata);
        build/src/main/fight_event.o(.sdata);
        build/src/main/fight_modes.o(.sdata);
        build/src/main/fight_start.o(.sdata);
        build/src/main/menu_screen.o(.sdata);
        build/src/main/cdrom.o(.sdata);
        . = ALIGN(4);
        build/src/main/memcard.o(.sdata);
        build/src/main/memcard_save.o(.sdata);
        build/src/main/replay.o(.sdata);
        . = ALIGN(4);
        build/src/main/link.o(.sdata);
        . = ALIGN(4);
        build/src/main/story.o(.sdata);
        build/src/main/fight_after.o(.sdata);
        build/src/main/font.o(.sdata);
        build/src/main/bitstream.o(.sdata);
        build/src/main/stage.o(.sdata);
        build/src/main/cloth_model.o(.sdata);
        build/src/main/model_face.o(.sdata);
        build/src/main/light.o(.sdata);
        . = ALIGN(4);
        build/src/main/math_sin.o(.sdata);
        build/src/main/ndata.o(.sdata);
        build/src/main/arena.o(.sdata);
        build/src/main/resource.o(.sdata);
        build/src/main/scene_frame.o(.sdata);
        build/src/main/stage_hooks.o(.sdata);
        build/src/main/wave.o(.sdata);
        build/src/main/tex.o(.sdata);
        build/src/main/model_tiny.o(.sdata);
        build/src/main/tex_scroll.o(.sdata);
        build/src/main/resource_weapon.o(.sdata);
        build/src/main/scene.o(.sdata);
        build/src/main/render_asm.o(.sdata);
        build/src/main/math_asm.o(.sdata);
        build/src/main/collision.o(.sdata);
        build/src/main/render_skin_asm.o(.sdata);
        build/src/main/cutscene.o(.sdata);
        build/src/main/cpu.o(.sdata);
        build/src/main/snd.o(.sdata);
        build/src/main/hud.o(.sdata);
        build/src/main/eff.o(.sdata);
        build/src/main/menu_pages.o(.sdata);
        build/src/main/select.o(.sdata);
        build/src/main/select_team.o(.sdata);
        build/src/main/menu_run.o(.sdata);
        build/src/main/narration.o(.sdata);
        build/src/main/staff.o(.sdata);
        /* The zeroed part begins here; nothing names its first eight bytes. */
        main_SBSS_START = .;
        . += 8;
        build/src/main/motion_decode.o(.sbss);
        build/src/main/camera.o(.sbss);
        build/src/main/fighter_pad.o(.sbss);
        build/src/main/fight.o(.sbss);
        build/src/main/fighter_pose.o(.sbss);
        build/src/main/fighter.o(.sbss);
        . = ALIGN(8);
        build/src/main/memcard_save.o(.sbss);
        build/src/main/replay.o(.sbss);
        build/src/main/link.o(.sbss);
        . = ALIGN(4);
        build/src/main/story.o(.sbss);
        build/src/main/fight_after.o(.sbss);
        build/src/main/font.o(.sbss);
        build/src/main/bitstream.o(.sbss);
        build/src/main/stage.o(.sbss);
        build/src/main/cloth_model.o(.sbss);
        build/src/main/model_face.o(.sbss);
        . = ALIGN(8);
        build/src/main/light.o(.sbss);
        . += 12; /* named by nothing */
        build/src/main/math_sin.o(.sbss);
        build/src/main/ndata.o(.sbss);
        . = ALIGN(8);
        build/src/main/arena.o(.sbss);
        build/src/main/resource.o(.sbss);
        build/src/main/scene_frame.o(.sbss);
        . = ALIGN(8);
        build/src/main/stage_hooks.o(.sbss);
        . = ALIGN(8);
        build/src/main/wave.o(.sbss);
        . = ALIGN(8);
        build/src/main/tex.o(.sbss);
        build/src/main/model_tiny.o(.sbss);
        build/src/main/tex_scroll.o(.sbss);
        build/src/main/resource_weapon.o(.sbss);
        build/src/main/scene.o(.sbss);
        build/src/main/render_asm.o(.sbss);
        build/src/main/math_asm.o(.sbss);
        build/src/main/collision.o(.sbss);
        build/src/main/render_skin_asm.o(.sbss);
        build/src/main/cutscene.o(.sbss);
        build/src/main/cpu.o(.sbss);
        . = ALIGN(8);
        build/src/main/snd.o(.sbss);
        build/src/main/hud.o(.sbss);
        . = ALIGN(8);
        build/src/main/eff.o(.sbss);
        . = ALIGN(8);
        build/src/main/menu_pages.o(.sbss);
        build/src/main/select.o(.sbss);
        . = ALIGN(8);
        build/src/main/select_team.o(.sbss);
        . = ALIGN(8);
        build/src/main/menu_run.o(.sbss);
        . = ALIGN(8);
        build/src/main/narration.o(.sbss);
        . = ALIGN(8);
        build/src/main/staff.o(.sbss);
        main_DATA_END = .;
        main_DATA_SIZE = ABSOLUTE(main_DATA_END - main_DATA_START);
    }
    __romPos += SIZEOF(.main);
    __romPos = ALIGN(__romPos, 16);
    main_ROM_END = __romPos;
    main_VRAM_END = .;

    /* What starts as zeroes: NOLOAD, never in the EXE (objcopy -j .main). */
    main_bss_VRAM = ADDR(.main_bss);
    .main_bss ADDR(.main) + SIZEOF(.main) (NOLOAD) :
    {
        /* The variables no file initialises and none declares `static`.
         * Each is a tentative definition in its C file and has a section
         * of its own there, .gnu.linkonce.b.<name> (maspsx
         * --comm-own-sections); they are listed here in the order the
         * original linker gave them, which GNU ld cannot be told another
         * way. That linker put the ones of eight bytes or fewer here,
         * after the last file's .sbss, and the larger ones after the last
         * file's .bss (.main_bss_common below), each run in the order of
         * the sum of the characters of the variable's name and its
         * length, modulo 512, and each variable on a boundary that goes
         * by its size: 8 for eight bytes or more, 4 for four to seven, 2
         * for two or three. Each section has that alignment already (from
         * maspsx), so only what the rule does not give is written here:
         * a `. += n` is a variable that no code names (its name's sum
         * falls between its neighbours'), or more of the one before it.
         * docs/FAKES.md, "The zeroed variables", has each of them.
         *
         * Sony's start-up code clears from main_SBSS_START to main_BSS_END.
         * The executable's file ends in the middle of these, at the next
         * 0x800 after the data (make_psexe.py pads it with zeroes), so none
         * of this is in the file. */

        /* Sony's assembly modules: sixteen bytes each (eight for the last),
         * of which a module's code names these. The overlay has the first
         * three modules with the same spaces. */
        *(.gnu.linkonce.b._patch_pad_ra)
        . += 0x4;
        *(.gnu.linkonce.b._patch_pad_enable)
        *(.gnu.linkonce.b._patch_pad_disable)
        *(.gnu.linkonce.b._send_pad_ra)
        *(.gnu.linkonce.b._send_pad_entry)
        . += 0x8;
        *(.gnu.linkonce.b._remove_ChgclrPAD_ra)
        . += 0xc;
        *(.gnu.linkonce.b._ExitCard_ra)
        . += 0xc;
        *(.gnu.linkonce.b._patch_gte_ra)
        . += 0xc;
        *(.gnu.linkonce.b.__ra_temp)
        . += 0x4;

        /* Small variables, eight bytes or fewer. */
        *(.gnu.linkonce.b.g_fight_restart)
        *(.gnu.linkonce.b.g_story_henchmen_count)
        *(.gnu.linkonce.b.g_render_view_tilt)
        *(.gnu.linkonce.b.g_story_henchmen_left)
        *(.gnu.linkonce.b.g_link_recv_packet)
        *(.gnu.linkonce.b.g_fight_debug_view)
        /* the packet is one object of eight bytes (link.h), here and twice
         * below */
        . = ALIGN(8);
        *(.gnu.linkonce.b.g_link_send_pad_word)
        *(.gnu.linkonce.b.g_link_send_check_word)
        *(.gnu.linkonce.b.g_select_team)
        *(.gnu.linkonce.b.g_fight_stage)
        . += 0x2;
        *(.gnu.linkonce.b.g_disp_loading_noise_burst)
        *(.gnu.linkonce.b.g_disp_frame_count)
        *(.gnu.linkonce.b.g_fight_was_paused)
        *(.gnu.linkonce.b.g_camera_current)
        *(.gnu.linkonce.b.g_cdrom_fade_target)
        . = ALIGN(8);
        *(.gnu.linkonce.b.g_link_recv_pad_word)
        *(.gnu.linkonce.b.g_link_recv_check_word)
        *(.gnu.linkonce.b.g_story_player_wounds)
        *(.gnu.linkonce.b.g_fight_paired_move_flags)
        *(.gnu.linkonce.b.g_team_time_limit)
        *(.gnu.linkonce.b.g_link_prev_send_pad_word)
        *(.gnu.linkonce.b.g_link_prev_send_check_word)
        *(.gnu.linkonce.b.g_fight_paired_move)
        *(.gnu.linkonce.b.g_menu_message_tile)
        *(.gnu.linkonce.b.g_menu_message_mode)
        *(.gnu.linkonce.b.g_menu_message_sprt)
        *(.gnu.linkonce.b.g_team_result_timer)
        *(.gnu.linkonce.b.g_replay_fighter_frames)
        *(.gnu.linkonce.b.g_chanbara_fast_unlock)
        *(.gnu.linkonce.b.g_sys_frame_period)
        *(.gnu.linkonce.b.g_object_fighter_kinds)
        *(.gnu.linkonce.b.g_story_guest_wounds)
        *(.gnu.linkonce.b.g_fight_viewer)
        *(.gnu.linkonce.b.g_replay_write_frame)
        *(.gnu.linkonce.b.g_menu_debug_m_option)
        *(.gnu.linkonce.b.g_camera_mode)
        *(.gnu.linkonce.b.g_camera_turn_frames)
        . += 0x8;
        *(.gnu.linkonce.b.g_node_camera_root_ptr)
        *(.gnu.linkonce.b.g_node_identity_root_ptr)
        *(.gnu.linkonce.b.g_camera_zoom)
        *(.gnu.linkonce.b.g_story_opponent_named)
        *(.gnu.linkonce.b.g_fight_auto_pause_countdown)
        *(.gnu.linkonce.b.g_replay_object_draw_cursor)
        *(.gnu.linkonce.b.g_eff_initialized)
        *(.gnu.linkonce.b.g_cdrom_mix)
        *(.gnu.linkonce.b.g_fight_paired_move_separation)
        *(.gnu.linkonce.b.g_eff_prims)
        *(.gnu.linkonce.b.g_eff_particle_random)
        *(.gnu.linkonce.b.g_chanbara_area_changed)
        *(.gnu.linkonce.b.g_menu_file)
        *(.gnu.linkonce.b.g_link_close_requested)
        *(.gnu.linkonce.b.g_link_read_fd)
        *(.gnu.linkonce.b.g_link_event_read_done)
        *(.gnu.linkonce.b.g_link_write_fd)
        *(.gnu.linkonce.b.g_menu_page)
        *(.gnu.linkonce.b.g_sys_unused_boot_flags)
        *(.gnu.linkonce.b.g_fight_winner)
        *(.gnu.linkonce.b.g_disp_ot)
        *(.gnu.linkonce.b.g_snd_ambient_timers)
        *(.gnu.linkonce.b.g_fight_falling_fighter)
        *(.gnu.linkonce.b.g_cdrom_result)
        *(.gnu.linkonce.b.g_disp_loading_screen)
        *(.gnu.linkonce.b.g_fight_falling_forward)
        *(.gnu.linkonce.b.g_fight_story_wounds)
        *(.gnu.linkonce.b.g_fight_story_wounds_p2)
        *(.gnu.linkonce.b.g_story_cutscene_set)
        *(.gnu.linkonce.b.g_fight_paired_move_start_pending)
        *(.gnu.linkonce.b.g_disp_prim_buffers)
        *(.gnu.linkonce.b.g_fight_squashed_view)
        *(.gnu.linkonce.b.g_team_results)
        *(.gnu.linkonce.b.g_chanbara_clean_unlock)
        *(.gnu.linkonce.b.g_replay_ring_wrapped)
        *(.gnu.linkonce.b.g_vscpu_faced)
        *(.gnu.linkonce.b.g_team_packed_scores)
        *(.gnu.linkonce.b.g_fight_practice_options)
        *(.gnu.linkonce.b.g_render_ot)
        *(.gnu.linkonce.b.g_render_options)
        *(.gnu.linkonce.b.g_memcard_fd)
        *(.gnu.linkonce.b.g_disp_prim_buffer_size)
        *(.gnu.linkonce.b.g_replay_event_cursor)
        *(.gnu.linkonce.b.g_memcard_request_status)
        *(.gnu.linkonce.b.g_story_chapter)
        *(.gnu.linkonce.b.g_story_unlocks)
        *(.gnu.linkonce.b.g_story_route)
        *(.gnu.linkonce.b.g_story_interlude_scene)
        *(.gnu.linkonce.b.g_story_henchman_tint)
        *(.gnu.linkonce.b.g_sys_screen_frames)
        *(.gnu.linkonce.b.g_vscpu_weapon_cursor)
        *(.gnu.linkonce.b.g_story_route_timer)
        *(.gnu.linkonce.b.g_link_send_payload)
        *(.gnu.linkonce.b.g_story_first_henchmen)
        *(.gnu.linkonce.b.g_memcard_request_operation)
        . += 0x6;
        *(.gnu.linkonce.b.g_replay_playback_frame)
        . += 0x1;
        *(.gnu.linkonce.b.g_fight_wins_p1)
        *(.gnu.linkonce.b.g_fight_wins_p2)
        *(.gnu.linkonce.b.g_eff_next_prim)
        *(.gnu.linkonce.b.g_link_handshake_retries)
        *(.gnu.linkonce.b.g_memcard_sw_event_ioe)
        *(.gnu.linkonce.b.g_camera_zoom_used)
        *(.gnu.linkonce.b.g_team_out_timer)
        . += 0x6;
        *(.gnu.linkonce.b.g_fight_blow_dir_x)
        *(.gnu.linkonce.b.g_fight_blow_dir_y)
        *(.gnu.linkonce.b.g_fight_blow_dir_z)
        *(.gnu.linkonce.b.g_memcard_sw_event_err)
        *(.gnu.linkonce.b.g_memcard_probe_pending_result)
        *(.gnu.linkonce.b.g_team_points_to_win)
        *(.gnu.linkonce.b.g_memcard_sw_event_timeout)
        *(.gnu.linkonce.b.g_memcard_sw_event_new)
        *(.gnu.linkonce.b.g_fight_first_choice)
        *(.gnu.linkonce.b.g_render_ot_base)
        *(.gnu.linkonce.b.g_fight_continues)
        *(.gnu.linkonce.b.g_link_event_error)
        *(.gnu.linkonce.b.g_memcard_request_retry_count)
        *(.gnu.linkonce.b.g_team_break_timer)
        *(.gnu.linkonce.b.g_fight_choice)
        *(.gnu.linkonce.b.g_kaishaku_start_pair)
        *(.gnu.linkonce.b.g_fight_paired_move_is_common)
        *(.gnu.linkonce.b.g_team_time_up_timer)
        *(.gnu.linkonce.b.g_render_queue_end)
        *(.gnu.linkonce.b.g_fight_blades_met)
        *(.gnu.linkonce.b.g_light_current)
        *(.gnu.linkonce.b.g_link_write_ok)
        *(.gnu.linkonce.b.g_team_winner)
        *(.gnu.linkonce.b.g_fight_defeat_frames)
        *(.gnu.linkonce.b.g_motion_common_file)
        *(.gnu.linkonce.b.g_sys_screen)
        *(.gnu.linkonce.b.g_story_player_roster)
        *(.gnu.linkonce.b.g_memcard_hw_event_ioe)
        *(.gnu.linkonce.b.g_memcard_hw_event_err)
        *(.gnu.linkonce.b.g_cdrom_fade_step)
        *(.gnu.linkonce.b.g_story_script_cursor)
        *(.gnu.linkonce.b.g_memcard_hw_event_timeout)
        *(.gnu.linkonce.b.g_chanbara_kind)
        *(.gnu.linkonce.b.g_memcard_hw_event_new)
        *(.gnu.linkonce.b.g_cdrom_fade_len)
        *(.gnu.linkonce.b.g_chanbara_time)
        *(.gnu.linkonce.b.g_story_victim)
        *(.gnu.linkonce.b.g_motion_fighter_files)
        *(.gnu.linkonce.b.g_link_wait_state)
        *(.gnu.linkonce.b.g_team_match)
        *(.gnu.linkonce.b.g_fight_lock_runner)
        *(.gnu.linkonce.b.g_story_cutscene)
        *(.gnu.linkonce.b.g_fight_distance)
        *(.gnu.linkonce.b.g_motion_common_loaded)
        *(.gnu.linkonce.b.g_motion_win_frames)
        *(.gnu.linkonce.b.g_memcard_probe_result)
        *(.gnu.linkonce.b.g_story_interlude)
        *(.gnu.linkonce.b.g_team_points)
        *(.gnu.linkonce.b.g_team_points_p2)
        *(.gnu.linkonce.b.g_fight_practice_round)
        *(.gnu.linkonce.b.g_team_match_count)
        *(.gnu.linkonce.b.g_fight_lock_frames)
        *(.gnu.linkonce.b.g_link_side)
        *(.gnu.linkonce.b.g_fight_ending)
        . += 0x3;
        *(.gnu.linkonce.b.g_fight_lock_requested)
        *(.gnu.linkonce.b.g_team_warnings)
        *(.gnu.linkonce.b.g_team_warnings_p2)
        *(.gnu.linkonce.b.g_link_handshake_step)
        *(.gnu.linkonce.b.g_fight_paired_move_initiator)
        *(.gnu.linkonce.b.g_team_penalized)
        *(.gnu.linkonce.b.g_disp_prim_cursor)
        *(.gnu.linkonce.b.g_team_warning_timer)
        *(.gnu.linkonce.b.g_fight_first_person)
        *(.gnu.linkonce.b.g_rng_state)
        *(.gnu.linkonce.b.g_motion_win_frames_loaded)
        *(.gnu.linkonce.b.g_motion_loaded_sets)
        *(.gnu.linkonce.b.g_memcard_file_count)
        *(.gnu.linkonce.b.g_memcard_request_write_started)
        *(.gnu.linkonce.b.g_link_checksum_errors)
        *(.gnu.linkonce.b.g_fight_rematch_offered)
        *(.gnu.linkonce.b.g_scene_count)
        *(.gnu.linkonce.b.g_render_tiny_ot)
        *(.gnu.linkonce.b.g_fight_mode)
        *(.gnu.linkonce.b.g_chanbara_model)
        *(.gnu.linkonce.b.g_fight_restart_pair)
        *(.gnu.linkonce.b.g_fight_start_pair)
        *(.gnu.linkonce.b.g_team_clan_bits)
        *(.gnu.linkonce.b.g_chanbara_count)
        *(.gnu.linkonce.b.g_link_peer_unlock_mask)
        *(.gnu.linkonce.b.g_fight_paired_move_break_frame)
        *(.gnu.linkonce.b.g_chanbara_rank)
        *(.gnu.linkonce.b.g_chanbara_tint)
        *(.gnu.linkonce.b.g_fight_distance_change)
        *(.gnu.linkonce.b.g_team_time_frames)
        *(.gnu.linkonce.b.g_story_route_drawn)
        *(.gnu.linkonce.b.g_link_recv_bit_count)
        *(.gnu.linkonce.b.g_menu_message_choice_y)
        *(.gnu.linkonce.b.g_fight_rng_seed)
        *(.gnu.linkonce.b.g_snd_initialized)
        *(.gnu.linkonce.b.g_story_chapter_fights)
        *(.gnu.linkonce.b.g_link_sent_bit_count)
        *(.gnu.linkonce.b.g_story_victim_scene)
        *(.gnu.linkonce.b.g_sys_skip_frames)
        *(.gnu.linkonce.b.g_fight_loaded_stage)
        *(.gnu.linkonce.b.g_cutscene_unused_start_flag)
        *(.gnu.linkonce.b.g_fight_lock_flash_delay)
        *(.gnu.linkonce.b.g_pad_remote_map_circle)
        *(.gnu.linkonce.b.g_pad_remote_map_cross)
        *(.gnu.linkonce.b.g_pad_remote_map_square)
        *(.gnu.linkonce.b.g_story_guest_roster)
        *(.gnu.linkonce.b.g_link_first_exchange)
        *(.gnu.linkonce.b.g_snd_prop_sound_ticks)
        *(.gnu.linkonce.b.g_team_begin_countdown)
        *(.gnu.linkonce.b.g_vscpu_count)
        *(.gnu.linkonce.b.g_team_ippon_timer)
        *(.gnu.linkonce.b.g_memcard_timeout_counter)
        *(.gnu.linkonce.b.g_link_aborted)
        *(.gnu.linkonce.b.g_fight_choice_timer)
        . = ALIGN(8);
    }

    /* .bss as Sony's start-up code is told it (__bss, __bsslen): the larger
     * variables. First each file's own, the ones it declares `static`: file
     * by file in link order, in the order the file declares them. A file's
     * start on an 8-byte boundary (SUBALIGN(8)); inside a file each variable
     * is on a 4-byte one whatever its size. The three gaps are multiples of
     * eight between two files: variables of one or the other that no code
     * names, or of a file between that has no others. */
    .main_bss_files (NOLOAD) : SUBALIGN(8)
    {
        main_BSS_START = .;
        build/src/main/font.o(.bss);
        build/src/main/bitstream.o(.bss);
        build/src/main/stage.o(.bss);
        build/src/main/cloth_model.o(.bss);
        . += 0x468;
        build/src/main/model_face.o(.bss);
        build/src/main/model.o(.bss);
        build/src/main/light.o(.bss);
        . += 0x1c0;
        build/src/main/stage_props.o(.bss);
        build/src/main/arena.o(.bss);
        build/src/main/scene_frame.o(.bss);
        build/src/main/stage_hooks.o(.bss);
        . += 0x218;
        build/src/main/wave.o(.bss);
        build/src/main/tex_scroll.o(.bss);
        build/src/main/resource_weapon.o(.bss);
        build/src/main/collision.o(.bss);
        build/src/main/cutscene.o(.bss);
        build/src/main/snd.o(.bss);
        build/src/main/eff.o(.bss);
        build/src/main/menu_pages.o(.bss);
    }

    /* Those of Sony's libraries, module by module in link order. A module
     * starts on an 8-byte boundary here too. The 8 bytes after LIBCD's
     * BIOS are a later module's that no code names: the overlay has BIOS
     * without them. */
    .main_bss_sdk ADDR(.main_bss_files) + SIZEOF(.main_bss_files) (NOLOAD) : SUBALIGN(8)
    {
        build/src/main/psxsdk/libapi/pad.o(.bss);
        build/src/main/psxsdk/libc2/rand.o(.bss);
        build/src/main/psxsdk/libc2/putchar.o(.bss);
        build/src/main/psxsdk/libgpu/sys.o(.bss);
        build/src/main/psxsdk/libcd/bios.o(.bss);
        . += 0x8;
        build/src/main/psxsdk/libsnd/vm_init.o(.bss);
        build/src/main/psxsdk/libcomb/comb.o(.bss);
        . = ALIGN(8);
    }

    .main_bss_common (NOLOAD) :
    {
        /* The rest, the game's and Sony's, in the original linker's order. */
        *(.gnu.linkonce.b._svm_okon1)
        *(.gnu.linkonce.b._svm_okon2)
        *(.gnu.linkonce.b._svm_orev1)
        *(.gnu.linkonce.b.g_motion_decoders)
        *(.gnu.linkonce.b._svm_orev2)
        *(.gnu.linkonce.b.g_render_skin_colors)
        *(.gnu.linkonce.b.SsFCALL)
        *(.gnu.linkonce.b.g_shared_buffer)
        *(.gnu.linkonce.b._svm_voice)
        *(.gnu.linkonce.b.g_camera_second)
        *(.gnu.linkonce.b.g_cloth_scratch_save)
        *(.gnu.linkonce.b._svm_rattr)
        *(.gnu.linkonce.b.g_render_tiny_ot_db)
        *(.gnu.linkonce.b.g_fighter_body_points)
        . += 0x8;
        *(.gnu.linkonce.b.g_light_setups)
        *(.gnu.linkonce.b._svm_sreg_dirty)
        *(.gnu.linkonce.b.g_team_scores)
        *(.gnu.linkonce.b.g_camera_main)
        *(.gnu.linkonce.b.g_scene_settings)
        *(.gnu.linkonce.b._svm_vab_pg)
        *(.gnu.linkonce.b.g_math_rot_matrix_funcs)
        *(.gnu.linkonce.b._svm_vab_vh)
        *(.gnu.linkonce.b._svm_damper)
        *(.gnu.linkonce.b._svm_vab_tn)
        *(.gnu.linkonce.b.g_cloths)
        *(.gnu.linkonce.b.g_replay_object_draws)
        *(.gnu.linkonce.b._spu_RQ)
        *(.gnu.linkonce.b.g_disp_db)
        *(.gnu.linkonce.b.g_model_tiny_matrix)
        *(.gnu.linkonce.b._svm_stereo_mono)
        *(.gnu.linkonce.b.g_pad_packets)
        *(.gnu.linkonce.b.g_camera_offset)
        *(.gnu.linkonce.b.g_render_view_matrix)
        *(.gnu.linkonce.b._snd_ev_flag)
        *(.gnu.linkonce.b.kMaxPrograms)
        *(.gnu.linkonce.b.g_node_identity_root)
        *(.gnu.linkonce.b._svm_pg)
        *(.gnu.linkonce.b.g_snd_stage_vab_buffer)
        . += 0x4;
        *(.gnu.linkonce.b._svm_vh)
        *(.gnu.linkonce.b._svm_tn)
        *(.gnu.linkonce.b._SsVmMaxVoice)
        *(.gnu.linkonce.b.g_render_tilt_matrix)
        *(.gnu.linkonce.b.g_replay_events)
        *(.gnu.linkonce.b.g_node_camera_root)
        *(.gnu.linkonce.b.g_cdrom)
        . += 0x20;
        *(.gnu.linkonce.b.g_fighters)
        *(.gnu.linkonce.b.g_motion_common_sections)
        *(.gnu.linkonce.b.g_fight_config)
        *(.gnu.linkonce.b.g_pad_state)
        *(.gnu.linkonce.b.g_team_rosters)
        *(.gnu.linkonce.b.g_motion_fighter_sections)
        *(.gnu.linkonce.b.g_team_weapons)
        *(.gnu.linkonce.b._snd_openflag)
        *(.gnu.linkonce.b._autopan)
        *(.gnu.linkonce.b._svm_cur)
        *(.gnu.linkonce.b.g_memcard_file_list)
        *(.gnu.linkonce.b._svm_vab_used)
        *(.gnu.linkonce.b._svm_sreg_buf)
        *(.gnu.linkonce.b._autovol)
        *(.gnu.linkonce.b.g_render_queue)
        *(.gnu.linkonce.b._svm_auto_kof_mode)
        *(.gnu.linkonce.b._svm_envx_ptr)
        *(.gnu.linkonce.b.g_reloc_tables)
        *(.gnu.linkonce.b.g_reloc_counts)
        *(.gnu.linkonce.b._que)
        *(.gnu.linkonce.b.VBLANK_MINUS)
        *(.gnu.linkonce.b.g_object_dirt)
        *(.gnu.linkonce.b.g_snd_mode_vab_buffer)
        *(.gnu.linkonce.b.g_saved_record)
        *(.gnu.linkonce.b.g_object_pool)
        *(.gnu.linkonce.b._ss_score)
        *(.gnu.linkonce.b._SsMarkCallback)
        *(.gnu.linkonce.b._snd_seq_s_max)
        *(.gnu.linkonce.b._snd_seq_t_max)
        *(.gnu.linkonce.b.g_vscpu_order)
        *(.gnu.linkonce.b._svm_vab_total)
        *(.gnu.linkonce.b._svm_vab_count)
        *(.gnu.linkonce.b._svm_vab_start)
        *(.gnu.linkonce.b.g_snd_prop_sound_positions)
        *(.gnu.linkonce.b._svm_envx_hist)
        *(.gnu.linkonce.b._svm_okof1)
        *(.gnu.linkonce.b._svm_okof2)
        . = ALIGN(8);

        /* A file with only such variables has to be named once to be
         * linked, and no file has other .bss: these are all empty. */
        build/src/main/globals.o(.bss);
        build/src/main/cloth.o(.bss);
        build/src/main/pad.o(.bss);
        build/src/main/motion_decode.o(.bss);
        build/src/main/camera.o(.bss);
        build/src/main/fighter_pad.o(.bss);
        build/src/main/fight.o(.bss);
        build/src/main/fighter_pose.o(.bss);
        build/src/main/fighter.o(.bss);
        build/src/main/fighter_hit.o(.bss);
        build/src/main/hit_test.o(.bss);
        build/src/main/object.o(.bss);
        build/src/main/fight_event.o(.bss);
        build/src/main/fight_modes.o(.bss);
        build/src/main/fight_start.o(.bss);
        build/src/main/menu_screen.o(.bss);
        build/src/main/cdrom.o(.bss);
        build/src/main/memcard.o(.bss);
        build/src/main/memcard_save.o(.bss);
        build/src/main/replay.o(.bss);
        build/src/main/link.o(.bss);
        build/src/main/story.o(.bss);
        build/src/main/fight_after.o(.bss);
        build/src/main/math.o(.bss);
        build/src/main/render.o(.bss);
        build/src/main/math_sin.o(.bss);
        build/src/main/ndata.o(.bss);
        build/src/main/resource.o(.bss);
        build/src/main/tex.o(.bss);
        build/src/main/model_tiny.o(.bss);
        build/src/main/scene.o(.bss);
        build/src/main/render_asm.o(.bss);
        build/src/main/math_asm.o(.bss);
        build/src/main/render_skin_asm.o(.bss);
        build/src/main/cpu.o(.bss);
        build/src/main/hud.o(.bss);
        build/src/main/select.o(.bss);
        build/src/main/select_team.o(.bss);
        build/src/main/menu_run.o(.bss);
        build/src/main/narration.o(.bss);
        build/src/main/staff.o(.bss);
        build/src/main/psxsdk/libapi/c67.o(.bss);
        build/src/main/psxsdk/libapi/c112.o(.bss);
        build/src/main/psxsdk/libapi/c159.o(.bss);
        build/src/main/psxsdk/libapi/a08.o(.bss);
        build/src/main/psxsdk/libapi/a09.o(.bss);
        build/src/main/psxsdk/libapi/a11.o(.bss);
        build/src/main/psxsdk/libapi/a12.o(.bss);
        build/src/main/psxsdk/libapi/a36.o(.bss);
        build/src/main/psxsdk/libapi/a37.o(.bss);
        build/src/main/psxsdk/libapi/a39.o(.bss);
        build/src/main/psxsdk/libapi/a50.o(.bss);
        build/src/main/psxsdk/libapi/a52.o(.bss);
        build/src/main/psxsdk/libapi/a53.o(.bss);
        build/src/main/psxsdk/libapi/a54.o(.bss);
        build/src/main/psxsdk/libapi/a65.o(.bss);
        build/src/main/psxsdk/libapi/a66.o(.bss);
        build/src/main/psxsdk/libapi/a67.o(.bss);
        build/src/main/psxsdk/libapi/a91.o(.bss);
        build/src/main/psxsdk/libapi/counter.o(.bss);
        build/src/main/psxsdk/libapi/a18.o(.bss);
        build/src/main/psxsdk/libapi/a19.o(.bss);
        build/src/main/psxsdk/libapi/a20.o(.bss);
        build/src/main/psxsdk/libapi/a21.o(.bss);
        build/src/main/psxsdk/libapi/l02.o(.bss);
        build/src/main/psxsdk/libapi/l03.o(.bss);
        build/src/main/psxsdk/libapi/patch.o(.bss);
        build/src/main/psxsdk/libapi/c68.o(.bss);
        build/src/main/psxsdk/libapi/sendpad.o(.bss);
        build/src/main/psxsdk/libapi/chclrpad.o(.bss);
        build/src/main/psxsdk/libc2/memcpy.o(.bss);
        build/src/main/psxsdk/libc2/strcpy.o(.bss);
        build/src/main/psxsdk/libc2/strlen.o(.bss);
        build/src/main/psxsdk/libc2/printf.o(.bss);
        build/src/main/psxsdk/libc2/prnt.o(.bss);
        build/src/main/psxsdk/libc2/ctype.o(.bss);
        build/src/main/psxsdk/libc2/memchr.o(.bss);
        build/src/main/psxsdk/libc2/sprintf.o(.bss);
        build/src/main/psxsdk/libc2/memmove.o(.bss);
        build/src/main/psxsdk/libcard/c171.o(.bss);
        build/src/main/psxsdk/libcard/c172.o(.bss);
        build/src/main/psxsdk/libcard/card.o(.bss);
        build/src/main/psxsdk/libcard/a78.o(.bss);
        build/src/main/psxsdk/libcard/a80.o(.bss);
        build/src/main/psxsdk/libcard/init.o(.bss);
        build/src/main/psxsdk/libcard/a74.o(.bss);
        build/src/main/psxsdk/libcard/a75.o(.bss);
        build/src/main/psxsdk/libcard/a76.o(.bss);
        build/src/main/psxsdk/libcard/end.o(.bss);
        build/src/main/psxsdk/libgpu/ext.o(.bss);
        build/src/main/psxsdk/libgpu/prim.o(.bss);
        build/src/main/psxsdk/libapi/c73.o(.bss);
        build/src/main/psxsdk/libgte/geo_00.o(.bss);
        build/src/main/psxsdk/libgte/geo_01.o(.bss);
        build/src/main/psxsdk/libgte/msc00.o(.bss);
        build/src/main/psxsdk/libgte/msc01.o(.bss);
        build/src/main/psxsdk/libgte/msc06.o(.bss);
        build/src/main/psxsdk/libgte/msc09.o(.bss);
        build/src/main/psxsdk/libgte/mtx_000.o(.bss);
        build/src/main/psxsdk/libgte/mtx_003.o(.bss);
        build/src/main/psxsdk/libgte/mtx_004.o(.bss);
        build/src/main/psxsdk/libgte/mtx_005.o(.bss);
        build/src/main/psxsdk/libgte/mtx_00a.o(.bss);
        build/src/main/psxsdk/libgte/mtx_01.o(.bss);
        build/src/main/psxsdk/libgte/mtx_03.o(.bss);
        build/src/main/psxsdk/libgte/mtx_04.o(.bss);
        build/src/main/psxsdk/libgte/mtx_05.o(.bss);
        build/src/main/psxsdk/libgte/mtx_08.o(.bss);
        build/src/main/psxsdk/libgte/mtx_09.o(.bss);
        build/src/main/psxsdk/libgte/mtx_11.o(.bss);
        build/src/main/psxsdk/libgte/mtx_12.o(.bss);
        build/src/main/psxsdk/libgte/reg04.o(.bss);
        build/src/main/psxsdk/libgte/reg09.o(.bss);
        build/src/main/psxsdk/libgte/reg10.o(.bss);
        build/src/main/psxsdk/libgte/reg11.o(.bss);
        build/src/main/psxsdk/libgte/reg12.o(.bss);
        build/src/main/psxsdk/libgte/reg13.o(.bss);
        build/src/main/psxsdk/libgte/smp_00.o(.bss);
        build/src/main/psxsdk/libgte/smp_02.o(.bss);
        build/src/main/psxsdk/libgte/smp_03.o(.bss);
        build/src/main/psxsdk/libgte/smp_04.o(.bss);
        build/src/main/psxsdk/libgte/cmb_00.o(.bss);
        build/src/main/psxsdk/libgte/fgo_01.o(.bss);
        build/src/main/psxsdk/libgte/fgo_03.o(.bss);
        build/src/main/psxsdk/libgte/fgo_04.o(.bss);
        build/src/main/psxsdk/libgte/fgo_05.o(.bss);
        build/src/main/psxsdk/libgte/fgo_06.o(.bss);
        build/src/main/psxsdk/libgte/ratan.o(.bss);
        build/src/main/psxsdk/libgte/patchgte.o(.bss);
        build/src/main/psxsdk/libcd/event.o(.bss);
        build/src/main/psxsdk/libapi/a07.o(.bss);
        build/src/main/psxsdk/libcd/sys.o(.bss);
        build/src/main/psxsdk/libc2/puts.o(.bss);
        build/src/main/psxsdk/libcd/cdread.o(.bss);
        build/src/main/psxsdk/libetc/vsync.o(.bss);
        build/src/main/psxsdk/libapi/l10.o(.bss);
        build/src/main/psxsdk/libetc/intr.o(.bss);
        build/src/main/psxsdk/libapi/c114.o(.bss);
        build/src/main/psxsdk/libapi/a23.o(.bss);
        build/src/main/psxsdk/libapi/a24.o(.bss);
        build/src/main/psxsdk/libapi/a25.o(.bss);
        build/src/main/psxsdk/libc2/setjmp.o(.bss);
        build/src/main/psxsdk/libetc/intr_vb.o(.bss);
        build/src/main/psxsdk/libetc/intr_dma.o(.bss);
        build/src/main/psxsdk/libetc/vmode.o(.bss);
        build/src/main/psxsdk/libsn/open.o(.bss);
        build/src/main/psxsdk/libsn/close.o(.bss);
        build/src/main/psxsdk/libsn/lseek.o(.bss);
        build/src/main/psxsdk/libsn/snmain.o(.bss);
        build/src/main/psxsdk/libsn/sndef.o(.bss);
        build/src/main/psxsdk/libapi/c57.o(.bss);
        build/src/main/psxsdk/libsn/read.o(.bss);
        build/src/main/psxsdk/libsn/snread.o(.bss);
        build/src/main/psxsdk/libsnd/ssend.o(.bss);
        build/src/main/psxsdk/libsnd/ssinit_c.o(.bss);
        build/src/main/psxsdk/libsnd/ssinit.o(.bss);
        build/src/main/psxsdk/libsnd/ssquit.o(.bss);
        build/src/main/psxsdk/libsnd/scssattr.o(.bss);
        build/src/main/psxsdk/libsnd/scsmvol.o(.bss);
        build/src/main/psxsdk/libsnd/ssstart.o(.bss);
        build/src/main/psxsdk/libsnd/sscall.o(.bss);
        build/src/main/psxsdk/libsnd/cres.o(.bss);
        build/src/main/psxsdk/libsnd/decre.o(.bss);
        build/src/main/psxsdk/libsnd/pause.o(.bss);
        build/src/main/psxsdk/libsnd/play.o(.bss);
        build/src/main/psxsdk/libsnd/midiread.o(.bss);
        build/src/main/psxsdk/libsnd/miditime.o(.bss);
        build/src/main/psxsdk/libsnd/next.o(.bss);
        build/src/main/psxsdk/libsnd/replay.o(.bss);
        build/src/main/psxsdk/libsnd/stop.o(.bss);
        build/src/main/psxsdk/libsnd/scssvol.o(.bss);
        build/src/main/psxsdk/libsnd/sstick.o(.bss);
        build/src/main/psxsdk/libsnd/tempo.o(.bss);
        build/src/main/psxsdk/libsnd/ut_ako.o(.bss);
        build/src/main/psxsdk/libsnd/ut_gvba.o(.bss);
        build/src/main/psxsdk/libsnd/ut_keyv.o(.bss);
        build/src/main/psxsdk/libsnd/ut_rdep.o(.bss);
        build/src/main/psxsdk/libsnd/ut_rev.o(.bss);
        build/src/main/psxsdk/libsnd/ut_roff.o(.bss);
        build/src/main/psxsdk/libsnd/ut_ron.o(.bss);
        build/src/main/psxsdk/libsnd/ut_vvol.o(.bss);
        build/src/main/psxsdk/libsnd/vm_aloc2.o(.bss);
        build/src/main/psxsdk/libsnd/vm_doff.o(.bss);
        build/src/main/psxsdk/libsnd/vm_f.o(.bss);
        build/src/main/psxsdk/libsnd/vm_n2p.o(.bss);
        build/src/main/psxsdk/libsnd/vm_no1.o(.bss);
        build/src/main/psxsdk/libsnd/vm_nowof.o(.bss);
        build/src/main/psxsdk/libsnd/vm_nowon.o(.bss);
        build/src/main/psxsdk/libsnd/vm_seq.o(.bss);
        build/src/main/psxsdk/libsnd/vm_vsu.o(.bss);
        build/src/main/psxsdk/libsnd/vs_auto.o(.bss);
        build/src/main/psxsdk/libsnd/vs_mono.o(.bss);
        build/src/main/psxsdk/libsnd/vs_srv.o(.bss);
        build/src/main/psxsdk/libsnd/vs_vab.o(.bss);
        build/src/main/psxsdk/libsnd/vs_vfb.o(.bss);
        build/src/main/psxsdk/libsnd/vs_vh.o(.bss);
        build/src/main/psxsdk/libsnd/vs_vtb.o(.bss);
        build/src/main/psxsdk/libsnd/vs_vtc.o(.bss);
        build/src/main/psxsdk/libspu/s_i.o(.bss);
        build/src/main/psxsdk/libspu/s_ini.o(.bss);
        build/src/main/psxsdk/libspu/spu.o(.bss);
        build/src/main/psxsdk/libspu/s_dcb.o(.bss);
        build/src/main/psxsdk/libspu/s_q.o(.bss);
        build/src/main/psxsdk/libapi/a13.o(.bss);
        build/src/main/psxsdk/libspu/s_m_init.o(.bss);
        build/src/main/psxsdk/libspu/s_m_m.o(.bss);
        build/src/main/psxsdk/libspu/s_m_int.o(.bss);
        build/src/main/psxsdk/libspu/s_m_f.o(.bss);
        build/src/main/psxsdk/libspu/s_snv.o(.bss);
        build/src/main/psxsdk/libspu/s_sav.o(.bss);
        build/src/main/psxsdk/libspu/s_snc.o(.bss);
        build/src/main/psxsdk/libspu/s_sr.o(.bss);
        build/src/main/psxsdk/libspu/s_m_util.o(.bss);
        build/src/main/psxsdk/libspu/s_srmp.o(.bss);
        build/src/main/psxsdk/libspu/s_sra.o(.bss);
        build/src/main/psxsdk/libspu/s_srv.o(.bss);
        build/src/main/psxsdk/libspu/s_crwa.o(.bss);
        build/src/main/psxsdk/libapi/a10.o(.bss);
        build/src/main/psxsdk/libspu/s_sk.o(.bss);
        build/src/main/psxsdk/libspu/s_gks.o(.bss);
        build/src/main/psxsdk/libspu/s_r.o(.bss);
        build/src/main/psxsdk/libspu/s_w.o(.bss);
        build/src/main/psxsdk/libspu/s_stsa.o(.bss);
        build/src/main/psxsdk/libspu/s_stm.o(.bss);
        build/src/main/psxsdk/libspu/s_itc.o(.bss);
        build/src/main/psxsdk/libspu/s_it.o(.bss);
        build/src/main/psxsdk/libspu/s_sca.o(.bss);
        build/src/main/psxsdk/libspu/sr_gaks.o(.bss);
        build/src/main/psxsdk/libspu/s_sva.o(.bss);
        build/src/main/psxsdk/libspu/s_n2p.o(.bss);
        build/src/main/psxsdk/libspu/s_gvv.o(.bss);
        build/src/main/psxsdk/libspu/s_gvex.o(.bss);
        build/src/main/psxsdk/libapi/a71.o(.bss);
        build/src/main/psxsdk/libapi/a72.o(.bss);
        main_BSS_END = .;
        main_BSS_SIZE = ABSOLUTE(main_BSS_END - main_BSS_START);
    }
    main_bss_VRAM_END = .;

    /DISCARD/ :
    {
        *(*);
    }
}
