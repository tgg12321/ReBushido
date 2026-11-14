glabel DisablePAD
    /* 69774 80078F74 0A80093C */  lui        $t1, %hi(_patch_pad_disable)
    /* 69778 80078F78 2436298D */  lw         $t1, %lo(_patch_pad_disable)($t1)
    /* 6977C 80078F7C 00000000 */  nop
    /* 69780 80078F80 08002001 */  jr         $t1
    /* 69784 80078F84 00000000 */   nop
endlabel DisablePAD
