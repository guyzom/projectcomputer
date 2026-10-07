# disktest.asm
# Sums the contents of disk sectors 0..7 word by word and stores the result in
# sector 8:  sector8[i] = sector0[i] + sector1[i] + ... + sector7[i]
#
# Sectors are transferred through DMA into a memory buffer; the program polls
# diskstatus and never relies on interrupts.

	add $s0, $zero, $imm1, 0x800, 0		# $s0 = read buffer (one sector)
	add $s1, $zero, $imm1, 0x900, 0		# $s1 = accumulator (128 words)
	add $gp, $zero, $imm1, 128, 0		# $gp = words per sector

	add $t1, $zero, $zero, 0, 0		# clear the accumulator: i = 0
ZERO:
	bge $imm1, $t1, $gp, ZERO_DONE, 0	# until i == 128
	add $a0, $s1, $t1, 0, 0			# &acc[i]
	sw  $zero, $a0, $zero, 0, 0		# acc[i] = 0
	add $t1, $t1, $imm1, 1, 0		# i++
	beq $imm1, $zero, $zero, ZERO, 0
ZERO_DONE:

	add $t2, $zero, $zero, 0, 0		# s = 0 (sector index)
SECTOR:
	bge $imm1, $t2, $imm2, WRITEBACK, 8	# once all 8 sectors are added, write out
WAIT_READ:
	in  $t0, $zero, $imm1, 17, 0		# read diskstatus
	bne $imm1, $t0, $zero, WAIT_READ, 0	# wait until the disk is free
	out $t2, $zero, $imm1, 15, 0		# disksector = s
	out $s0, $zero, $imm1, 16, 0		# diskbuffer = read buffer
	add $a0, $zero, $imm1, 1, 0		# 1 = read sector
	out $a0, $zero, $imm1, 14, 0		# diskcmd = read
BUSY_READ:
	in  $t0, $zero, $imm1, 17, 0		# poll diskstatus
	bne $imm1, $t0, $zero, BUSY_READ, 0	# wait until the transfer completes

	add $t1, $zero, $zero, 0, 0		# add the sector into the accumulator
SUM:
	bge $imm1, $t1, $gp, SUM_DONE, 0	# until i == 128
	add $a0, $s0, $t1, 0, 0			# &buf[i]
	lw  $a1, $a0, $zero, 0, 0		# buf[i]
	add $a0, $s1, $t1, 0, 0			# &acc[i]
	lw  $a2, $a0, $zero, 0, 0		# acc[i]
	add $a2, $a2, $a1, 0, 0			# acc[i] + buf[i]
	sw  $a2, $a0, $zero, 0, 0		# store it back
	add $t1, $t1, $imm1, 1, 0		# i++
	beq $imm1, $zero, $zero, SUM, 0
SUM_DONE:
	add $t2, $t2, $imm1, 1, 0		# s++
	beq $imm1, $zero, $zero, SECTOR, 0

WRITEBACK:
WAIT_WRITE:
	in  $t0, $zero, $imm1, 17, 0		# read diskstatus
	bne $imm1, $t0, $zero, WAIT_WRITE, 0	# wait until the disk is free
	add $a0, $zero, $imm1, 8, 0		# target sector 8
	out $a0, $zero, $imm1, 15, 0		# disksector = 8
	out $s1, $zero, $imm1, 16, 0		# diskbuffer = accumulator
	add $a0, $zero, $imm1, 2, 0		# 2 = write sector
	out $a0, $zero, $imm1, 14, 0		# diskcmd = write
BUSY_WRITE:
	in  $t0, $zero, $imm1, 17, 0		# poll diskstatus
	bne $imm1, $t0, $zero, BUSY_WRITE, 0	# wait until the write completes
	halt $zero, $zero, $zero, 0, 0		# done
