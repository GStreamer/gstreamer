.data
	.align 4
	.type	 rounder0,@object
rounder0:
	.long 65536
	.long 65536
	.size	 rounder0,8
	.align 4
	.type	 rounder4,@object
rounder4:
	.long 1024
	.long 1024
	.size	 rounder4,8
	.align 4
	.type	 rounder1,@object
rounder1:
	.long 3597
	.long 3597
	.size	 rounder1,8
	.align 4
	.type	 rounder7,@object
rounder7:
	.long 512
	.long 512
	.size	 rounder7,8
	.align 4
	.type	 rounder2,@object
rounder2:
	.long 2260
	.long 2260
	.size	 rounder2,8
	.align 4
	.type	 rounder6,@object
rounder6:
	.long 512
	.long 512
	.size	 rounder6,8
	.align 4
	.type	 rounder3,@object
rounder3:
	.long 1203
	.long 1203
	.size	 rounder3,8
	.align 4
	.type	 rounder5,@object
rounder5:
	.long 120
	.long 120
	.size	 rounder5,8
	.align 2
	.type	 _T1.46,@object
_T1.46:
	.value 13036
	.value 13036
	.value 13036
	.value 13036
	.align 2
	.type	 _T2.47,@object
_T2.47:
	.value 27146
	.value 27146
	.value 27146
	.value 27146
	.align 2
	.type	 _T3.48,@object
_T3.48:
	.value -21746
	.value -21746
	.value -21746
	.value -21746
	.align 2
	.type	 _C4.49,@object
_C4.49:
	.value 23170
	.value 23170
	.value 23170
	.value 23170
	.local	scratch0.50
	.comm	scratch0.50,8,4
	.local	scratch1.51
	.comm	scratch1.51,8,4
	.align 2
	.type	 table04.54,@object
table04.54:
	.value 16384
	.value 21407
	.value -16384
	.value -21407
	.value 16384
	.value 8867
	.value 16384
	.value 8867
	.value 22725
	.value 19266
	.value -22725
	.value -12873
	.value 12873
	.value 4520
	.value 19266
	.value -4520
	.value 16384
	.value -8867
	.value 16384
	.value -8867
	.value -16384
	.value 21407
	.value 16384
	.value -21407
	.value 12873
	.value -22725
	.value 19266
	.value -22725
	.value 4520
	.value 19266
	.value 4520
	.value -12873
	.align 2
	.type	 table17.55,@object
table17.55:
	.value 22725
	.value 29692
	.value -22725
	.value -29692
	.value 22725
	.value 12299
	.value 22725
	.value 12299
	.value 31521
	.value 26722
	.value -31521
	.value -17855
	.value 17855
	.value 6270
	.value 26722
	.value -6270
	.value 22725
	.value -12299
	.value 22725
	.value -12299
	.value -22725
	.value 29692
	.value 22725
	.value -29692
	.value 17855
	.value -31521
	.value 26722
	.value -31521
	.value 6270
	.value 26722
	.value 6270
	.value -17855
	.align 2
	.type	 table26.56,@object
table26.56:
	.value 21407
	.value 27969
	.value -21407
	.value -27969
	.value 21407
	.value 11585
	.value 21407
	.value 11585
	.value 29692
	.value 25172
	.value -29692
	.value -16819
	.value 16819
	.value 5906
	.value 25172
	.value -5906
	.value 21407
	.value -11585
	.value 21407
	.value -11585
	.value -21407
	.value 27969
	.value 21407
	.value -27969
	.value 16819
	.value -29692
	.value 25172
	.value -29692
	.value 5906
	.value 25172
	.value 5906
	.value -16819
	.align 2
	.type	 table35.57,@object
table35.57:
	.value 19266
	.value 25172
	.value -19266
	.value -25172
	.value 19266
	.value 10426
	.value 19266
	.value 10426
	.value 26722
	.value 22654
	.value -26722
	.value -15137
	.value 15137
	.value 5315
	.value 22654
	.value -5315
	.value 19266
	.value -10426
	.value 19266
	.value -10426
	.value -19266
	.value 25172
	.value 19266
	.value -25172
	.value 15137
	.value -26722
	.value 22654
	.value -26722
	.value 5315
	.value 22654
	.value 5315
	.value -15137
