/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstarch.h: Architecture-specific inclusions
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GST_GSTARCH_H__
#define __GST_GSTARCH_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif



/***** Intel x86 *****/
#if defined(HAVE_CPU_I386)
#define GST_ARCH_SET_SP(stackpointer) \
  __asm__( "movl %0, %%esp\n" : : "r"(stackpointer) );

#define GST_ARCH_CALL(target) \
    __asm__("call *%0" : : "r"(target) );

// assuming the stackframe is 16 bytes
#define GST_ARCH_SETUP_STACK(sp) sp -= 4



/***** PowerPC *****/
#elif defined (HAVE_CPU_PPC)

// should bring this in line with others and use an "r"
#define GST_ARCH_SET_SP(stackpointer) \
    __asm__("lwz 1,%0" : : "m"(stackpointer))
  
#define GST_ARCH_CALL(target) \
    __asm__( "mr 0,%0\n\t" \
             "mtlr 0\n\t" \
             "blrl" : : "r"(target) );
  
struct minimal_ppc_stackframe {
    unsigned long back_chain;
    unsigned long LR_save;
    unsigned long unused1;
    unsigned long unused2;
};

#define GST_ARCH_SETUP_STACK(sp) \
    sp = ((unsigned long *)(sp)) - 4; \
    ((struct minimal_ppc_stackframe *)sp)->back_chain = 0;



/***** DEC[/Compaq/HP?/Intel?] Alpha *****/
#elif defined(HAVE_CPU_ALPHA)

#define GST_ARCH_SET_SP(stackpointer) \
    __asm__("bis $31,%0,$30" : : "r"(stackpointer));

#define GST_ARCH_CALL(target) \
    __asm__( "bis $31,%0,$27\n\t" \
             "jsr $26,($27),0" : : "r"(target) );

// Need to get more information about the stackframe format
// and get the fields more correct.  Check GDB sources maybe?
struct minimal_stackframe {
    unsigned long back_chain;
    unsigned long LR_save;
    unsigned long unused1;
    unsigned long unused2;
};

#define GST_ARCH_SETUP_STACK(sp) \
    sp = ((unsigned long *)(sp)) - 4; \
    ((struct minimal_stackframe *)sp)->back_chain = 0;



/***** ARM *****/
#elif defined(HAVE_CPU_ARM)

#define GST_ARCH_SET_SP(stackpointer) \
    __asm__( "mov sp, %0" : : "r"(stackpointer));

#define GST_ARCH_CALL(target) \
    __asm__( "mov pc, %0" : : "r"(target) );

// Need to get more information about the stackframe format
// and get the fields more correct.  Check GDB sources maybe?

#define GST_ARCH_SETUP_STACK(sp) sp -= 4



/***** Sun SPARC *****/
#elif defined(HAVE_CPU_SPARC)

#define GST_ARCH_SET_SP(stackpointer) \
    __asm__( "ta 3\n\t" \
             "mov %0, %%sp" : : "r"(stackpointer));

#define GST_ARCH_CALL(target) \
    __asm__( "call %0,0\n\t" \
             "nop" : : "r"(target) );

#define GST_ARCH_PRESETJMP() \
    __asm__( "ta 3" );

// Need to get more information about the stackframe format
// and get the fields more correct.  Check GDB sources maybe?

#define GST_ARCH_SETUP_STACK(sp) sp -= 4



/***** MIPS *****/
#elif defined(HAVE_CPU_MIPS)

#define GST_ARCH_SET_SP(stackpointer) \
    __asm__("move $sp,%0\n\t" : : "r"(stackpointer));

#define GST_ARCH_CALL(target) \
    __asm__("move $25,%1\n\t"	/* call via $25 */ \
            "jal  $25\n\t" : : "r"(target));

// assuming the stackframe is 16 bytes
#define GST_ARCH_SETUP_STACK(sp) sp -= 4
    


/***** HP-PA *****/
#elif defined(HAVE_CPU_HPPA)

#define GST_ARCH_SET_SP(stackpointer) \
    __asm__("copy %0,%%sp\n\t" : : "r"(stackpointer));

#define GST_ARCH_CALL(target) \
    __asm__("copy $1,%%r22\n\t"		/* set call address */ \
            ".CALL\n\t"			/* call pseudo insn (why?) */ \
            "bl $$dyncall,%%r31\n\t" : : "r"(target));

// assume stackframe is 16 bytes
#define GST_ARCH_SETUP_STACK(sp) sp -= 4



#else
#error Need to know about this architecture, or have a generic implementation
#endif

#endif /* __GST_GSTARCH_H__ */
