/* Pthread-friendly coroutines with pth
 * Copyright (C) 2002 Andy Wingo <wingo@pobox.com>
 *
 * cothreads.c: public API implementation
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

#define HAVE_LINUXTHREADS

#ifdef HAVE_LINUXTHREADS
static cothreads_config cothreads_config_default = COTHREADS_CONFIG_LINUXTHREADS_INITIALIZER;
#else
static cothreads_config cothreads_config_default = COTHREADS_CONFIG_GTHREAD_INITIALIZER;
#endif

cothreads_config *_cothreads_config_global = NULL;

static gboolean (*stack_alloc_func) (char**, char**);

static void	cothread_private_set	(char *sp, void *priv, size_t size);
static void	cothread_private_get	(char *sp, void *priv, size_t size);
static void	cothread_stub		(void);


/**
 * cothreads_initialized:
 *
 * Query the state of the cothreads system.
 *
 * Returns: TRUE if cothreads_init() has already been called, FALSE otherwise
 */
gboolean
cothreads_initialized (void) 
{
  return (_cothreads_config_global != NULL);
}

/**
 * cothreads_init:
 * @config: attributes for creation of cothread stacks
 *
 * Initialize the cothreads system. If @config is NULL, use the default parameters
 * detected at compile-time.
 */
void
cothreads_init (cothreads_config *config)
{
  static cothreads_config _config;
  
  if (cothreads_initialized()) {
    g_warning ("cothread system has already been initialized");
    return;
  }

  /* we don't hold on to *config, we copy it (if it's supplied) */
  if (!config)
    _config = cothreads_config_default;
  else 
    _config = *config;
  
  _cothreads_config_global = &_config;
  
  switch (_cothreads_config_global->method) {
  case COTHREADS_ALLOC_METHOD_MALLOC:
    stack_alloc_func = cothread_stack_alloc_on_heap;
    break;
  case COTHREADS_ALLOC_METHOD_GTHREAD_STACK:
    stack_alloc_func = cothread_stack_alloc_on_gthread_stack;
    break;
  case COTHREADS_ALLOC_METHOD_LINUXTHREADS:
    stack_alloc_func = cothread_stack_alloc_linuxthreads;
    break;
  default:
    g_error ("unexpected value for config method %d", _cothreads_config_global->method);
  }
}

/**
 * cothread_create:
 * @func: function to start with this cothread
 * @argc: argument count
 * @argv: argument vector
 *
 * Create a new cothread running a given function. You must explictly switch
 * into this cothread to give it control. If @func is NULL, a cothread is
 * created on the current stack with the current stack pointer.
 *
 * Returns: A pointer to the new cothread
 */
cothread*
cothread_create (void (*func)(int, void **), int argc, void **argv)
{
  char *low, *high;
  cothread_private priv;
  cothread *ret = g_new0 (cothread, 1);
  
  if (!func) {
    /* we are being asked to save the current thread into a new cothread. this
     * only happens for the first cothread. */
    if (_cothreads_config_global->alloc_cothread_0)
      if (!stack_alloc_func (&low, &high))
        g_error ("couldn't create cothread 0");
      else
        g_message ("created cothread 0 with low=%p, high=%p", low, high);
    else
      g_message ("created cothread 0");
    
    pth_mctx_save (ret);
    return ret;
  }
  
  if (!stack_alloc_func (&low, &high))
    g_error ("could not allocate a new cothread stack");
  
  g_message ("created a cothread with low=%p, high=%p", low, high);
  
  pth_mctx_set (ret, cothread_stub, low, high);
  
  priv.argc = argc;
  priv.argv = argv;
  priv.func = func;
  cothread_private_set (low, &priv, sizeof(priv));
  
  return ret;
}

/**
 * cothread_destroy:
 * @thread: cothread to destroy
 *
 * Deallocate any memory used by the cothread data structures.
 */
void
cothread_destroy (cothread *thread)
{
  /* FIXME: have method-specific destroy functions. */
  
  g_free (thread);
}

/* the whole 'page size' thing is to allow for the last page of a stack or chunk
 * to be mmap'd as a boundary page */

static void
cothread_private_set (char *sp, void *priv, size_t size)
{
  char *dest;
  
#if PTH_STACK_GROWTH > 0
  dest = (char*) ((gulong)sp | (_cothreads_config_global->chunk_size / _cothreads_config_global->blocks_per_chunk - 1))
    - size + 1 - getpagesize();
#else
  dest = (char*) ((gulong)sp &~ (_cothreads_config_global->chunk_size / _cothreads_config_global->blocks_per_chunk - 1))
    + getpagesize();
#endif
  
  memcpy (dest, priv, size);
}

static void
cothread_private_get (char *sp, void *priv, size_t size)
{
  char *src;
  
#if PTH_STACK_GROWTH > 0
  src = (char*) ((gulong)sp | (_cothreads_config_global->chunk_size / _cothreads_config_global->blocks_per_chunk - 1))
    - size + 1 - getpagesize();
#else
  src = (char*) ((gulong)sp &~ (_cothreads_config_global->chunk_size / _cothreads_config_global->blocks_per_chunk - 1))
    + getpagesize();
#endif
  
  memcpy (priv, src, size);
}

static void
cothread_stub (void)
{
  cothread_private priv;
  
  cothread_private_get (CURRENT_STACK_FRAME, &priv, sizeof (priv));
  
  priv.func (priv.argc, priv.argv);
  
  g_warning ("we really shouldn't get here");
}

