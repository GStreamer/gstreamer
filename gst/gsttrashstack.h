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

#ifndef __GST_TRASH_STACK_H__
#define __GST_TRASH_STACK_H__

#include <glib.h>
#include "gstmacros.h"

G_BEGIN_DECLS typedef struct _GstTrashStack GstTrashStack;
typedef struct _GstTrashStackElement GstTrashStackElement;

struct _GstTrashStackElement
{
  GstTrashStackElement *next;
};

typedef volatile gpointer gst_vgpointer;	/* gtk-doc volatile workaround */
typedef volatile gulong gst_vgulong;	/* gtk-doc volatile workaround */

struct _GstTrashStack
{
  gst_vgpointer head;
  gst_vgulong count;		/* for the ABA problem */
  GMutex *lock;			/* lock for C fallback */
};

GST_INLINE_FUNC GstTrashStack *gst_trash_stack_new (void);
GST_INLINE_FUNC void gst_trash_stack_init (GstTrashStack * stack);
GST_INLINE_FUNC void gst_trash_stack_destroy (GstTrashStack * stack);
GST_INLINE_FUNC void gst_trash_stack_free (GstTrashStack * stack);

GST_INLINE_FUNC void gst_trash_stack_push (GstTrashStack * stack, gpointer mem);
GST_INLINE_FUNC gpointer gst_trash_stack_pop (GstTrashStack * stack);

#if defined (GST_CAN_INLINE) || defined (__GST_TRASH_STACK_C__)

#if defined (USE_FAST_STACK_TRASH) && defined (__i386__) && defined (__GNUC__) && __GNUC__ >= 2

#ifdef GST_CONFIG_NO_SMP
#define SMP_LOCK ""
#else
#define SMP_LOCK "lock ; "
#endif

/*
 * intel ia32 optimized lockfree implementations
 */
GST_INLINE_FUNC void
gst_trash_stack_init (GstTrashStack * stack)
{
  stack->head = NULL;
  stack->count = 0;
}

GST_INLINE_FUNC void
gst_trash_stack_destroy (GstTrashStack * stack)
{
}

GST_INLINE_FUNC void
gst_trash_stack_push (GstTrashStack * stack, gpointer mem)
{
  __asm__ __volatile__ ("1:                         \n\t" "  movl %2, (%1);           \n\t"	/* mem->next == stack->head */
      SMP_LOCK "cmpxchg %1, %0;   \n\t"	/* if head unchanged, move mem into it */
      "  jnz 1b;                  \n"	/* head changed, retry */
      ::"m" (*stack), "r" (mem), "a" (stack->head)
      );
}

GST_INLINE_FUNC gpointer
gst_trash_stack_pop (GstTrashStack * stack)
{
  GstTrashStackElement *head;

  /* pop is a little more complicated as we need to avoid the so called ABA
   * problem that arises when a pop and push of the same element happens
   * right between when we read head->next and try to swing the new pointer
   * into place. This is usually solved using a counter which makes it highly
   * inlikely that we manage to grab the wrong head->next value.
   */
  __asm__ __volatile__ ("  testl %%eax, %%eax;      \n\t"	/* if (head == NULL) return */
      "  jz 20f;                  \n\t" "10:                        \n\t" "  movl (%%eax), %%ebx;     \n\t"	/* take value pointed to by head (head->next) */
      "  movl %%edx, %%ecx;       \n\t"	/* take counter */
      "  incl %%ecx;              \n\t"	/* and increment */
      SMP_LOCK "cmpxchg8b %1;     \n\t"	/* if eax:edx == *stack, move ebx:ecx to *stack,
					 * else *stack is moved into eax:edx again... */
      "  jnz 10b;                 \n\t"	/* ... and we retry */
      "20:                        \n":"=a" (head)
      :"m" (*stack), "a" (stack->head), "d" (stack->count)
      :"ecx", "ebx");

  return head;
}

#else

/*
 * generic implementation
 */
GST_INLINE_FUNC void
gst_trash_stack_init (GstTrashStack * stack)
{
  stack->head = NULL;
  stack->lock = g_mutex_new ();
}

GST_INLINE_FUNC void
gst_trash_stack_destroy (GstTrashStack * stack)
{
  g_mutex_free (stack->lock);
}

GST_INLINE_FUNC void
gst_trash_stack_push (GstTrashStack * stack, gpointer mem)
{
  GstTrashStackElement *elem = (GstTrashStackElement *) mem;

  g_mutex_lock (stack->lock);
  elem->next = stack->head;
  stack->head = elem;
  g_mutex_unlock (stack->lock);
}

GST_INLINE_FUNC gpointer
gst_trash_stack_pop (GstTrashStack * stack)
{
  GstTrashStackElement *head;

  g_mutex_lock (stack->lock);
  head = (GstTrashStackElement *) stack->head;
  if (head)
    stack->head = head->next;
  g_mutex_unlock (stack->lock);

  return head;
}

#endif

/*
 * common functions
 */
GST_INLINE_FUNC GstTrashStack *
gst_trash_stack_new (void)
{
  GstTrashStack *stack;

  stack = g_new (GstTrashStack, 1);
  gst_trash_stack_init (stack);

  return stack;
}

GST_INLINE_FUNC void
gst_trash_stack_free (GstTrashStack * stack)
{
  gst_trash_stack_destroy (stack);
  g_free (stack);
}

#endif /* defined (GST_CAN_INLINE) || defined (__GST_TRASH_STACK_C__) */

G_END_DECLS
#endif /*  __GST_TRASH_STACK_H__ */
