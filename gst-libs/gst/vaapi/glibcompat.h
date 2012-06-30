/*
 *  glibcompat.h - System-dependent definitions
 *
 *  Copyright (C) 2012 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#ifndef GLIB_COMPAT_H
#define GLIB_COMPAT_H

#include <glib.h>
#include <glib-object.h>

#if !GLIB_CHECK_VERSION(2,27,2)
static inline void
g_list_free_full(GList *list, GDestroyNotify free_func)
{
    g_list_foreach(list, (GFunc)free_func, NULL);
    g_list_free(list);
}
#endif

#if !GLIB_CHECK_VERSION(2,28,0)
static inline void
g_clear_object_inline(volatile GObject **object_ptr)
{
    gpointer * const ptr = (gpointer)object_ptr;
    gpointer old;

    do {
        old = g_atomic_pointer_get(ptr);
    } while G_UNLIKELY(!g_atomic_pointer_compare_and_exchange(ptr, old, NULL));

    if (old)
        g_object_unref(old);
}
#undef  g_clear_object
#define g_clear_object(obj) g_clear_object_inline((volatile GObject **)(obj))
#endif

#if GLIB_CHECK_VERSION(2,31,2)
#define GStaticMutex                    GMutex
#undef  g_static_mutex_init
#define g_static_mutex_init(mutex)      g_mutex_init(mutex)
#undef  g_static_mutex_free
#define g_static_mutex_free(mutex)      g_mutex_clear(mutex)
#undef  g_static_mutex_lock
#define g_static_mutex_lock(mutex)      g_mutex_lock(mutex)
#undef  g_static_mutex_unlock
#define g_static_mutex_unlock(mutex)    g_mutex_unlock(mutex)

#define GStaticRecMutex                 GRecMutex
#undef  g_static_rec_mutex_init
#define g_static_rec_mutex_init(mutex)  g_rec_mutex_init(mutex)
#undef  g_static_rec_mutex_free
#define g_static_rec_mutex_free(mutex)  g_rec_mutex_clear(mutex)
#undef  g_static_rec_mutex_lock
#define g_static_rec_mutex_lock(mutex)  g_rec_mutex_lock(mutex)
#undef  g_static_rec_mutex_unlock
#define g_static_rec_mutex_unlock(m)    g_rec_mutex_unlock(m)
#endif

#endif /* GLIB_COMPAT_H */
