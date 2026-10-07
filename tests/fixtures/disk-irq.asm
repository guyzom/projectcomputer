# Read sector 3 to 0x300 through DMA and resume after its irq1 handler.
    add $t0, $zero, $imm1, HANDLER, 0
    out $t0, $zero, $imm1, 6, 0
    add $t0, $zero, $imm1, 1, 0
    out $t0, $zero, $imm1, 1, 0
    add $t0, $zero, $imm1, 3, 0
    out $t0, $zero, $imm1, 15, 0
    add $t0, $zero, $imm1, 0x300, 0
    out $t0, $zero, $imm1, 16, 0
    add $t0, $zero, $imm1, 1, 0
    out $t0, $zero, $imm1, 14, 0
WAIT:
    beq $imm1, $v0, $zero, WAIT, 0
    in $s0, $zero, $imm1, 17, 0
    sw $s0, $zero, $imm1, 0x381, 0
    halt $zero, $zero, $zero, 0, 0
HANDLER:
    add $v0, $v0, $imm1, 1, 0
    in $s0, $zero, $imm1, 4, 0
    sw $s0, $zero, $imm1, 0x380, 0
    in $t1, $zero, $imm1, 7, 0
    sw $t1, $zero, $imm1, 0x382, 0
    out $zero, $zero, $imm1, 4, 0
    reti $zero, $zero, $zero, 0, 0
