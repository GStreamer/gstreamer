/* GStreamer
 * Copyright (C) 1999, 2003 Erik Walthinsen <omega@cse.ogi.edu>
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
 *
 *
 * Much of the code in this file is taken from the Linux kernel. 
 * The code is relicensed under the LGPL with the kind permission of
 * Linus Torvalds,Ralf Baechle and Alan Cox
 */

#ifndef __GST_ATOMIC_IMPL_H__
#define __GST_ATOMIC_IMPL_H__

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include "gstatomic.h"
#include "gstmacros.h"

G_BEGIN_DECLS

#if defined (GST_CAN_INLINE) || defined (__GST_ATOMIC_C__)
  
/***** Intel x86 *****/
#if defined (HAVE_CPU_I386) && defined(__GNUC__)

#ifdef GST_CONFIG_NO_SMP
#define SMP_LOCK ""
#else
#define SMP_LOCK "lock ; "
#endif

GST_INLINE_FUNC void 	gst_atomic_int_init 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC void 	gst_atomic_int_destroy 	(GstAtomicInt *aint) { } 
GST_INLINE_FUNC void 	gst_atomic_int_set 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC gint 	gst_atomic_int_read 	(GstAtomicInt *aint) { return aint->counter; }

GST_INLINE_FUNC void 
gst_atomic_int_add (GstAtomicInt *aint, gint val)
{
  __asm__ __volatile__(
    SMP_LOCK "addl %1,%0"
      :"=m" (aint->counter)
      :"ir" (val), "m" (aint->counter));
}

GST_INLINE_FUNC void
gst_atomic_int_inc (GstAtomicInt *aint)
{
  __asm__ __volatile__(
    SMP_LOCK "incl %0"
      :"=m" (aint->counter)
      :"m" (aint->counter));
}

GST_INLINE_FUNC gboolean
gst_atomic_int_dec_and_test (GstAtomicInt *aint)
{
  guchar res;

  __asm__ __volatile__(
    SMP_LOCK "decl %0; sete %1"
      :"=m" (aint->counter), "=qm" (res)
      :"m" (aint->counter) : "memory");

  return res != 0;
}

/***** PowerPC *****/
#elif defined (HAVE_CPU_PPC) && defined(__GNUC__)

#ifdef GST_CONFIG_NO_SMP
#define SMP_SYNC        ""
#define SMP_ISYNC
#else
#define SMP_SYNC        "\tsync\n"
#define SMP_ISYNC       "\tisync\n"
#endif

/* Erratum #77 on the 405 means we need a sync or dcbt before every stwcx.
 * The old ATOMIC_SYNC_FIX covered some but not all of this.
 */
#ifdef GST_CONFIG_IBM405_ERR77
#define PPC405_ERR77(ra,rb)     "\tdcbt " #ra "," #rb "\n"
#else
#define PPC405_ERR77(ra,rb)
#endif

GST_INLINE_FUNC void 	gst_atomic_int_init 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC void 	gst_atomic_int_destroy 	(GstAtomicInt *aint) { } 
GST_INLINE_FUNC void 	gst_atomic_int_set 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC gint 	gst_atomic_int_read 	(GstAtomicInt *aint) { return aint->counter; }

GST_INLINE_FUNC void 
gst_atomic_int_add (GstAtomicInt *aint, gint val)
{
  int t;

  __asm__ __volatile__(
    "1:     lwarx   %0,0,%3\n"
    "       add     %0,%2,%0\n"
            PPC405_ERR77(0,%3)
    "       stwcx.  %0,0,%3 \n"
    "       bne-    1b\n"
      : "=&r" (t), "=m" (aint->counter)
      : "r" (val), "r" (&aint->counter), "m" (aint->counter)
      : "cc");
}

GST_INLINE_FUNC void
gst_atomic_int_inc (GstAtomicInt *aint)
{
  int t;

  __asm__ __volatile__(
    "1:     lwarx   %0,0,%2\n"
    "       addic   %0,%0,1\n"
            PPC405_ERR77(0,%2)
    "       stwcx.  %0,0,%2\n"
    "       bne-    1b\n"
      : "=&r" (t), "=m" (aint->counter)
      : "r" (&aint->counter), "m" (aint->counter)
      : "cc");
}