.text
	.align 4
.globl gst_idct_sse_idct
	.type	 gst_idct_sse_idct,@function
gst_idct_sse_idct:
	subl $8,%esp
	pushl %ebp
	pushl %edi
	pushl %esi
	pushl %ebx
	call .L51
.L51:
	popl %ebx
	addl $_GLOBAL_OFFSET_TABLE_+[.-.L51],%ebx
	movl 28(%esp),%edx
	leal table04.54@GOTOFF(%ebx),%eax
	movq (%edx), %mm2
	movq 8(%edx), %mm5
	movq %mm2, %mm0
	movq (%eax), %mm3
	movq %mm5, %mm6
	movq 8(%eax), %mm4
	pmaddwd %mm0, %mm3
	pshufw $78, %mm2, %mm2
	leal rounder0@GOTOFF(%ebx),%ecx
	movq 16(%eax), %mm1
	pmaddwd %mm2, %mm4
	pmaddwd 32(%eax), %mm0
	pshufw $78, %mm6, %mm6
	movq 24(%eax), %mm7
	pmaddwd %mm5, %mm1
	paddd (%ecx), %mm3
	pmaddwd %mm6, %mm7
	pmaddwd 40(%eax), %mm2
	paddd %mm4, %mm3
	pmaddwd 48(%eax), %mm5
	movq %mm3, %mm4
	pmaddwd 56(%eax), %mm6
	paddd %mm7, %mm1
	paddd (%ecx), %mm0
	psubd %mm1, %mm3
	psrad $11, %mm3
	paddd %mm4, %mm1
	paddd %mm2, %mm0
	psrad $11, %mm1
	paddd %mm6, %mm5
	movq %mm0, %mm4
	paddd %mm5, %mm0
	psubd %mm5, %mm4
	movq 64(%edx), %mm2
	psrad $11, %mm0
	movq 72(%edx), %mm5
	psrad $11, %mm4
	packssdw %mm0, %mm1
	movq %mm5, %mm6
	packssdw %mm3, %mm4
	movq %mm2, %mm0
	movq %mm1, (%edx)
	pshufw $177, %mm4, %mm4
	movq (%eax), %mm3
	movq %mm4, 8(%edx)
	pmaddwd %mm0, %mm3
	movq 8(%eax), %mm4
	pshufw $78, %mm2, %mm2
	leal rounder4@GOTOFF(%ebx),%ecx
	movq 16(%eax), %mm1
	pmaddwd %mm2, %mm4
	pmaddwd 32(%eax), %mm0
	pshufw $78, %mm6, %mm6
	movq 24(%eax), %mm7
	pmaddwd %mm5, %mm1
	paddd (%ecx), %mm3
	pmaddwd %mm6, %mm7
	pmaddwd 40(%eax), %mm2
	paddd %mm4, %mm3
	pmaddwd 48(%eax), %mm5
	movq %mm3, %mm4
	pmaddwd 56(%eax), %mm6
	paddd %mm7, %mm1
	paddd (%ecx), %mm0
	psubd %mm1, %mm3
	psrad $11, %mm3
	paddd %mm4, %mm1
	paddd %mm2, %mm0
	psrad $11, %mm1
	paddd %mm6, %mm5
	movq %mm0, %mm4
	paddd %mm5, %mm0
	psubd %mm5, %mm4
	leal table17.55@GOTOFF(%ebx),%eax
	movq 16(%edx), %mm2
	psrad $11, %mm0
	movq 24(%edx), %mm5
	psrad $11, %mm4
	packssdw %mm0, %mm1
	movq %mm5, %mm6
	packssdw %mm3, %mm4
	movq %mm2, %mm0
	movq %mm1, 64(%edx)
	pshufw $177, %mm4, %mm4
	movq (%eax), %mm3
	movq %mm4, 72(%edx)
	pmaddwd %mm0, %mm3
	movq 8(%eax), %mm4
	pshufw $78, %mm2, %mm2
	leal rounder1@GOTOFF(%ebx),%ecx
	movq 16(%eax), %mm1
	pmaddwd %mm2, %mm4
	pmaddwd 32(%eax), %mm0
	pshufw $78, %mm6, %mm6
	movq 24(%eax), %mm7
	pmaddwd %mm5, %mm1
	paddd (%ecx), %mm3
	pmaddwd %mm6, %mm7
	pmaddwd 40(%eax), %mm2
	paddd %mm4, %mm3
	pmaddwd 48(%eax), %mm5
	movq %mm3, %mm4
	pmaddwd 56(%eax), %mm6
	paddd %mm7, %mm1
	paddd (%ecx), %mm0
	psubd %mm1, %mm3
	psrad $11, %mm3
	paddd %mm4, %mm1
	paddd %mm2, %mm0
	psrad $11, %mm1
	paddd %mm6, %mm5
	movq %mm0, %mm4
	paddd %mm5, %mm0
	psubd %mm5, %mm4
	movq 112(%edx), %mm2
	psrad $11, %mm0
	movq 120(%edx), %mm5
	psrad $11, %mm4
	packssdw %mm0, %mm1
	movq %mm5, %mm6
	packssdw %mm3, %mm4
	movq %mm2, %mm0
	movq %mm1, 16(%edx)
	pshufw $177, %mm4, %mm4
	movq (%eax), %mm3
	movq %mm4, 24(%edx)
	pmaddwd %mm0, %mm3
	movq 8(%eax), %mm4
	pshufw $78, %mm2, %mm2
	leal rounder7@GOTOFF(%ebx),%ecx
	movq 16(%eax), %mm1
	pmaddwd %mm2, %mm4
	pmaddwd 32(%eax), %mm0
	pshufw $78, %mm6, %mm6
	movq 24(%eax), %mm7
	pmaddwd %mm5, %mm1
	paddd (%ecx), %mm3
	pmaddwd %mm6, %mm7
	pmaddwd 40(%eax), %mm2
	paddd %mm4, %mm3
	pmaddwd 48(%eax), %mm5
	movq %mm3, %mm4
	pmaddwd 56(%eax), %mm6
	paddd %mm7, %mm1
	paddd (%ecx), %mm0
	psubd %mm1, %mm3
	psrad $11, %mm3
	paddd %mm4, %mm1
	paddd %mm2, %mm0
	psrad $11, %mm1
	paddd %mm6, %mm5
	movq %mm0, %mm4
	paddd %mm5, %mm0
	psubd %mm5, %mm4
	leal table26.56@GOTOFF(%ebx),%eax
	movq 32(%edx), %mm2
	psrad $11, %mm0
	movq 40(%edx), %mm5
	psrad $11, %mm4
	packssdw %mm0, %mm1
	movq %mm5, %mm6
	packssdw %mm3, %mm4
	movq %mm2, %mm0
	movq %mm1, 112(%edx)
	pshufw $177, %mm4, %mm4
	movq (%eax), %mm3
	movq %mm4, 120(%edx)
	pmaddwd %mm0, %mm3
	movq 8(%eax), %mm4
	pshufw $78, %mm2, %mm2
	leal rounder2@GOTOFF(%ebx),%ecx
	movq 16(%eax), %mm1
	pmaddwd %mm2, %mm4
	pmaddwd 32(%eax), %mm0
	pshufw $78, %mm6, %mm6
	movq 24(%eax), %mm7
	pmaddwd %mm5, %mm1
	paddd (%ecx), %mm3
	pmaddwd %mm6, %mm7
	pmaddwd 40(%eax), %mm2
	paddd %mm4, %mm3
	pmaddwd 48(%eax), %mm5
	movq %mm3, %mm4
	pmaddwd 56(%eax), %mm6
	paddd %mm7, %mm1
	paddd (%ecx), %mm0
	psubd %mm1, %mm3
	psrad $11, %mm3
	paddd %mm4, %mm1
	paddd %mm2, %mm0
	psrad $11, %mm1
	paddd %mm6, %mm5
	movq %mm0, %mm4
	paddd %mm5, %mm0
	psubd %mm5, %mm4
	movq 96(%edx), %mm2
	psrad $11, %mm0
	movq 104(%edx), %mm5
	psrad $11, %mm4
	packssdw %mm0, %mm1
	movq %mm5, %mm6
	packssdw %mm3, %mm4
	movq %mm2, %mm0
	movq %mm1, 32(%edx)
	pshufw $177, %mm4, %mm4
	movq (%eax), %mm3
	movq %mm4, 40(%edx)
	pmaddwd %mm0, %mm3
	movq 8(%eax), %mm4
	pshufw $78, %mm2, %mm2
	leal rounder6@GOTOFF(%ebx),%ecx
	movq 16(%eax), %mm1
	pmaddwd %mm2, %mm4
	pmaddwd 32(%eax), %mm0
	pshufw $78, %mm6, %mm6
	movq 24(%eax), %mm7
	pmaddwd %mm5, %mm1
	paddd (%ecx), %mm3
	pmaddwd %mm6, %mm7
	pmaddwd 40(%eax), %mm2
	paddd %mm4, %mm3
	pmaddwd 48(%eax), %mm5
	movq %mm3, %mm4
	pmaddwd 56(%eax), %mm6
	paddd %mm7, %mm1
	paddd (%ecx), %mm0
	psubd %mm1, %mm3
	psrad $11, %mm3
	paddd %mm4, %mm1
	paddd %mm2, %mm0
	psrad $11, %mm1
	paddd %mm6, %mm5
	movq %mm0, %mm4
	paddd %mm5, %mm0
	psubd %mm5, %mm4
	leal table35.57@GOTOFF(%ebx),%eax
	movq 48(%edx), %mm2
	psrad $11, %mm0
	movq 56(%edx), %mm5
	psrad $11, %mm4
	packssdw %mm0, %mm1
	movq %mm5, %mm6
	packssdw %mm3, %mm4
	movq %mm2, %mm0
	movq %mm1, 96(%edx)
	pshufw $177, %mm4, %mm4
	movq (%eax), %mm3
	movq %mm4, 104(%edx)
	pmaddwd %mm0, %mm3
	movq 8(%eax), %mm4
	pshufw $78, %mm2, %mm2
	leal rounder3@GOTOFF(%ebx),%ecx
	movq 16(%eax), %mm1
	pmaddwd %mm2, %mm4
	pmaddwd 32(%eax), %mm0
	pshufw $78, %mm6, %mm6
	movq 24(%eax), %mm7
	pmaddwd %mm5, %mm1
	paddd (%ecx), %mm3
	pmaddwd %mm6, %mm7
	pmaddwd 40(%eax), %mm2
	paddd %mm4, %mm3
	pmaddwd 48(%eax), %mm5
	movq %mm3, %mm4
	pmaddwd 56(%eax), %mm6
	paddd %mm7, %mm1
	paddd (%ecx), %mm0
	psubd %mm1, %mm3
	psrad $11, %mm3
	paddd %mm4, %mm1
	paddd %mm2, %mm0
	psrad $11, %mm1
	paddd %mm6, %mm5
	movq %mm0, %mm4
	paddd %mm5, %mm0
	psubd %mm5, %mm4
	movq 80(%edx), %mm2
	psrad $11, %mm0
	movq 88(%edx), %mm5
	psrad $11, %mm4
	packssdw %mm0, %mm1
	movq %mm5, %mm6
	packssdw %mm3, %mm4
	movq %mm2, %mm0
	movq %mm1, 48(%edx)
	pshufw $177, %mm4, %mm4
	movq (%eax), %mm3
	movq %mm4, 56(%edx)
	pmaddwd %mm0, %mm3
	movq 8(%eax), %mm4
	pshufw $78, %mm2, %mm2
	leal rounder5@GOTOFF(%ebx),%ecx
	movq 16(%eax), %mm1
	pmaddwd %mm2, %mm4
	pmaddwd 32(%eax), %mm0
	pshufw $78, %mm6, %mm6
	movq 24(%eax), %mm7
	pmaddwd %mm5, %mm1
	paddd (%ecx), %mm3
	pmaddwd %mm6, %mm7
	pmaddwd 40(%eax), %mm2
	paddd %mm4, %mm3
	pmaddwd 48(%eax), %mm5
	movq %mm3, %mm4
	pmaddwd 56(%eax), %mm6
	paddd %mm7, %mm1
	paddd (%ecx), %mm0
	psubd %mm1, %mm3
	psrad $11, %mm3
	paddd %mm4, %mm1
	paddd %mm2, %mm0
	psrad $11, %mm1
	paddd %mm6, %mm5
	movq %mm0, %mm4
	paddd %mm5, %mm0
	psubd %mm5, %mm4
	psrad $11, %mm0
	psrad $11, %mm4
	packssdw %mm0, %mm1
	packssdw %mm3, %mm4
	movq %mm1, 80(%edx)
	pshufw $177, %mm4, %mm4
	movq %mm4, 88(%edx)
	leal _T1.46@GOTOFF(%ebx),%edi
	movq (%edi), %mm0
	movq 16(%edx), %mm1
	movq %mm0, %mm2
	movq 112(%edx), %mm4
	pmulhw %mm1, %mm0
	leal _T3.48@GOTOFF(%ebx),%esi
	movl %esi,16(%esp)
	movq (%esi), %mm5
	pmulhw %mm4, %mm2
	movq 80(%edx), %mm6
	movq %mm5, %mm7
	movq 48(%edx), %mm3
	psubsw %mm4, %mm0
	leal _T2.47@GOTOFF(%ebx),%ecx
	movq (%ecx), %mm4
	pmulhw %mm3, %mm5
	paddsw %mm2, %mm1
	pmulhw %mm6, %mm7
	movq %mm4, %mm2
	paddsw %mm3, %mm5
	pmulhw 32(%edx), %mm4
	paddsw %mm6, %mm7
	psubsw %mm6, %mm5
	paddsw %mm3, %mm7
	movq 96(%edx), %mm3
	movq %mm0, %mm6
	pmulhw %mm3, %mm2
	psubsw %mm5, %mm0
	psubsw %mm3, %mm4
	paddsw %mm6, %mm5
	leal scratch0.50@GOTOFF(%ebx),%esi
	movl %esi,20(%esp)
	movq %mm0, scratch0.50@GOTOFF(%ebx)
	movq %mm1, %mm6
	paddsw 32(%edx), %mm2
	paddsw %mm7, %mm6
	psubsw %mm7, %mm1
	movq %mm1, %mm7
	movq (%edx), %mm3
	paddsw %mm5, %mm1
	leal _C4.49@GOTOFF(%ebx),%eax
	movq (%eax), %mm0
	psubsw %mm5, %mm7
	leal scratch1.51@GOTOFF(%ebx),%ebp
	movq %mm6, scratch1.51@GOTOFF(%ebx)
	pmulhw %mm0, %mm1
	movq %mm4, %mm6
	pmulhw %mm0, %mm7
	movq 64(%edx), %mm5
	movq %mm3, %mm0
	psubsw %mm5, %mm3
	paddsw %mm5, %mm0
	paddsw %mm3, %mm4
	movq %mm0, %mm5
	psubsw %mm6, %mm3
	paddsw %mm2, %mm5
	paddsw %mm1, %mm1
	psubsw %mm2, %mm0
	paddsw %mm7, %mm7
	movq %mm3, %mm2
	movq %mm4, %mm6
	paddsw %mm7, %mm3
	psraw $6, %mm3
	paddsw %mm1, %mm4
	psraw $6, %mm4
	psubsw %mm1, %mm6
	movq (%ebp), %mm1
	psubsw %mm7, %mm2
	psraw $6, %mm6
	movq %mm5, %mm7
	movq %mm4, 16(%edx)
	psraw $6, %mm2
	movq %mm3, 32(%edx)
	paddsw %mm1, %mm5
	movq (%esi), %mm4
	psubsw %mm1, %mm7
	psraw $6, %mm5
	movq %mm0, %mm3
	movq %mm2, 80(%edx)
	psubsw %mm4, %mm3
	psraw $6, %mm7
	paddsw %mm0, %mm4
	movq %mm5, (%edx)
	psraw $6, %mm3
	movq %mm6, 96(%edx)
	psraw $6, %mm4
	movq %mm7, 112(%edx)
	movq %mm3, 64(%edx)
	movq %mm4, 48(%edx)
	movq (%edi), %mm0
	movq 24(%edx), %mm1
	movq %mm0, %mm2
	movq 120(%edx), %mm4
	pmulhw %mm1, %mm0
	movl 16(%esp),%esi
	movq (%esi), %mm5
	pmulhw %mm4, %mm2
	movq 88(%edx), %mm6
	movq %mm5, %mm7
	movq 56(%edx), %mm3
	psubsw %mm4, %mm0
	movq (%ecx), %mm4
	pmulhw %mm3, %mm5
	paddsw %mm2, %mm1
	pmulhw %mm6, %mm7
	movq %mm4, %mm2
	paddsw %mm3, %mm5
	pmulhw 40(%edx), %mm4
	paddsw %mm6, %mm7
	psubsw %mm6, %mm5
	paddsw %mm3, %mm7
	movq 104(%edx), %mm3
	movq %mm0, %mm6
	pmulhw %mm3, %mm2
	psubsw %mm5, %mm0
	psubsw %mm3, %mm4
	paddsw %mm6, %mm5
	movq %mm0, scratch0.50@GOTOFF(%ebx)
	movq %mm1, %mm6
	paddsw 40(%edx), %mm2
	paddsw %mm7, %mm6
	psubsw %mm7, %mm1
	movq %mm1, %mm7
	movq 8(%edx), %mm3
	paddsw %mm5, %mm1
	movq (%eax), %mm0
	psubsw %mm5, %mm7
	movq %mm6, scratch1.51@GOTOFF(%ebx)
	pmulhw %mm0, %mm1
	movq %mm4, %mm6
	pmulhw %mm0, %mm7
	movq 72(%edx), %mm5
	movq %mm3, %mm0
	psubsw %mm5, %mm3
	paddsw %mm5, %mm0
	paddsw %mm3, %mm4
	movq %mm0, %mm5
	psubsw %mm6, %mm3
	paddsw %mm2, %mm5
	paddsw %mm1, %mm1
	psubsw %mm2, %mm0
	paddsw %mm7, %mm7
	movq %mm3, %mm2
	movq %mm4, %mm6
	paddsw %mm7, %mm3
	psraw $6, %mm3
	paddsw %mm1, %mm4
	psraw $6, %mm4
	psubsw %mm1, %mm6
	movq (%ebp), %mm1
	psubsw %mm7, %mm2
	psraw $6, %mm6
	movq %mm5, %mm7
	movq %mm4, 24(%edx)
	psraw $6, %mm2
	movq %mm3, 40(%edx)
	paddsw %mm1, %mm5
	movl 20(%esp),%esi
	movq (%esi), %mm4
	psubsw %mm1, %mm7
	psraw $6, %mm5
	movq %mm0, %mm3
	movq %mm2, 88(%edx)
	psubsw %mm4, %mm3
	psraw $6, %mm7
	paddsw %mm0, %mm4
	movq %mm5, 8(%edx)
	psraw $6, %mm3
	movq %mm6, 104(%edx)
	psraw $6, %mm4
	movq %mm7, 120(%edx)
	movq %mm3, 72(%edx)
	movq %mm4, 56(%edx)
	popl %ebx
	popl %esi
	popl %edi
	popl %ebp
	addl $8,%esp
	ret
