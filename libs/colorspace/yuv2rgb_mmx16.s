.globl mmx_80w
.data
	.align 4
	.type	 mmx_80w,@object
	.size	 mmx_80w,8
mmx_80w:
	.long 8388736
	.long 8388736
.globl mmx_10w
	.align 4
	.type	 mmx_10w,@object
	.size	 mmx_10w,8
mmx_10w:
	.long 269488144
	.long 269488144
.globl mmx_00ffw
	.align 4
	.type	 mmx_00ffw,@object
	.size	 mmx_00ffw,8
mmx_00ffw:
	.long 16711935
	.long 16711935
.globl mmx_Y_coeff
	.align 4
	.type	 mmx_Y_coeff,@object
	.size	 mmx_Y_coeff,8
mmx_Y_coeff:
	.long 624895295
	.long 624895295
.globl mmx_U_green
	.align 4
	.type	 mmx_U_green,@object
	.size	 mmx_U_green,8
mmx_U_green:
	.long -209849475
	.long -209849475
.globl mmx_U_blue
	.align 4
	.type	 mmx_U_blue,@object
	.size	 mmx_U_blue,8
mmx_U_blue:
	.long 1083392147
	.long 1083392147
.globl mmx_V_red
	.align 4
	.type	 mmx_V_red,@object
	.size	 mmx_V_red,8
mmx_V_red:
	.long 856830738
	.long 856830738
.globl mmx_V_green
	.align 4
	.type	 mmx_V_green,@object
	.size	 mmx_V_green,8
mmx_V_green:
	.long -436410884
	.long -436410884
.globl mmx_redmask
	.align 4
	.type	 mmx_redmask,@object
	.size	 mmx_redmask,8
mmx_redmask:
	.long -117901064
	.long -117901064
.globl mmx_grnmask
	.align 4
	.type	 mmx_grnmask,@object
	.size	 mmx_grnmask,8
mmx_grnmask:
	.long -50529028
	.long -50529028
.text
	.align 4
.globl gst_colorspace_yuv_to_bgr16_mmx
	.type	 gst_colorspace_yuv_to_bgr16_mmx,@function
gst_colorspace_yuv_to_bgr16_mmx:
	subl $8,%esp
	pushl %ebp
	pushl %edi
	pushl %esi
	movl 28(%esp),%edi
	movl 32(%esp),%ecx
	movl 36(%esp),%edx
	movl $1,%ebp
	movl 48(%esp),%esi
	sarl $1,%esi
	movl %esi,16(%esp)

	pxor      %mm4, %mm4      	# zero mm4
	
	movl %esi,12(%esp)
	sarl $2,12(%esp)

	movl 40(%esp),%esi

	.p2align 4,,7
.L68:

	movd      (%ecx), %mm0       	# Load 4 Cb       00 00 00 00 00 u3 u2 u1 u0
	movd      (%edx), %mm1       	# Load 4 Cr       00 00 00 00 00 v2 v1 v0
	movq      (%edi), %mm6       	# Load 8 Y        Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0
	
	movl 12(%esp),%eax

	.p2align 4,,7
