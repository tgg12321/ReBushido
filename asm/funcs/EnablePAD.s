glabel EnablePAD
    /* 69760 80078F60 0A80093C */  lui        $t1, %hi(_patch_pad_enable)
    /* 69764 80078F64 2036298D */  lw         $t1, %lo(_patch_pad_enable)($t1)
    /* 69768 80078F68 00000000 */  nop
    /* 6976C 80078F6C 08002001 */  jr         $t1
    /* 69770 80078F70 00000000 */   nop
endlabel EnablePAD
