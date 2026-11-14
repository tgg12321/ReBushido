# Reads back what collision_ProjectOnFace worked out: the point's place along
# the face's two axes, scaled down two bits.
#
# In, as a C function: where the two words go.
glabel collision_ReadProjection
    mfc2   $t0, $9
    mfc2   $t1, $10
    sra    $t0, $t0, 2
    sra    $t1, $t1, 2
    sw     $t0, 0($a0)
    sw     $t1, 0($a1)
    jr     $ra
    nop
    nop
    nop
    nop
