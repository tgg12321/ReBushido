glabel dma_execute
    /* A69C 801E269C C8FFBD27 */  addiu      $sp, $sp, -0x38
    /* A6A0 801E26A0 1800B0AF */  sw         $s0, 0x18($sp)
    /* A6A4 801E26A4 21808000 */  addu       $s0, $a0, $zero
    /* A6A8 801E26A8 2000B2AF */  sw         $s2, 0x20($sp)
    /* A6AC 801E26AC 2190A000 */  addu       $s2, $a1, $zero
    /* A6B0 801E26B0 2400B3AF */  sw         $s3, 0x24($sp)
    /* A6B4 801E26B4 2198C000 */  addu       $s3, $a2, $zero
    /* A6B8 801E26B8 2800B4AF */  sw         $s4, 0x28($sp)
    /* A6BC 801E26BC 21A0E000 */  addu       $s4, $a3, $zero
    /* A6C0 801E26C0 21200000 */  addu       $a0, $zero, $zero
    /* A6C4 801E26C4 00291000 */  sll        $a1, $s0, 4
    /* A6C8 801E26C8 0001023C */  lui        $v0, (0x1000000 >> 16)
    /* A6CC 801E26CC 3000BFAF */  sw         $ra, 0x30($sp)
    /* A6D0 801E26D0 2C00B5AF */  sw         $s5, 0x2C($sp)
    /* A6D4 801E26D4 1C00B1AF */  sw         $s1, 0x1C($sp)
    /* A6D8 801E26D8 801F013C */  lui        $at, (0x1F801088 >> 16)
    /* A6DC 801E26DC 2108A100 */  addu       $at, $a1, $at
    /* A6E0 801E26E0 8810238C */  lw         $v1, (0x1F801088 & 0xFFFF)($at)
    /* A6E4 801E26E4 4800B58F */  lw         $s5, 0x48($sp)
    /* A6E8 801E26E8 4C00B193 */  lbu        $s1, 0x4C($sp)
    /* A6EC 801E26EC 24186200 */  and        $v1, $v1, $v0
    /* A6F0 801E26F0 0D006010 */  beqz       $v1, .L801E2728
    /* A6F4 801E26F4 FF002232 */   andi      $v0, $s1, 0xFF
    /* A6F8 801E26F8 0100063C */  lui        $a2, (0x10000 >> 16)
    /* A6FC 801E26FC 0001033C */  lui        $v1, (0x1000000 >> 16)
  .L801E2700:
    /* A700 801E2700 12008610 */  beq        $a0, $a2, .L801E274C
    /* A704 801E2704 00000000 */   nop
    /* A708 801E2708 801F013C */  lui        $at, (0x1F801088 >> 16)
    /* A70C 801E270C 2108A100 */  addu       $at, $a1, $at
    /* A710 801E2710 8810228C */  lw         $v0, (0x1F801088 & 0xFFFF)($at)
    /* A714 801E2714 00000000 */  nop
    /* A718 801E2718 24104300 */  and        $v0, $v0, $v1
    /* A71C 801E271C F8FF4014 */  bnez       $v0, .L801E2700
    /* A720 801E2720 01008424 */   addiu     $a0, $a0, 0x1
    /* A724 801E2724 FF002232 */  andi       $v0, $s1, 0xFF
  .L801E2728:
    /* A728 801E2728 01000524 */  addiu      $a1, $zero, 0x1
    /* A72C 801E272C 10004514 */  bne        $v0, $a1, .L801E2770
    /* A730 801E2730 04100502 */   sllv      $v0, $a1, $s0
    /* A734 801E2734 1F80033C */  lui        $v1, %hi(d_icr)
    /* A738 801E2738 3054638C */  lw         $v1, %lo(d_icr)($v1)
    /* A73C 801E273C 00000000 */  nop
    /* A740 801E2740 02006490 */  lbu        $a0, 0x2($v1)
    /* A744 801E2744 E2890708 */  j          .L801E2788
    /* A748 801E2748 25208200 */   or        $a0, $a0, $v0
  .L801E274C:
    /* A74C 801E274C 801F013C */  lui        $at, (0x1F801088 >> 16)
    /* A750 801E2750 2108A100 */  addu       $at, $a1, $at
    /* A754 801E2754 8810258C */  lw         $a1, (0x1F801088 & 0xFFFF)($at)
    /* A758 801E2758 1E80043C */  lui        $a0, %hi(dma_status_error)
    /* A75C 801E275C DC908424 */  addiu      $a0, $a0, %lo(dma_status_error)
    /* A760 801E2760 2568070C */  jal        printf
    /* A764 801E2764 00000000 */   nop
    /* A768 801E2768 CA890708 */  j          .L801E2728
    /* A76C 801E276C FF002232 */   andi      $v0, $s1, 0xFF
  .L801E2770:
    /* A770 801E2770 1F80033C */  lui        $v1, %hi(d_icr)
    /* A774 801E2774 3054638C */  lw         $v1, %lo(d_icr)($v1)
    /* A778 801E2778 00000000 */  nop
    /* A77C 801E277C 02006490 */  lbu        $a0, 0x2($v1)
    /* A780 801E2780 27100200 */  nor        $v0, $zero, $v0
    /* A784 801E2784 24208200 */  and        $a0, $a0, $v0
  .L801E2788:
    /* A788 801E2788 020064A0 */  sb         $a0, 0x2($v1)
    /* A78C 801E278C 1F80023C */  lui        $v0, %hi(d_icr)
    /* A790 801E2790 3054428C */  lw         $v0, %lo(d_icr)($v0)
    /* A794 801E2794 00000000 */  nop
    /* A798 801E2798 0000428C */  lw         $v0, 0x0($v0)
    /* A79C 801E279C 00000000 */  nop
    /* A7A0 801E27A0 1000A2AF */  sw         $v0, 0x10($sp)
    /* A7A4 801E27A4 80301000 */  sll        $a2, $s0, 2
    /* A7A8 801E27A8 0300C624 */  addiu      $a2, $a2, 0x3
    /* A7AC 801E27AC 01000324 */  addiu      $v1, $zero, 0x1
    /* A7B0 801E27B0 0418C300 */  sllv       $v1, $v1, $a2
    /* A7B4 801E27B4 801F053C */  lui        $a1, (0x1F801080 >> 16)
    /* A7B8 801E27B8 8010A534 */  ori        $a1, $a1, (0x1F801080 & 0xFFFF)
    /* A7BC 801E27BC 00111000 */  sll        $v0, $s0, 4
    /* A7C0 801E27C0 21284500 */  addu       $a1, $v0, $a1
    /* A7C4 801E27C4 1F80043C */  lui        $a0, %hi(d_pcr)
    /* A7C8 801E27C8 2C54848C */  lw         $a0, %lo(d_pcr)($a0)
    /* A7CC 801E27CC 00141300 */  sll        $v0, $s3, 16
    /* A7D0 801E27D0 0000868C */  lw         $a2, 0x0($a0)
    /* A7D4 801E27D4 25105400 */  or         $v0, $v0, $s4
    /* A7D8 801E27D8 2530C300 */  or         $a2, $a2, $v1
    /* A7DC 801E27DC 000086AC */  sw         $a2, 0x0($a0)
    /* A7E0 801E27E0 0000B2AC */  sw         $s2, 0x0($a1)
    /* A7E4 801E27E4 0400A524 */  addiu      $a1, $a1, 0x4
    /* A7E8 801E27E8 0000A2AC */  sw         $v0, 0x0($a1)
    /* A7EC 801E27EC 1F80033C */  lui        $v1, %hi(cd_reg0)
    /* A7F0 801E27F0 1454638C */  lw         $v1, %lo(cd_reg0)($v1)
    /* A7F4 801E27F4 00000000 */  nop
    /* A7F8 801E27F8 00006290 */  lbu        $v0, 0x0($v1)
    /* A7FC 801E27FC 00000000 */  nop
    /* A800 801E2800 40004230 */  andi       $v0, $v0, 0x40
    /* A804 801E2804 06004014 */  bnez       $v0, .L801E2820
    /* A808 801E2808 0400A524 */   addiu     $a1, $a1, 0x4
  .L801E280C:
    /* A80C 801E280C 00006290 */  lbu        $v0, 0x0($v1)
    /* A810 801E2810 00000000 */  nop
    /* A814 801E2814 40004230 */  andi       $v0, $v0, 0x40
    /* A818 801E2818 FCFF4010 */  beqz       $v0, .L801E280C
    /* A81C 801E281C 00000000 */   nop
  .L801E2820:
    /* A820 801E2820 0000B5AC */  sw         $s5, 0x0($a1)
    /* A824 801E2824 0000A28C */  lw         $v0, 0x0($a1)
    /* A828 801E2828 00000000 */  nop
    /* A82C 801E282C 1000A2AF */  sw         $v0, 0x10($sp)
    /* A830 801E2830 3000BF8F */  lw         $ra, 0x30($sp)
    /* A834 801E2834 2C00B58F */  lw         $s5, 0x2C($sp)
    /* A838 801E2838 2800B48F */  lw         $s4, 0x28($sp)
    /* A83C 801E283C 2400B38F */  lw         $s3, 0x24($sp)
    /* A840 801E2840 2000B28F */  lw         $s2, 0x20($sp)
    /* A844 801E2844 1C00B18F */  lw         $s1, 0x1C($sp)
    /* A848 801E2848 1800B08F */  lw         $s0, 0x18($sp)
    /* A84C 801E284C 3800BD27 */  addiu      $sp, $sp, 0x38
    /* A850 801E2850 0800E003 */  jr         $ra
    /* A854 801E2854 00000000 */   nop
endlabel dma_execute
