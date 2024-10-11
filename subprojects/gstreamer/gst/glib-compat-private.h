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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GLIB_COMPAT_PRIVATE_H__
#define __GLIB_COMPAT_PRIVATE_H__

#include <glib.h>

G_BEGIN_DECLS

/* copies */

/* adaptations */
#if !GLIB_CHECK_VERSION(2, 67, 4)
#define g_memdup2(ptr,sz) ((G_LIKELY(((guint64)(sz)) < G_MAXUINT)) ? g_memdup(ptr,sz) : (g_abort(),NULL))
#endif

#if !GLIB_CHECK_VERSION(2, 81, 1)
#define g_sort_array(a,n,s,f,udata) gst_g_sort_array(a,n,s,f,udata)

// Don't need to maintain ABI compat here (n_elements), since we never pass
// the function as pointer but always call it directly ourselves.
static inline void
gst_g_sort_array (const void       *array,
                  gssize            n_elements,
                  size_t            element_size,
                  GCompareDataFunc  compare_func,
                  void             *user_data)
{
  if (n_elements >= 0 && n_elements <= G_MAXINT) {
    g_qsort_with_data (array, n_elements, element_size, compare_func, user_data);
  } else {
    g_abort ();
  }
}
#endif

G_END_DECLS

#endif
