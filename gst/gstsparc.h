/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstsparc.h: Header for Sparc-specific architecture issues
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

#ifndef __GST_GSTSPARC_H__
#define __GST_GSTSPARC_H__

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

#endif /* __GST_GSTSPARC_H__ */
