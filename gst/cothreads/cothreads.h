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


#ifndef CURRENT_STACK_FRAME
#define CURRENT_STACK_FRAME  ({ char __csf; &__csf; })
#endif /* CURRENT_STACK_FRAME */


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
  cothread_attr_method method;        /* the method of allocating new cothread stacks */
  int chunk_size;                     /* size of contiguous chunk of memory for cothread stacks */
  int blocks_per_chunk;               /* cothreads per chunk */
  gboolean alloc_cothread_0;          /* if the first cothread needs to be allocated */
};


gboolean	cothreads_initialized	(void);
void		cothreads_init		(cothread_attr *attr);

cothread*	cothread_create		(void (*func)(int, void**), int argc, void **argv);
void		cothread_destroy	(cothread *thread);

/* 'old' and 'new' are of type (cothread*) */
#define cothread_switch(old,new) pth_mctx_switch(old,new)
#define	cothread_yield(new) pth_mctx_restore(new);

#endif /* __COTHREAD_H__ */
