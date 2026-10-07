# circle.asm
# Draws a filled white circle on the monitor.
# The centre offset inside the frame buffer is read from 0x100 and the radius
# (in pixels) from 0x101. The circle is assumed to fit entirely on the screen.
#
# A pixel at (row, col) has the frame buffer offset row*256 + col, so the
# centre coordinates are cx = centre & 0xFF and cy = centre >> 8.

	lw  $a0, $zero, $imm1, 0x100, 0		# $a0 = centre offset (temporary)
	and $s0, $a0, $imm1, 0xFF, 0		# $s0 = cx = centre & 0xFF
	srl $s1, $a0, $imm1, 8, 0		# $s1 = cy = centre >> 8
	lw  $a0, $zero, $imm1, 0x101, 0		# $a0 = radius (temporary)
	mul $gp, $a0, $a0, 0, 0			# $gp = r*r
	add $t0, $s1, $a0, 0, 0			# $t0 = cy + r  (last row)
	sub $t1, $s0, $a0, 0, 0			# $t1 = cx - r  (first column)
	add $t2, $s0, $a0, 0, 0			# $t2 = cx + r  (last column)
	sub $a0, $s1, $a0, 0, 0			# $a0 = row = cy - r  (first row)
ROW:
	bgt $imm1, $a0, $t0, END, 0		# stop once row passes cy + r
	add $a1, $t1, $zero, 0, 0		# $a1 = col = cx - r
COL:
	bgt $imm1, $a1, $t2, NEXT_ROW, 0	# end of this row once col passes cx + r
	sub $a2, $a1, $s0, 0, 0			# dx = col - cx
	mul $a2, $a2, $a2, 0, 0			# dx*dx
	sub $a3, $a0, $s1, 0, 0			# dy = row - cy
	mul $a3, $a3, $a3, 0, 0			# dy*dy
	add $a2, $a2, $a3, 0, 0			# dx*dx + dy*dy
	bgt $imm1, $a2, $gp, SKIP, 0		# outside the circle -> do not draw
	sll $v0, $a0, $imm1, 8, 0		# row << 8
	add $v0, $v0, $a1, 0, 0			# offset = row*256 + col
	out $v0, $zero, $imm1, 20, 0		# monitoraddr = offset
	add $v0, $zero, $imm1, 255, 0		# white
	out $v0, $zero, $imm1, 21, 0		# monitordata = 255
	add $v0, $zero, $imm1, 1, 0		# draw command
	out $v0, $zero, $imm1, 22, 0		# monitorcmd = 1 -> write the pixel
SKIP:
	add $a1, $a1, $imm1, 1, 0		# col++
	beq $imm1, $zero, $zero, COL, 0		# continue along the row
NEXT_ROW:
	add $a0, $a0, $imm1, 1, 0		# row++
	beq $imm1, $zero, $zero, ROW, 0		# continue to the next row
END:
	halt $zero, $zero, $zero, 0, 0		# done

	.word 0x100 0x8080			# centre = (128,128) -> 128*256 + 128
	.word 0x101 50				# radius = 50 pixels
