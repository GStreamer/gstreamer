/* Pthread-friendly coroutines with pth
 * Copyright (C) 2002 Andy Wingo <wingo@pobox.com>
 *
 * cothread-private.h: private prototypes
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

#ifndef __COTHREAD_PRIVATE_H__
#define __COTHREAD_PRIVATE_H__

#include <cothreads.h>

typedef struct _cothread_private cothread_private;

struct _cothread_private {
  int argc;
  char **argv;
  void (*func) (int argc, char **argv);
};

extern cothread_attr *_cothread_attr_global;


gboolean	cothread_stack_alloc_on_gthread_stack	(char **low, char **high);
gboolean	cothread_stack_alloc_linuxthreads	(char **low, char **high);
gboolean	cothread_stack_alloc_on_heap		(char **low, char **high);


#endif /* __COTHREAD_PRIVATE_H__ */
