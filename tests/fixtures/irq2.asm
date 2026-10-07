# Enable one external interrupt; record its return address and resume the caller.
    add $t0, $zero, $imm1, HANDLER, 0
    out $t0, $zero, $imm1, 6, 0
    add $t0, $zero, $imm1, 1, 0
    out $t0, $zero, $imm1, 2, 0
WAIT:
    beq $imm1, $v0, $zero, WAIT, 0
    add $s0, $zero, $imm1, 123, 0
    sw $s0, $zero, $imm1, 0x300, 0
    halt $zero, $zero, $zero, 0, 0
HANDLER:
    add $v0, $v0, $imm1, 1, 0
    in $t1, $zero, $imm1, 7, 0
    sw $t1, $zero, $imm1, 0x301, 0
    out $zero, $zero, $imm1, 5, 0
    reti $zero, $zero, $zero, 0, 0
