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

typedef struct _GstAtomicSwap GstAtomicSwap;

#define GST_ATOMIC_SWAP_VALUE(swap)	((swap)->value)

struct _GstAtomicSwap {
  volatile gpointer 	 value;  
  volatile gulong 	 cnt; 			/* for the ABA problem */
  GMutex                *lock;			/* lock for C fallback */
};

#if defined (__i386__) && defined (__GNUC__) && __GNUC__ >= 2 

#define GST_ATOMIC_LOCK "lock ; "

#define _GST_ATOMIC_SWAP_INIT(swap,val)		\
G_STMT_START {						\
  (swap)->value = (gpointer)(val);			\
  (swap)->cnt = 0;					\
} G_STMT_END

#define _GST_ATOMIC_SWAP(swap, val)				\
G_STMT_START {							\
  __asm__ __volatile__ ("1:"					\
                        "  movl %2, (%1);"			\
                        GST_ATOMIC_LOCK "cmpxchg %1, %0;"	\
                        "  jnz 1b;"				\
                          :					\
                          : "m" (*swap), 			\
			    "r" (val), 				\
			    "a" ((swap)->value));		\
} G_STMT_END

#define _GST_ATOMIC_SWAP_GET(swap, val, res)			\
G_STMT_START {							\
  __asm__ __volatile__ ("  testl %%eax, %%eax;"			\
                        "  jz 20f;"				\
                        "10:"					\
                        "  movl (%%eax), %%ebx;"		\
                        "  movl %%edx, %%ecx;"			\
                        "  incl %%ecx;"				\
                        GST_ATOMIC_LOCK "cmpxchg8b %1;"		\
                        "  jnz 10b;"				\
                        "20:\t"					\
                          : "=a" (*res)				\
                          :  "m" (*(swap)), 			\
			     "a" (val), 			\
			     "d" ((swap)->cnt)			\
                          :  "ecx", "ebx");			\
} G_STMT_END

#elif defined (__powerpc__) && defined (__GNUC__) && __GNUC__ >= 2 

#define _GST_ATOMIC_SWAP_INIT(swap,val)		\
G_STMT_START {						\
  (swap)->value = (gpointer)(val);			\
  (swap)->cnt = 0;					\
} G_STMT_END

#define _GST_ATOMIC_SWAP(swap, val)				\
G_STMT_START {							\
  int tmp;							\
  __asm__ __volatile__ ("1:"					\
                        "	lwarx	%0, 0, %2	\n"	\
                        "	stwcx.  %3, 0, %2 	\n"	\
                        "	bne-	1b		\n"	\
                          : "=&r" (tmp),			\
			    "=m" (*swap)			\
                          : "r" (swap),  			\
			    "r" (val), 				\
			    "m" (* swap)			\
			  : "9", "cc");				\
} G_STMT_END

#define _GST_ATOMIC_SWAP_GET(swap, val, res)			\
G_STMT_START {							\
  __asm__ __volatile__ ("1:"					\
                        "	lwarx	%0, 0, %2	\n"	\
                        "	stwcx.  %3, 0, %2 	\n"	\
                        "	bne-	1b		\n"	\
                          : "=&r" (*(res)),			\
			    "=m" (*swap)			\
                          : "r" (swap),  			\
			    "r" (val), 				\
			    "m" (*swap)				\
			  : "cc");				\
} G_STMT_END

#else

#define _GST_ATOMIC_SWAP_INIT(swap,val)			\
G_STMT_START {						\
  (swap)->lock = g_mutex_new();				\
  (swap)->value = (gpointer)val;			\
} G_STMT_END

#define _GST_ATOMIC_SWAP(swap, val)			\
G_STMT_START {						\
  gpointer tmp;						\
  g_mutex_lock ((swap)->lock);				\
  tmp = (swap)->value;					\
  (swap)->value = val;					\
  ((gpointer)*val) = tmp;				\
  g_mutex_unlock ((swap)->lock);			\
} G_STMT_END

#define _GST_ATOMIC_SWAP_GET(swap, val, res)		\
G_STMT_START {						\
  if (val) {						\
    gpointer tmp;					\
    gint *tmp2;	/* this is pretty EVIL */		\
    g_mutex_lock ((swap)->lock);			\
    tmp = (swap)->value;				\
    tmp2 = val;						\
    (swap)->value = (gpointer)*tmp2;			\
    (*res) = (gpointer)*tmp2 = (gint*)tmp;		\
    g_mutex_unlock ((swap)->lock);			\
  }							\
} G_STMT_END
#endif

/* initialize the swap structure with an initial value */
#define GST_ATOMIC_SWAP_INIT(swap,val)		_GST_ATOMIC_SWAP_INIT(swap, val)

/* atomically swap the contents the swap value with the value pointed to
 * by val. */
#define GST_ATOMIC_SWAP(swap, val)		_GST_ATOMIC_SWAP(swap, val)

/* atomically swap the contents the swap value with the value pointed to
 * by val. The new value of the swap value is returned in the memory pointed
 * to by res */
#define GST_ATOMIC_SWAP_GET(swap,val,res)	_GST_ATOMIC_SWAP_GET(swap, val, res)

G_END_DECLS

#endif /*  __GST_ATOMIC_H__ */
