/* Pthread-friendly coroutines with pth
 * Copyright (C) 2002 Andy Wingo <wingo@pobox.com>
 *
 * cothread.h: public API
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

#ifndef __COTHREAD_H__
#define __COTHREAD_H__

#include <glib.h>
#include <pth_p.h>


typedef pth_mctx_t cothread;
typedef enum _cothread_attr_method cothread_attr_method;
typedef struct _cothread_attr cothread_attr;


enum _cothread_attr_method 
{
  COTHREAD_ATTR_METHOD_MALLOC,        /* cothread stacks on the heap, one block per chunk */
  COTHREAD_ATTR_METHOD_GTHREAD_STACK, /* cothread stacks within the current gthread's stack */
  COTHREAD_ATTR_METHOD_LINUXTHREADS,  /* a hack that allows for linuxthreads compatibility */
};

struct _cothread_attr {
  cothread_attr_method method;
  int chunk_size;
  int blocks_per_chunk;
  gboolean alloc_cothread_0;
};


cothread*	cothread_init		(cothread_attr *attr);
cothread*	cothread_create		(void (*func)(void));
void		cothread_destroy	(cothread *thread);

#define cothread_switch(old,new) pth_mctx_switch(old,new)

#endif /* __COTHREAD_H__ */
