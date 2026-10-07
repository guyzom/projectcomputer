# The timer raises irq0 once. The handler stops it and acknowledges the status.
    add $t0, $zero, $imm1, HANDLER, 0
    out $t0, $zero, $imm1, 6, 0
    add $t0, $zero, $imm1, 1, 0
    out $t0, $zero, $imm1, 0, 0
    add $t0, $zero, $imm1, 3, 0
    out $t0, $zero, $imm1, 13, 0
    add $t0, $zero, $imm1, 1, 0
    out $t0, $zero, $imm1, 11, 0
WAIT:
    beq $imm1, $v0, $zero, WAIT, 0
    add $s0, $zero, $imm1, 123, 0
    sw $s0, $zero, $imm1, 0x300, 0
    halt $zero, $zero, $zero, 0, 0
HANDLER:
    out $zero, $zero, $imm1, 11, 0
    add $v0, $v0, $imm1, 1, 0
    in $t1, $zero, $imm1, 7, 0
    sw $t1, $zero, $imm1, 0x301, 0
    out $zero, $zero, $imm1, 3, 0
    reti $zero, $zero, $zero, 0, 0
