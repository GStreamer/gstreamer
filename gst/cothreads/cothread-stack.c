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

/* chunks can contain 1 or more blocks, each block contains one cothread stack */

enum cothread_attr_method 
{
  COTHREAD_ATTR_METHOD_MALLOC,        /* cothread stacks on the heap, one block per chunk */
  COTHREAD_ATTR_METHOD_GTHREAD_STACK, /* cothread stacks within the current gthread's stack */
  COTHREAD_ATTR_METHOD_LINUXTHREADS,  /* a hack that allows for linuxthreads compatibility */
}

struct cothread_attr {
  enum cothread_attr_method method;
  int chunk_size;
  int blocks_per_chunk;
}

#ifdef HAVE_LINUXTHREADS
static struct cothread_attr cothread_attr_default = 
{
  COTHREAD_ATTR_METHOD_LINUXTHREADS,  /* use the linuxthreads hack */
  0x20000,                            /* 2 MB */
  8                                   /* for a stack size of 256 KB */
}
#else
static struct cothread_attr cothread_attr_default = 
{
  COTHREAD_ATTR_METHOD_GTHREAD_STACK, /* this is what the old cothreads code does */
  0x10000,                            /* only 1 MB due the the FreeBSD defaults */
  8                                   /* for a stack size of 128 KB */
}
#endif

static struct cothread_attr *_attr = NULL; /* set in cothread_init() */

enum cothread_block_state
{
  COTHREAD_BLOCK_STATE_UNUSED=0,
  COTHREAD_BLOCK_STATE_IN_USE
}

struct cothread_chunk {
  struct cothread_chunk *next;
  enum cothread_block_state *block_states;
  char *chunk;
  int size;
  int reserved_bottom;
  gboolean needs_free;
}

/* size must be a power of two. */
struct cothread_chunk*
cothread_chunk_new (unsigned long size, gboolean allocate)
{
  struct cothread_chunk *ret;
  char *sp = CURRENT_STACK_FRAME;
  
  ret = g_new0 (struct cothread_chunk, 1);
  ret->block_states = g_new0 (enum cothread_block_state, _attr->blocks_per_chunk);
  
  if (allocate) {
    if (!posix_memalign(&ret->chunk, size, size))
      g_error ("memalign operation failed");
  } else {
    /* if we don't allocate the chunk, we must already be in it. */

    ret->chunk = (unsigned long) sp &~ (size - 1);
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

gboolean
cothread_stack_alloc_on_heap (char **low, char **high)
{
  *low = g_malloc (_attr->chunk_size / _attr->blocks_per_chunk);
  *high = *low + sizeof (*low);
  return TRUE;
}

/**
 * cothread_stack_alloc_chunked:
 * @chunk: the chunk for the 
 * Make a new cothread stack out of a chunk. Chunks are assumed to be aligned on
 * boundaries of _attr->chunk_size.
 *
 * Returns: the new cothread context
 */
  /* we assume that the stack is aligned on _attr->chunk_size boundaries */
static gboolean
cothread_stack_alloc_chunked (struct cothread_chunk *chunk, char **low, char **high, 
                              (struct cothread_chunk*)(*chunk_new)(struct cothread_chunk*))
{
  int block;
  struct cothread_chunk *walk, *last;
  
  for (walk=chunk; walk; last=walk, walk=walk->next) {
    if (chunk->block_states[0] == COTHREAD_BLOCK_STATE_UNUSED) {
      chunk->block_states[0] = COTHREAD_BLOCK_STATE_IN_USE;
#if PTH_STACK_GROWTH > 0
      *low  = chunk->chunk + chunk->reserved_bottom;
      *high = chunk->chunk + chunk->chunk_size / _attr->blocks_per_chunk;
#else
      *low  = chunk->chunk + chunk->size * (chunk->nblocks - 1) / chunk->nblocks;
      *high = chunk->chunk + chunk->size - chunk->reserved_bottom;
#endif
      return TRUE;
    }
    
    for (block = 1; block < _attr->blocks_per_chunk; block++) {
      if (chunk->block_states[block] == COTHREAD_BLOCK_STATE_UNUSED) {
#if PTH_STACK_GROWTH > 0
        *low  = chunk->chunk + chunk->size * (chunk->nblocks - block - 1) / chunk->nblocks;
#else
        *low  = chunk->chunk + chunk->size * (block - 1) / chunk->nblocks;
#endif
        *high = *low + chunk->size / chunk->nblocks;
        return TRUE;
      }
    }
  }
  
  if (!chunk_new)
    return FALSE;
  else
    return cothread_stack_alloc_chunked (chunk_new (last), low, high, NULL);
}

gboolean
cothread_stack_alloc_on_gthread_stack (char **low, char **high)
{
  struct cothread_chunk *chunk = NULL;
  static GStaticPrivate chunk_key = G_STATIC_PRIVATE_INIT;
  
  if (!(chunk = g_static_private_get(&chunk_key))) {
    chunk = cothread_chunk_new (_attr->size, FALSE);
    g_static_private_set (&chunk_key, chunk, cothread_chunk_free);
  }
  
  return cothread_stack_alloc_chunked (chunk, low, high, NULL);
}

gboolean
cothread_stack_alloc_linuxthreads (char **low, char **high)
{
  struct cothread_chunk *chunk = NULL;
  static GStaticPrivate chunk_key = G_STATIC_PRIVATE_INIT;
  
  if (!(chunk = g_static_private_get(&chunk_key))) {
    chunk = cothread_chunk_new (_attr->size, FALSE);
    g_static_private_set (&chunk_key, chunk, cothread_chunk_free);
  }
  
  return cothread_stack_alloc_chunked (chunk, low, high, cothread_chunk_new_linuxthreads);
}

struct cothread_chunk*
cothread_chunk_new_linuxthreads (struct cothread_chunk* old)
{
  struct cothread_chunk *new;
  void *pthread_descr;
  
  new = cothread_chunk_new (_attr->chunk_size, TRUE);
  pthread_descr = __linuxthreads_self();
#if PTH_STACK_GROWTH > 0
  /* we don't really know pthread_descr's size in this case, but we can be
   * conservative. it's normally 1K in the down-growing case, so we allocate 2K.
   */
  new->reserved_bottom = 2048;
  memcpy(new->chunk, pthread_descr, 2048);
#else
  new->reserved_bottom = ((unsigned long) pthread_descr | (_attr->chunk_size - 1)) - (unsigned long) pthread_descr;
  memcpy(new->chunk + new->size - new->reserved_bottom - 1, pthread_descr, new->reserved_bottom);
#endif
  
  old->next = new;
  return new;
}

    
