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

#define G_COMPAT_DEFINE(new_api, new_args, old_api, old_args)   \
static inline void                                              \
new_api new_args                                                \
{                                                               \
    old_api old_args;                                           \
}

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

#if !GLIB_CHECK_VERSION(2,31,2)
typedef GStaticMutex GCompatMutex;
G_COMPAT_DEFINE(g_compat_mutex_init, (GCompatMutex *mutex),
                g_static_mutex_init, (mutex))
G_COMPAT_DEFINE(g_compat_mutex_clear, (GCompatMutex *mutex),
                g_static_mutex_free, (mutex))
G_COMPAT_DEFINE(g_compat_mutex_lock, (GCompatMutex *mutex),
                g_static_mutex_lock, (mutex))
G_COMPAT_DEFINE(g_compat_mutex_unlock, (GCompatMutex *mutex),
                g_static_mutex_unlock, (mutex))

typedef GStaticRecMutex GCompatRecMutex;
G_COMPAT_DEFINE(g_compat_rec_mutex_init, (GCompatRecMutex *mutex),
                g_static_rec_mutex_init, (mutex))
G_COMPAT_DEFINE(g_compat_rec_mutex_clear, (GCompatRecMutex *mutex),
                g_static_rec_mutex_free, (mutex))
G_COMPAT_DEFINE(g_compat_rec_mutex_lock, (GCompatRecMutex *mutex),
                g_static_rec_mutex_lock, (mutex))
G_COMPAT_DEFINE(g_compat_rec_mutex_unlock, (GCompatRecMutex *mutex),
                g_static_rec_mutex_unlock, (mutex))

typedef GCond *GCompatCond;
G_COMPAT_DEFINE(g_compat_cond_init, (GCompatCond *cond),
                *cond = g_cond_new, ())
G_COMPAT_DEFINE(g_compat_cond_clear, (GCompatCond *cond),
                if (*cond) g_cond_free, (*cond))
G_COMPAT_DEFINE(g_compat_cond_signal, (GCompatCond *cond),
                g_cond_signal, (*cond))
G_COMPAT_DEFINE(g_compat_cond_broadcast, (GCompatCond *cond),
                g_cond_broadcast, (*cond))
G_COMPAT_DEFINE(g_compat_cond_wait, (GCompatCond *cond, GCompatMutex *mutex),
                g_cond_wait, (*cond, g_static_mutex_get_mutex(mutex)))

static inline gboolean
g_cond_wait_until(GCompatCond *cond, GStaticMutex *mutex, gint64 end_time)
{
    gint64 diff_time;
    GTimeVal timeout;

    diff_time = end_time - g_get_monotonic_time();
    g_get_current_time(&timeout);
    g_time_val_add(&timeout, diff_time > 0 ? diff_time : 0);
    return g_cond_timed_wait(*cond, g_static_mutex_get_mutex(mutex), &timeout);
}

#define GMutex                          GCompatMutex
#undef  g_mutex_init
#define g_mutex_init(mutex)             g_compat_mutex_init(mutex)
#undef  g_mutex_clear
#define g_mutex_clear(mutex)            g_compat_mutex_clear(mutex)
#undef  g_mutex_lock
#define g_mutex_lock(mutex)             g_compat_mutex_lock(mutex)
#undef  g_mutex_unlock
#define g_mutex_unlock(mutex)           g_compat_mutex_unlock(mutex)

#define GRecMutex                       GCompatRecMutex
#undef  g_rec_mutex_init
#define g_rec_mutex_init(mutex)         g_compat_rec_mutex_init(mutex)
#undef  g_rec_mutex_clear
#define g_rec_mutex_clear(mutex)        g_compat_rec_mutex_clear(mutex)
#undef  g_rec_mutex_lock
#define g_rec_mutex_lock(mutex)         g_compat_rec_mutex_lock(mutex)
#undef  g_rec_mutex_unlock
#define g_rec_mutex_unlock(mutex)       g_compat_rec_mutex_unlock(mutex)

#define GCond                           GCompatCond
#undef  g_cond_init
#define g_cond_init(cond)               g_compat_cond_init(cond)
#undef  g_cond_clear
#define g_cond_clear(cond)              g_compat_cond_clear(cond)
#undef  g_cond_signal
#define g_cond_signal(cond)             g_compat_cond_signal(cond)
#undef  g_cond_wait
#define g_cond_wait(cond, mutex)        g_compat_cond_wait(cond, mutex)
#endif

#if !GLIB_CHECK_VERSION(2,31,18)
static inline gpointer
g_async_queue_timeout_pop(GAsyncQueue *queue, guint64 timeout)
{
    GTimeVal end_time;

    g_get_current_time(&end_time);
    g_time_val_add(&end_time, timeout);
    return g_async_queue_timed_pop(queue, &end_time);
}
#endif

#undef G_COMPAT_DEFINE

#endif /* GLIB_COMPAT_H */