GST_INLINE_FUNC gboolean
gst_atomic_int_dec_and_test (GstAtomicInt *aint)
{
  int t;

  __asm__ __volatile__(
    "1:     lwarx   %0,0,%1\n"
    "       addic   %0,%0,-1\n"
            PPC405_ERR77(0,%1)
    "       stwcx.  %0,0,%1\n"
    "       bne-    1b\n"
            SMP_ISYNC
      : "=&r" (t)
      : "r" (&aint->counter)
      : "cc", "memory");

  return t == 0;
}

/***** DEC[/Compaq/HP?/Intel?] Alpha *****/
#elif defined(HAVE_CPU_ALPHA) && defined(__GNUC__)

GST_INLINE_FUNC void 	gst_atomic_int_init 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC void 	gst_atomic_int_destroy 	(GstAtomicInt *aint) { } 
GST_INLINE_FUNC void 	gst_atomic_int_set 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC gint 	gst_atomic_int_read 	(GstAtomicInt *aint) { return aint->counter; }

GST_INLINE_FUNC void 
gst_atomic_int_add (GstAtomicInt *aint, gint val)
{
  unsigned long temp;

  __asm__ __volatile__(
    "1:     ldl_l %0,%1\n"
    "       addl %0,%2,%0\n"
    "       stl_c %0,%1\n"
    "       beq %0,2f\n"
    ".subsection 2\n"
    "2:     br 1b\n"
    ".previous"
      :"=&r" (temp), "=m" (aint->counter)
      :"Ir" (val), "m" (aint->counter));
}

GST_INLINE_FUNC void
gst_atomic_int_inc (GstAtomicInt *aint)
{
  gst_atomic_int_add (aint, 1);
}

GST_INLINE_FUNC gboolean
gst_atomic_int_dec_and_test (GstAtomicInt *aint)
{
  long temp, result;
  int val = 1;
  __asm__ __volatile__(
    "1:     ldl_l %0,%1\n"
    "       subl %0,%3,%2\n"
    "       subl %0,%3,%0\n"
    "       stl_c %0,%1\n"
    "       beq %0,2f\n"
    "       mb\n"
    ".subsection 2\n"
    "2:     br 1b\n"
    ".previous"
      :"=&r" (temp), "=m" (aint->counter), "=&r" (result)
      :"Ir" (val), "m" (aint->counter) : "memory");

  return result == 0;
}

/***** Sun SPARC *****/
#elif 0 && defined(HAVE_CPU_SPARC) && defined(__GNUC__)
/* allegedly broken again */

GST_INLINE_FUNC void 	gst_atomic_int_destroy 	(GstAtomicInt *aint) { } 

#ifdef GST_CONFIG_NO_SMP
GST_INLINE_FUNC void 	gst_atomic_int_init 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC void 	gst_atomic_int_set 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC gint 	gst_atomic_int_read 	(GstAtomicInt *aint) { return aint->counter; }
#else
GST_INLINE_FUNC void 	gst_atomic_int_init 	(GstAtomicInt *aint, gint val) { aint->counter = (val<<8); }
GST_INLINE_FUNC void 	gst_atomic_int_set 	(GstAtomicInt *aint, gint val) { aint->counter = (val<<8); }

/*
 * For SMP the trick is you embed the spin lock byte within
 * the word, use the low byte so signedness is easily retained
 * via a quick arithmetic shift.  It looks like this:
 *
 *      ----------------------------------------
 *      | signed 24-bit counter value |  lock  |  atomic_t
 *      ----------------------------------------
 *       31                          8 7      0
 */
GST_INLINE_FUNC gint
gst_atomic_int_read (GstAtomicInt *aint) 
{ 
  int ret = aint->counter;

  while (ret & 0xff)
    ret = aint->counter;

  return ret >> 8;
}
#endif /* GST_CONFIG_NO_SMP */

