.text
	.align 4
.globl gst_videoscale_scale_nearest_x86
	.type	 gst_videoscale_scale_nearest_x86,@function
gst_videoscale_scale_nearest_x86:

	subl $8,%esp
	pushl %ebp
	pushl %edi
	pushl %esi
	movl 28(%esp),%ebp
	movl 24(%esp),%edx
	addl $28,%edx
	movl 24(%esp),%eax
	movl %edx,8220(%eax)
	movl $65536,12(%esp)
	movl 40(%esp),%ecx
	sall $16,%ecx
	movl %ecx,%eax
	cltd
	idivl 48(%esp)
	movl %eax,%ecx
	movl 48(%esp),%eax
	movl %eax,16(%esp)
	testl %eax,%eax
	jle .L92
	jmp .L100
	.p2align 4,,7
.L97:
	addl 36(%esp),%ebp
	addl $-65536,12(%esp)
.L100:
	cmpl $65536,12(%esp)
	jg .L97
	movl 32(%esp),%edi
	movl %ebp,%esi
	movl 24(%esp),%edx
	
        movl 8220(%edx), %eax
        call *%eax
           
	movl 44(%esp),%eax
	addl %eax,32(%esp)
	addl %ecx,12(%esp)
	decl 16(%esp)
	cmpl $0,16(%esp)
	jg .L100
.L92:
	popl %esi
	popl %edi
	popl %ebp
	addl $8,%esp
	ret
