/* GStreamer
 * Copyright (C) 1999,2000 Erik Walthinsen <omega@cse.ogi.edu>
 *                    2000 Wim Taymans <wtay@chello.be>
 *
 * gstppc.h: Header for PPC-specific architecture issues
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

#ifndef GST_HGUARD_GSTALPHA_H
#define GST_HGUARD_GSTALPHA_H

#warning in gstalpha.h

#define GET_SP(target) \
    __asm__("stq $30,%0" : "=m"(target) : : "30");

#define SET_SP(stackpointer) \
    __asm__("bis $31,%0,$30" : : "r"(stackpointer));

#define CALL(target) \
    __asm__( "bis $31,%0,$27\n\t" \
             "jsr $26,($27),0" : : "r"(target) );

struct minimal_ppc_stackframe {
    unsigned long back_chain;
    unsigned long LR_save;
    unsigned long unused1;
    unsigned long unused2;
};

#define SETUP_STACK(sp) \
    sp = ((unsigned long *)(sp)) - 4; \
    ((struct minimal_ppc_stackframe *)sp)->back_chain = 0;

#endif /* GST_HGUARD_GSTALPHA_H */
