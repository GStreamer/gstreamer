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
typedef enum _cothreads_alloc_method cothreads_alloc_method;
typedef struct _cothreads_config cothreads_config;


enum _cothreads_alloc_method 
{
  COTHREADS_ALLOC_METHOD_MALLOC,        /* cothread stacks on the heap, one block per chunk */
  COTHREADS_ALLOC_METHOD_GTHREAD_STACK, /* cothread stacks within the current gthread's stack */
  COTHREADS_ALLOC_METHOD_LINUXTHREADS,  /* a hack that allows for linuxthreads compatibility */
};

struct _cothreads_config {
  cothreads_alloc_method method;        /* the method of allocating new cothread stacks */
  int chunk_size;                       /* size of contiguous chunk of memory for cothread stacks */
  int blocks_per_chunk;                 /* cothreads per chunk */
  gboolean alloc_cothread_0;            /* if the first cothread needs to be allocated */
};

#define COTHREADS_CONFIG_HEAP_INITIALIZER { \
  COTHREADS_ALLOC_METHOD_MALLOC,        /* each cothread on the heap */ \
  0x20000,                              /* stack size of 128 kB */ \
  1,                                    /* we aren't chunked */ \
  FALSE                                 /* nothing special for cothread 0 */ \
}

#define COTHREADS_CONFIG_GTHREAD_INITIALIZER { \
  COTHREADS_ALLOC_METHOD_GTHREAD_STACK, /* this is what the old cothreads code does */ \
  0x100000,                             /* only 1 MB due the the FreeBSD defaults */ \
  8,                                    /* for a stack size of 128 KB */ \
  TRUE                                  /* set up the first chunk */ \
}

#define COTHREADS_CONFIG_LINUXTHREADS_INITIALIZER { \
  COTHREADS_ALLOC_METHOD_LINUXTHREADS,  /* use the linuxthreads hack */ \
  0x200000,                             /* 2 MB */ \
  8,                                    /* for a stack size of 256 KB */ \
  TRUE                                  /* set up the first chunk */ \
}

gboolean	cothreads_initialized	(void);
void		cothreads_init		(cothreads_config *config);

cothread*	cothread_create		(void (*func)(int, void**), int argc, void **argv);
void		cothread_destroy	(cothread *thread);

/* 'old' and 'new' are of type (cothread*) */
#define cothread_switch(old,new) pth_mctx_switch(old,new)
#define	cothread_yield(new) pth_mctx_restore(new);

#endif /* __COTHREAD_H__ */
