/* Pthread-friendly coroutines with pth
 * Copyright (C) 2001 Andy Wingo <wingo@pobox.com>
 *
 * cothread-stack.c: various methods of allocating cothread stacks
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

#include "cothreads-private.h"
#include "linuxthreads.h"

typedef enum _cothread_block_state cothread_block_state;
typedef struct _cothread_chunk cothread_chunk;

enum _cothread_block_state
{
  COTHREAD_BLOCK_STATE_UNUSED=0,
  COTHREAD_BLOCK_STATE_IN_USE
};

struct _cothread_chunk {
  cothread_chunk *next;
  cothread_block_state *block_states;
  char *chunk;
  int size;
  int reserved_bottom;
  gboolean needs_free;
  int nblocks;
};


static cothread_chunk*	cothread_chunk_new		(unsigned long size, gboolean allocate);
static void		cothread_chunk_free		(cothread_chunk *chunk);
static gboolean		cothread_stack_alloc_chunked 	(cothread_chunk *chunk, char **low, char **high, 
                                                         cothread_chunk *(*chunk_new)(cothread_chunk*));
static cothread_chunk*	cothread_chunk_new_linuxthreads	(cothread_chunk* old);


gboolean
cothread_stack_alloc_on_heap (char **low, char **high)
{
  *low = g_malloc (_cothread_attr_global->chunk_size / _cothread_attr_global->blocks_per_chunk);
  *high = *low + sizeof (*low) - 1;
  return TRUE;
}

gboolean
cothread_stack_alloc_on_gthread_stack (char **low, char **high)
{
  cothread_chunk *chunk = NULL;
  static GStaticPrivate chunk_key = G_STATIC_PRIVATE_INIT;
  
  if (!(chunk = g_static_private_get(&chunk_key))) {
    chunk = cothread_chunk_new (_cothread_attr_global->chunk_size, FALSE);
    g_static_private_set (&chunk_key, chunk, cothread_chunk_free);
  }
  
  return cothread_stack_alloc_chunked (chunk, low, high, NULL);
}

gboolean
cothread_stack_alloc_linuxthreads (char **low, char **high)
{
  cothread_chunk *chunk = NULL;
  static GStaticPrivate chunk_key = G_STATIC_PRIVATE_INIT;
  
  if (!(chunk = g_static_private_get(&chunk_key))) {
    chunk = cothread_chunk_new (_cothread_attr_global->chunk_size, FALSE);
    g_static_private_set (&chunk_key, chunk, cothread_chunk_free);
  }
  
  return cothread_stack_alloc_chunked (chunk, low, high, cothread_chunk_new_linuxthreads);
}


/* size must be a power of two. */
static cothread_chunk*
cothread_chunk_new (unsigned long size, gboolean allocate)
{
  cothread_chunk *ret;
  char *sp = CURRENT_STACK_FRAME;
  
  ret = g_new0 (cothread_chunk, 1);
  ret->nblocks = _cothread_attr_global->blocks_per_chunk;
  ret->block_states = g_new0 (cothread_block_state, ret->nblocks);
  
  if (allocate) {
    if (!posix_memalign(&ret->chunk, size, size))
      g_error ("memalign operation failed");
  } else {
    /* if we don't allocate the chunk, we must already be in it. */

    ret->chunk = (char*) ((unsigned long) sp &~ (size - 1));
#if PTH_STACK_GROWTH > 0
    ret->reserved_bottom = sp - ret->chunk;
#else
    ret->reserved_bottom = sp + size - ret->chunk;
#endif
  }
  
  ret->size = size;
  ret->needs_free = allocate;
  
  return ret;
}

/**
 * cothread_stack_alloc_chunked:
 * @chunk: the chunk for the 
 * Make a new cothread stack out of a chunk. Chunks are assumed to be aligned on
 * boundaries of _cothread_attr_global->chunk_size.
 *
 * Returns: the new cothread context
 */
  /* we assume that the stack is aligned on _cothread_attr_global->chunk_size boundaries */
static gboolean
cothread_stack_alloc_chunked (cothread_chunk *chunk, char **low, char **high, 
                              cothread_chunk *(*chunk_new)(cothread_chunk*))
{
  int block;
  cothread_chunk *walk, *last;
  
  for (walk=chunk; walk; last=walk, walk=walk->next) {
    if (walk->block_states[0] == COTHREAD_BLOCK_STATE_UNUSED) {
      walk->block_states[0] = COTHREAD_BLOCK_STATE_IN_USE;
#if PTH_STACK_GROWTH > 0
      *low  = walk->chunk + walk->reserved_bottom;
      *high = walk->chunk + walk->size / walk->nblocks;
#else
      *low  = walk->chunk + walk->size * (walk->nblocks - 1) / walk->nblocks;
      *high = walk->chunk + walk->size - walk->reserved_bottom;
#endif
      return TRUE;
    }
    
    for (block = 1; block < walk->nblocks; block++) {
      if (walk->block_states[block] == COTHREAD_BLOCK_STATE_UNUSED) {
#if PTH_STACK_GROWTH > 0
        *low  = walk->chunk + walk->size * (walk->nblocks - block - 1) / walk->nblocks;
#else
        *low  = walk->chunk + walk->size * (block - 1) / walk->nblocks;
#endif
        *high = *low + walk->size / walk->nblocks;
        return TRUE;
      }
    }
  }
  
  if (!chunk_new)
    return FALSE;
  else
    return cothread_stack_alloc_chunked (chunk_new (last), low, high, NULL);
}

static void
cothread_chunk_free (cothread_chunk *chunk) 
{
  /* FIXME: implement me please */
}

static cothread_chunk*
cothread_chunk_new_linuxthreads (cothread_chunk* old)
{
  cothread_chunk *new;
  void *pthread_descr;
  
  new = cothread_chunk_new (_cothread_attr_global->chunk_size, TRUE);
  pthread_descr = __linuxthreads_self();
#if PTH_STACK_GROWTH > 0
  /* we don't really know pthread_descr's size in this case, but we can be
   * conservative. it's normally 1K in the down-growing case, so we allocate 2K.
   */
  new->reserved_bottom = 2048;
  memcpy(new->chunk, pthread_descr, 2048);
#else
  new->reserved_bottom = ((unsigned long) pthread_descr | (new->size - 1)) - (unsigned long) pthread_descr;
  memcpy(new->chunk + new->size - new->reserved_bottom - 1, pthread_descr, new->reserved_bottom);
#endif
  
  old->next = new;
  return new;
}
