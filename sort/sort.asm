# sort.asm
# Bubble sort of 16 numbers in descending order.
# The array lives at addresses 0x100..0x10F and is sorted in place.

	add $s0, $zero, $imm1, 0x100, 0		# $s0 = base address of the array
	add $t2, $zero, $imm1, 15, 0		# $t2 = 15 (used as the loop bound)
	add $a3, $zero, $zero, 0, 0		# $a3 = i, the outer pass counter
OUTER:
	bge $imm1, $a3, $t2, DONE, 0		# after 15 passes the array is sorted
	add $t1, $zero, $zero, 0, 0		# $t1 = j = 0
INNER:
	bge $imm1, $t1, $t2, NEXT_I, 0		# stop the pass once j reaches 15
	add $a0, $s0, $t1, 0, 0			# $a0 = address of element j
	lw  $a1, $a0, $zero, 0, 0		# $a1 = arr[j]
	lw  $a2, $a0, $imm1, 1, 0		# $a2 = arr[j+1]
	bge $imm1, $a1, $a2, NO_SWAP, 0		# already ordered if arr[j] >= arr[j+1]
	sw  $a2, $a0, $zero, 0, 0		# swap: arr[j]   = old arr[j+1]
	sw  $a1, $a0, $imm1, 1, 0		# swap: arr[j+1] = old arr[j]
NO_SWAP:
	add $t1, $t1, $imm1, 1, 0		# j++
	beq $imm1, $zero, $zero, INNER, 0	# continue the inner loop
NEXT_I:
	add $a3, $a3, $imm1, 1, 0		# i++
	beq $imm1, $zero, $zero, OUTER, 0	# continue the outer loop
DONE:
	halt $zero, $zero, $zero, 0, 0		# done

# initial (unsorted) contents of the array
	.array 0x100 45, 12, 78, 3, 99, 21, 6, 88, 34, 17, 55, 2, 63, 29, 71, 40