GST_INLINE_FUNC void 
gst_atomic_int_add (GstAtomicInt *aint, gint val)
{
  volatile int increment, *ptr;
  int lock = 1;
  int ignore = 0;

  ptr = &(aint->counter);

#if __GNUC__ > 3 || (__GNUC__ >=3 && __GNUC_MINOR__ >= 2)
 __asm__ __volatile__("1: ldstub [%[ptr] + 3], %[lock]\n"
		      "\torcc %[lock], 0, %[ignore]\n"
		      "\tbne 1b\n" /* go back until we have the lock */
		      "\tld [%[ptr]], %[inc]\n"
		      "\tsra %[inc], 8, %[inc]\n"
		      "\tadd %[inc], %[val], %[inc]\n"
		      "\tsll %[inc], 8, %[lock]\n"
		      "\tst %[lock],[%[ptr]]\n" /* Release the lock */
		      : [inc] "=&r" (increment), [lock] "=r" (lock),
		        [ignore] "=&r" (ignore)
		      : "0" (increment), [ptr] "r" (ptr), [val] "r" (val)
		      );
#else
 __asm__ __volatile__("1: ldstub [%4 + 3], %1\n"
		      "\torcc %1, 0, %2\n"
		      "\tbne 1b\n" /* go back until we have the lock */
		      "\tld [%4], %0\n"
		      "\tsra %0, 8, %0\n"
		      "\tadd %0, %5, %0\n"
		      "\tsll %0, 8, %1\n"
		      "\tst %1,[%4]\n" /* Release the lock */
		      : "=&r" (increment), "=r" (lock), "=&r" (ignore)
		      : "0" (increment), "r" (ptr), "r" (val)
		      );
#endif
}

GST_INLINE_FUNC void
gst_atomic_int_inc (GstAtomicInt *aint)
{
  gst_atomic_int_add (aint, 1);
}

GST_INLINE_FUNC gboolean
gst_atomic_int_dec_and_test (GstAtomicInt *aint)
{
  volatile int increment, *ptr;
  int lock = 1;
  int ignore = 0;

  ptr = &aint->counter;

#if __GNUC__ > 3 || (__GNUC__ >=3 && __GNUC_MINOR__ >= 2)
  __asm__ __volatile__("1: ldstub [%[ptr] + 3], %[lock]\n"
		       "\torcc %[lock], 0, %[ignore]\n"
		       "\tbne 1b\n" /* go back until we have the lock */
		       "\tld [%[ptr]], %[inc]\n"
		       "\tsra %[inc], 8, %[inc]\n"
		       "\tsub %[inc], 1, %[inc]\n"
		       "\tsll %[inc], 8, %[lock]\n"
		       "\tst %[lock],[%[ptr]]\n" /* Release the lock */
		       : [inc] "=&r" (increment), [lock] "=r" (lock),
		         [ignore] "=&r" (ignore)
		       : "0" (increment), [ptr] "r" (ptr)
		       );
#else
  __asm__ __volatile__("1: ldstub [%4 + 3], %1\n"
		       "\torcc %1, 0, %2\n"
		       "\tbne 1b\n" /* go back until we have the lock */
		       "\tld [%4], %0\n"
		       "\tsra %0, 8, %0\n"
		       "\tsub %0, 1, %0\n"
		       "\tsll %0, 8, %1\n"
		       "\tst %1,[%4]\n" /* Release the lock */
		       : "=&r" (increment), "=r" (lock), "=&r" (ignore)
		       : "0" (increment), "r" (ptr)
		       );
#endif

  return increment == 0;
}

/***** MIPS *****/
/* This is disabled because the asm code is broken on most MIPS
 * processors and doesn't generally compile. */
#elif defined(HAVE_CPU_MIPS) && defined(__GNUC__) && 0

GST_INLINE_FUNC void 	gst_atomic_int_init 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC void 	gst_atomic_int_destroy 	(GstAtomicInt *aint) { } 
GST_INLINE_FUNC void 	gst_atomic_int_set 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC gint 	gst_atomic_int_read 	(GstAtomicInt *aint) { return aint->counter; }

/* this only works on MIPS II and better */
GST_INLINE_FUNC void 
gst_atomic_int_add (GstAtomicInt *aint, gint val)
{
  unsigned long temp;

  __asm__ __volatile__(
    "1:   ll      %0, %1      # atomic_add\n"
    "     addu    %0, %2                  \n"
    "     sc      %0, %1                  \n"
    "     beqz    %0, 1b                  \n"
      : "=&r" (temp), "=m" (aint->counter)
      : "Ir" (val), "m" (aint->counter));
}

GST_INLINE_FUNC void
gst_atomic_int_inc (GstAtomicInt *aint)
{
  gst_atomic_int_add (aint, 1);
}

GST_INLINE_FUNC gboolean
gst_atomic_int_dec_and_test (GstAtomicInt *aint)
{
  unsigned long temp, result;
  int val = 1;

  __asm__ __volatile__(
    ".set push                                   \n"
    ".set noreorder           # atomic_sub_return\n"
    "1:   ll    %1, %2                           \n"
    "     subu  %0, %1, %3                       \n"
    "     sc    %0, %2                           \n"
    "     beqz  %0, 1b                           \n"
    "     subu  %0, %1, %3                       \n"
    ".set pop                                    \n"
      : "=&r" (result), "=&r" (temp), "=m" (aint->counter)
      : "Ir" (val), "m" (aint->counter)
      : "memory");

  return result == 0;
}

