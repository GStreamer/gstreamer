/*
 * glib-compat.c
 * Functions copied from glib 2.10
 *
 * Copyright 2005 David Schleef <ds@schleef.org>
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

#ifndef __GLIB_COMPAT_PRIVATE_H__
#define __GLIB_COMPAT_PRIVATE_H__

#include <glib.h>

G_BEGIN_DECLS

#if !GLIB_CHECK_VERSION(2,25,0)

#if defined (_MSC_VER) && !defined(_WIN64)
typedef struct _stat32 GStatBuf;
#else
typedef struct stat GStatBuf;
#endif

#endif

#if GLIB_CHECK_VERSION(2,26,0)
#define GLIB_HAS_GDATETIME
#endif

/* See bug #651514 */
#if GLIB_CHECK_VERSION(2,29,5)
#define G_ATOMIC_POINTER_COMPARE_AND_EXCHANGE(a,b,c) \
    g_atomic_pointer_compare_and_exchange ((a),(b),(c))
#define G_ATOMIC_INT_COMPARE_AND_EXCHANGE(a,b,c) \
    g_atomic_int_compare_and_exchange ((a),(b),(c))
#else
#define G_ATOMIC_POINTER_COMPARE_AND_EXCHANGE(a,b,c) \
    g_atomic_pointer_compare_and_exchange ((volatile gpointer *)(a),(b),(c))
#define G_ATOMIC_INT_COMPARE_AND_EXCHANGE(a,b,c) \
    g_atomic_int_compare_and_exchange ((volatile int *)(a),(b),(c))
#endif

/* See bug #651514 */
#if GLIB_CHECK_VERSION(2,29,5)
#define G_ATOMIC_INT_ADD(a,b) g_atomic_int_add ((a),(b))
#else
#define G_ATOMIC_INT_ADD(a,b) g_atomic_int_exchange_and_add ((a),(b))
#endif

/* copies */

/* adaptations */

G_END_DECLS

#endif
