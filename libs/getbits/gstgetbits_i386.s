	.p2align 4,,7
.globl _gst_getbits_i386
	.type	 _gst_getbits_i386,@function
_gst_getbits_i386:
	cmpl $0,8(%esp)
	jne .L39
	xorl %eax,%eax
	ret
.L39:
	movl 4(%esp),%edx
	movl (%edx),%ecx
	movl (%ecx),%eax
	bswap %eax
	movl 16(%edx),%ecx
        shll %cl, %eax
	movl 8(%esp),%ecx
	addl %ecx, 16(%edx)
	negl %ecx
	addl $32,%ecx
        shrl %cl, %eax
	movl 16(%edx),%ecx
	sarl $3,%ecx
	addl %ecx,(%edx)
	andl $7,16(%edx)
	ret

	.p2align 4,,7
.globl _gst_getbits_fast_i386
	.type	 _gst_getbits_fast_i386,@function
_gst_getbits_fast_i386:
	movl 4(%esp),%edx
	movl (%edx),%ecx
	movzbl 1(%ecx),%eax
       	movb (%ecx), %ah
	movl 16(%edx),%ecx
        shlw %cl, %ax
	movl 8(%esp),%ecx
	addl %ecx, 16(%edx)
	negl %ecx
	addl $16,%ecx
        shrl %cl, %eax
	movl 16(%edx),%ecx
	sarl $3,%ecx
	addl %ecx,(%edx)
	andl $7,16(%edx)
	ret

	.p2align 4,,7
.globl _gst_get1bit_i386
	.type	 _gst_get1bit_i386,@function
_gst_get1bit_i386:
	movl 4(%esp),%edx
	movl (%edx),%ecx
	movzbl (%ecx),%eax
	movl 16(%edx),%ecx
	incl %ecx
        rolb %cl, %al
	andb $1, %al
	movl %ecx, 16(%edx)
	andl $7,16(%edx)
	sarl $3,%ecx
	addl %ecx,(%edx)
	ret

	.p2align 4,,7
.globl _gst_showbits_i386
	.type	 _gst_showbits_i386,@function
_gst_showbits_i386:
	cmpl $0,8(%esp)
	jne .L40
	xorl %eax,%eax
	ret
.L40:
	movl 4(%esp),%edx
	movl (%edx),%ecx
       	movl (%ecx), %eax
	bswap %eax
	movl 16(%edx),%ecx
        shll %cl, %eax
	movl 8(%esp),%ecx
	negl %ecx
	addl $32,%ecx
        shrl %cl, %eax
	ret


	.p2align 4,,7
.globl _gst_flushbits_i386
	.type	 _gst_flushbits_i386,@function
_gst_flushbits_i386:
	movl 4(%esp),%ecx
	movl 16(%ecx),%eax
	addl 8(%esp),%eax
	movl %eax, %edx
	sarl $3,%eax
	addl %eax,(%ecx)
	andl $7, %edx
	movl %edx, 16(%ecx)
	ret


	.p2align 4,,7
.globl _gst_getbits_back_i386
	.type	 _gst_getbits_back_i386,@function
_gst_getbits_back_i386:
	movl 4(%esp),%edx
	movl 16(%edx),%ecx
	subl 8(%esp),%ecx
	movl %ecx, %eax
	sarl $3,%ecx
	addl %ecx,(%edx)
	andl $7,%eax
	movl %eax, 16(%edx)
	ret