/***** S/390 *****/
#elif defined(HAVE_CPU_S390) && defined(__GNUC__)

GST_INLINE_FUNC void 	gst_atomic_int_init 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC void 	gst_atomic_int_destroy 	(GstAtomicInt *aint) { } 
GST_INLINE_FUNC void 	gst_atomic_int_set 	(GstAtomicInt *aint, gint val) { aint->counter = val; }
GST_INLINE_FUNC gint 	gst_atomic_int_read 	(GstAtomicInt *aint) { return aint->counter; }

#define __CS_LOOP(old_val, new_val, ptr, op_val, op_string)             \
        __asm__ __volatile__("   l     %0,0(%3)\n"                      \
                             "0: lr    %1,%0\n"                         \
                             op_string "  %1,%4\n"                      \
                             "   cs    %0,%1,0(%3)\n"                   \
                             "   jl    0b"                              \
                             : "=&d" (old_val), "=&d" (new_val),        \
                               "+m" (((atomic_t *)(ptr))->counter)      \
                             : "a" (ptr), "d" (op_val) : "cc" );

GST_INLINE_FUNC void 
gst_atomic_int_add (GstAtomicInt *aint, gint val)
{
  int old_val, new_val;
  __CS_LOOP(old_val, new_val, aint, val, "ar");
}

GST_INLINE_FUNC void
gst_atomic_int_inc (GstAtomicInt *aint)
{
  int old_val, new_val;
  __CS_LOOP(old_val, new_val, aint, 1, "ar");
}

GST_INLINE_FUNC gboolean
gst_atomic_int_dec_and_test (GstAtomicInt *aint)
{
  int old_val, new_val;
  __CS_LOOP(old_val, new_val, aint, 1, "sr");
  return new_val == 0;
}

#else 
#warning consider putting your architecture specific atomic implementations here

/*
 * generic implementation
 */
GST_INLINE_FUNC void
gst_atomic_int_init (GstAtomicInt *aint, gint val)
{
  aint->counter = val;
  aint->lock = g_mutex_new ();
}

GST_INLINE_FUNC void
gst_atomic_int_destroy (GstAtomicInt *aint)
{
  g_mutex_free (aint->lock);
}

GST_INLINE_FUNC void
gst_atomic_int_set (GstAtomicInt *aint, gint val)
{
  g_mutex_lock (aint->lock);
  aint->counter = val;
  g_mutex_unlock (aint->lock);
}

GST_INLINE_FUNC gint
gst_atomic_int_read (GstAtomicInt *aint)
{
  gint res;

  g_mutex_lock (aint->lock);
  res = aint->counter;
  g_mutex_unlock (aint->lock);

  return res;
}

GST_INLINE_FUNC void 
gst_atomic_int_add (GstAtomicInt *aint, gint val)
{
  g_mutex_lock (aint->lock);
  aint->counter += val;
  g_mutex_unlock (aint->lock);
}

GST_INLINE_FUNC void
gst_atomic_int_inc (GstAtomicInt *aint)
{
  g_mutex_lock (aint->lock);
  aint->counter++;
  g_mutex_unlock (aint->lock);
}

GST_INLINE_FUNC gboolean
gst_atomic_int_dec_and_test (GstAtomicInt *aint)
{
  gboolean res;
  
  g_mutex_lock (aint->lock);
  aint->counter--;
  res = (aint->counter == 0);
  g_mutex_unlock (aint->lock);

  return res;
}

#endif 
/*
 * common functions
 */ 
GST_INLINE_FUNC GstAtomicInt*
gst_atomic_int_new (gint val)
{
  GstAtomicInt *aint;

  aint = g_new0 (GstAtomicInt, 1);
  gst_atomic_int_init (aint, val);

  return aint;
}

GST_INLINE_FUNC void
gst_atomic_int_free (GstAtomicInt *aint)
{
  gst_atomic_int_destroy (aint);
  g_free (aint);
}

#endif /* defined (GST_CAN_INLINE) || defined (__GST_TRASH_STACK_C__)*/

G_END_DECLS

#endif /*  __GST_ATOMIC_IMPL_H__ */
