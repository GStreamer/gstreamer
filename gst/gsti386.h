/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
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

#ifndef GST_HGUARD_GSTI386_H
#define GST_HGUARD_GSTI386_H

#define GET_SP(target) \
  __asm__("movl %%esp, %0" : "=m"(target) : : "esp", "ebp");

#define SET_SP(source) \
  __asm__("movl %0, %%esp\n" : "=m"(thread->sp));

#define JUMP(target) \
    __asm__("jmp " SYMBOL_NAME_STR(cothread_stub))

#define SETUP_STACK(sp) do ; while(0)

#endif /* GST_HGUARD_GSTI386_H */
