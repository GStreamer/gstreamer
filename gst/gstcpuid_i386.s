	.text
.globl gst_cpuid_i386
	.type	 gst_cpuid_i386,@function
gst_cpuid_i386:
	pushl %ebp
	movl %esp,%ebp
	pushl %edi
	pushl %ebx
	pushl %ecx
	pushl %edx
	movl 8(%ebp),%eax
	cpuid
	movl 12(%ebp),%edi
	test %edi,%edi
	jz L1	
	movl %eax,(%edi)
L1:	movl 16(%ebp),%edi
	test %edi,%edi
	jz L2
	movl %ebx,(%edi)
L2:	movl 20(%ebp),%edi
	test %edi,%edi
	jz L3
	movl %ecx,(%edi)
L3:	movl 24(%ebp),%edi
	test %edi,%edi
	jz L4
	movl %edx,(%edi)
L4:	popl %edx
	popl %ecx
	popl %ebx
	popl %edi
	movl %ebp,%esp
	popl %ebp
	ret

