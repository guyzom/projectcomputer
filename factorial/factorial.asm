# factorial.asm
# Computes n! recursively.
# n is read from address 0x100 and the result is written to address 0x101.
#
#   int factorial(int n) {
#       if (n == 0) return 1;
#       return n * factorial(n - 1);
#   }

	add $sp, $zero, $imm1, 2048, 0		# initialise the stack pointer
	lw  $a0, $zero, $imm1, 0x100, 0		# $a0 = n
	jal $ra, $imm1, $zero, FACT, 0		# $v0 = factorial(n)
	sw  $v0, $zero, $imm1, 0x101, 0		# store the result at 0x101
	halt $zero, $zero, $zero, 0, 0		# done

FACT:
	bne $imm1, $a0, $zero, RECURSE, 0	# if n != 0 handle the recursive case
	add $v0, $zero, $imm1, 1, 0		# base case: 0! = 1
	beq $ra, $zero, $zero, 0, 0		# return to caller
RECURSE:
	sub $sp, $sp, $imm1, 2, 0		# reserve a frame of two words
	sw  $ra, $sp, $imm1, 0, 0		# save the return address
	sw  $a0, $sp, $imm1, 1, 0		# save n
	sub $a0, $a0, $imm1, 1, 0		# argument for the recursive call = n-1
	jal $ra, $imm1, $zero, FACT, 0		# $v0 = factorial(n-1)
	lw  $a0, $sp, $imm1, 1, 0		# restore n
	lw  $ra, $sp, $imm1, 0, 0		# restore the return address
	add $sp, $sp, $imm1, 2, 0		# release the frame
	mul $v0, $v0, $a0, 0, 0			# $v0 = n * factorial(n-1)
	beq $ra, $zero, $zero, 0, 0		# return to caller

	.word 0x100 8				# n = 8  (8! = 40320)
