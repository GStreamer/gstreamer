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

#ifndef __GST_ATOMIC_H__
#define __GST_ATOMIC_H__

/* FIXME */
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#ifdef HAVE_ATOMIC_H
# include <asm/atomic.h>
#endif

#include <glib.h>

G_BEGIN_DECLS

typedef struct _GstAtomicInt GstAtomicInt;

struct _GstAtomicInt {
#ifdef HAVE_ATOMIC_H
  union {
    atomic_t               value;
    struct {
      int                    value;
      GMutex                *lock;
    } unused;
  } v;
#else
  int                    value;
  GMutex                *lock;
#endif
};

#ifdef HAVE_ATOMIC_H

/* atomic functions */
#define GST_ATOMIC_INT_INIT(ref, val)		(atomic_set(&((ref)->v.value), (val)))
#define GST_ATOMIC_INT_FREE(ref)		

#define GST_ATOMIC_INT_SET(ref,val)  		(atomic_set(&((ref)->v.value), (val)))
#define GST_ATOMIC_INT_VALUE(ref)  		(atomic_read(&((ref)->v.value)))
#define GST_ATOMIC_INT_READ(ref,res)  		(*res = atomic_read(&((ref)->v.value)))
#define GST_ATOMIC_INT_INC(ref)  		(atomic_inc (&((ref)->v.value)))
#define GST_ATOMIC_INT_DEC_AND_TEST(ref,zero)  	(*zero = atomic_dec_and_test (&((ref)->v.value)))
#define GST_ATOMIC_INT_ADD(ref, count)	  	(atomic_add ((count), &((ref)->v.value)))

#else

/* fallback using a lock */
#define GST_ATOMIC_INT_INIT(ref, val)		\
G_STMT_START {					\
  (ref)->value = (val);				\
  (ref)->lock = g_mutex_new();			\
} G_STMT_END

#define GST_ATOMIC_INT_FREE(ref)		g_mutex_free ((ref)->lock)

#define GST_ATOMIC_INT_SET(ref,val)		\
G_STMT_START {					\
  g_mutex_lock ((ref)->lock);			\
  (ref)->value = (val);				\
  g_mutex_unlock ((ref)->lock);			\
} G_STMT_END

#define GST_ATOMIC_INT_VALUE(ref)  		((ref)->value)
#define GST_ATOMIC_INT_READ(ref,res)  		\
G_STMT_START {					\
  g_mutex_lock ((ref)->lock);			\
  *res = (ref)->value;				\
  g_mutex_unlock ((ref)->lock);			\
} G_STMT_END

#define GST_ATOMIC_INT_INC(ref)  			\
G_STMT_START {					\
  g_mutex_lock ((ref)->lock);			\
  (ref)->value++;				\
  g_mutex_unlock ((ref)->lock);			\
} G_STMT_END

#define GST_ATOMIC_INT_DEC_AND_TEST(ref,zero)	\
G_STMT_START {					\
  g_mutex_lock ((ref)->lock);			\
  (ref)->value--;				\
  *zero = ((ref)->value == 0);			\
  g_mutex_unlock ((ref)->lock);			\
} G_STMT_END

#define GST_ATOMIC_INT_ADD(ref, count)		\
G_STMT_START {					\
  g_mutex_lock ((ref)->lock);			\
  (ref)->value += count;			\
  g_mutex_unlock ((ref)->lock);			\
} G_STMT_END

#endif /* HAVE_ATOMIC_H */

G_END_DECLS

#endif /*  __GST_ATOMIC_H__ */
