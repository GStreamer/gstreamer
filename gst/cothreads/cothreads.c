/* Pthread-friendly coroutines with pth
 * Copyright (C) 2002 Andy Wingo <wingo@pobox.com>
 *
 * cothread.c: public API implementation
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


#ifdef HAVE_LINUXTHREADS
static cothread_attr cothread_attr_default = 
{
  COTHREAD_ATTR_METHOD_LINUXTHREADS,  /* use the linuxthreads hack */
  0x200000,                           /* 2 MB */
  8,                                  /* for a stack size of 256 KB */
  TRUE                                /* set up the first chunk */
};
#else
static cothread_attr cothread_attr_default = 
{
  COTHREAD_ATTR_METHOD_GTHREAD_STACK, /* this is what the old cothreads code does */
  0x100000,                           /* only 1 MB due the the FreeBSD defaults */
  8,                                  /* for a stack size of 128 KB */
  TRUE                                /* set up the first chunk */
};
#endif

cothread_attr *_cothread_attr_global = NULL;

static gboolean (*stack_alloc_func) (char**, char**);

cothread*
cothread_init (cothread_attr *attr)
{
  static cothread_attr _attr;
  
  if (_cothread_attr_global) {
    g_warning ("cothread system has already been initialized");
    return;
  }
  
  if (!attr)
    _attr = cothread_attr_default;
  else 
    _attr = *attr;
  
  _cothread_attr_global = &_attr;
  
  switch (_cothread_attr_global->method) {
  case COTHREAD_ATTR_METHOD_MALLOC:
    stack_alloc_func = cothread_stack_alloc_on_heap;
    break;
  case COTHREAD_ATTR_METHOD_GTHREAD_STACK:
    stack_alloc_func = cothread_stack_alloc_on_gthread_stack;
    break;
  case COTHREAD_ATTR_METHOD_LINUXTHREADS:
    stack_alloc_func = cothread_stack_alloc_linuxthreads;
    break;
  default:
    g_error ("unexpected value for attr method %d", _cothread_attr_global->method);
  }
  
  return cothread_create (NULL);
}

cothread*
cothread_create (void (*func)(void))
{
  char *low, *high;
  cothread *ret = g_new0 (cothread, 1);
  
  if (!func) {
    /* we are being asked to save the current thread into a new cothread. this
     * only happens for the first cothread. */
    if (_cothread_attr_global->alloc_cothread_0)
      if (!stack_alloc_func (&low, &high))
        g_error ("couldn't create cothread 0");
    
    pth_mctx_save (ret);
    return ret;
  }
  
  if (!stack_alloc_func (&low, &high))
    g_error ("could not allocate a new cothread stack");
  
  pth_mctx_set (ret, func, low, high);
  
  return ret;
}

void
cothread_destroy (cothread *thread)
{
  /* FIXME: have method-specific destroy functions. */
  
  g_free (thread);
}