.L74:
	punpcklbw %mm4, %mm0      	# scatter 4 Cb    00 u3 00 u2 00 u1 00 u0
	punpcklbw %mm4, %mm1      	# scatter 4 Cr    00 v3 00 v2 00 v1 00 v0
	psubsw    mmx_80w, %mm0    	# Cb -= 128
	psubsw    mmx_80w, %mm1    	# Cr -= 128
	psllw     $3, %mm0         	# Promote precision
	psllw     $3, %mm1         	# Promote precision
	movq      %mm0, %mm2      	# Copy 4 Cb       00 u3 00 u2 00 u1 00 u0
	movq      %mm1, %mm3      	# Copy 4 Cr       00 v3 00 v2 00 v1 00 v0
	pmulhw    mmx_U_green, %mm2	# Mul Cb with green coeff -> Cb green
	pmulhw    mmx_V_green, %mm3	# Mul Cr with green coeff -> Cr green
	pmulhw    mmx_U_blue, %mm0 	# Mul Cb -> Cblue 00 b3 00 b2 00 b1 00 b0
	pmulhw    mmx_V_red, %mm1  	# Mul Cr -> Cred  00 r3 00 r2 00 r1 00 r0
	paddsw    %mm3, %mm2      	# Cb green + Cr green -> Cgreen
	psubusb   mmx_10w, %mm6    	# Y -= 16
	movq      %mm6, %mm7      	# Copy 8 Y        Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0
	pand      mmx_00ffw, %mm6  	# get Y even      00 Y6 00 Y4 00 Y2 00 Y0
	psrlw     $8, %mm7         	# get Y odd       00 Y7 00 Y5 00 Y3 00 Y1
	psllw     $3, %mm6         	# Promote precision
	psllw     $3, %mm7         	# Promote precision
	pmulhw    mmx_Y_coeff, %mm6	# Mul 4 Y even    00 y6 00 y4 00 y2 00 y0
	pmulhw    mmx_Y_coeff, %mm7	# Mul 4 Y odd     00 y7 00 y5 00 y3 00 y1
	movq      %mm0, %mm3      	# Copy Cblue
	movq      %mm1, %mm4      	# Copy Cred
	movq      %mm2, %mm5      	# Copy Cgreen
	paddsw    %mm6, %mm0      	# Y even + Cblue  00 B6 00 B4 00 B2 00 B0
	paddsw    %mm7, %mm3      	# Y odd  + Cblue  00 B7 00 B5 00 B3 00 B1
	paddsw    %mm6, %mm1      	# Y even + Cred   00 R6 00 R4 00 R2 00 R0
	paddsw    %mm7, %mm4      	# Y odd  + Cred   00 R7 00 R5 00 R3 00 R1
	paddsw    %mm6, %mm2      	# Y even + Cgreen 00 G6 00 G4 00 G2 00 G0
	paddsw    %mm7, %mm5      	# Y odd  + Cgreen 00 G7 00 G5 00 G3 00 G1
	packuswb  %mm0, %mm0      	# B6 B4 B2 B0 | B6 B4 B2 B0
	packuswb  %mm1, %mm1      	# R6 R4 R2 R0 | R6 R4 R2 R0
	packuswb  %mm2, %mm2      	# G6 G4 G2 G0 | G6 G4 G2 G0
	packuswb  %mm3, %mm3      	# B7 B5 B3 B1 | B7 B5 B3 B1
	packuswb  %mm4, %mm4      	# R7 R5 R3 R1 | R7 R5 R3 R1
	packuswb  %mm5, %mm5      	# G7 G5 G3 G1 | G7 G5 G3 G1
	punpcklbw %mm3, %mm0      	#                 B7 B6 B5 B4 B3 B2 B1 B0
	punpcklbw %mm4, %mm1      	#                 R7 R6 R5 R4 R3 R2 R1 R0
	punpcklbw %mm5, %mm2      	#                 G7 G6 G5 G4 G3 G2 G1 G0
	pand      mmx_redmask, %mm0	# b7b6b5b4 b3_0_0_0 b7b6b5b4 b3_0_0_0
	pand      mmx_grnmask, %mm2	# g7g6g5g4 g3g2_0_0 g7g6g5g4 g3g2_0_0
	pand      mmx_redmask, %mm1	# r7r6r5r4 r3_0_0_0 r7r6r5r4 r3_0_0_0
	psrlw     $3,%mm0		#0_0_0_b7 b6b5b4b3 0_0_0_b7 b6b5b4b3
	pxor      %mm4, %mm4      	# zero mm4
	movq      %mm0, %mm5      	# Copy B7-B0
	movq      %mm2, %mm7      	# Copy G7-G0
	punpcklbw %mm4, %mm2      	#  0_0_0_0  0_0_0_0 g7g6g5g4 g3g2_0_0
	punpcklbw %mm1, %mm0      	# r7r6r5r4 r3_0_0_0 0_0_0_b7 b6b5b4b3
	psllw     $3,%mm2		#  0_0_0_0 0_g7g6g5 g4g3g2_0  0_0_0_0
	por       %mm2, %mm0      	# r7r6r5r4 r3g7g6g5 g4g3g2b7 b6b5b4b3
	movq      8(%edi), %mm6      	# Load 8 Y        Y7 Y6 Y5 Y4 Y3 Y2 Y1 Y0
	movq      %mm0, (%esi)       	# store pixel 0-3
	punpckhbw %mm4, %mm7      	#  0_0_0_0  0_0_0_0 g7g6g5g4 g3g2_0_0
	punpckhbw %mm1, %mm5      	# r7r6r5r4 r3_0_0_0 0_0_0_b7 b6b5b4b3
	psllw     $3,%mm7		#  0_0_0_0 0_g7g6g5 g4g3g2_0  0_0_0_0
	movd      4(%ecx), %mm0      	# Load 4 Cb       00 00 00 00 u3 u2 u1 u0
	por       %mm7, %mm5      	# r7r6r5r4 r3g7g6g5 g4g3g2b7 b6b5b4b3
	movd      4(%edx), %mm1      	# Load 4 Cr       00 00 00 00 v3 v2 v1 v0
	movq      %mm5, 8(%esi)      	# store pixel 4-7
	
	addl $8,%edi
	addl $4,%ecx
	addl $4,%edx
	addl $16,%esi
	decl %eax
	jnz .L74
.L72:
	xorl $1,%ebp
	jne .L76
	subl 16(%esp),%ecx
	subl 16(%esp),%edx
.L76:
	subl $1,44(%esp)
	jnz .L68

	emms
	
	popl %esi
	popl %edi
	popl %ebp
	addl $8,%esp
	ret
