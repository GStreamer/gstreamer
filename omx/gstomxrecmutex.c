/*
 * Copyright (C) 2012, Collabora Ltd.
 *   Author: George Kiagiadakis <george.kiagiadakis@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstomxrecmutex.h"

void
gst_omx_rec_mutex_init (GstOMXRecMutex * mutex)
{
  mutex->lock = g_mutex_new ();
  mutex->recursion_lock = g_mutex_new ();
  mutex->recursion_allowed = FALSE;
}

void
gst_omx_rec_mutex_clear (GstOMXRecMutex * mutex)
{
  g_mutex_free (mutex->lock);
  g_mutex_free (mutex->recursion_lock);
}

void
gst_omx_rec_mutex_lock (GstOMXRecMutex * mutex)
{
  g_mutex_lock (mutex->lock);
}

void
gst_omx_rec_mutex_unlock (GstOMXRecMutex * mutex)
{
  g_mutex_unlock (mutex->lock);
}

/* must be called with mutex->lock taken */
void
gst_omx_rec_mutex_begin_recursion (GstOMXRecMutex * mutex)
{
  g_mutex_lock (mutex->recursion_lock);
  g_assert (mutex->recursion_allowed == FALSE);
  mutex->recursion_allowed = TRUE;
  g_mutex_unlock (mutex->recursion_lock);
}

/* must be called with mutex->lock taken */
void
gst_omx_rec_mutex_end_recursion (GstOMXRecMutex * mutex)
{
  g_mutex_lock (mutex->recursion_lock);
  g_assert (mutex->recursion_allowed == TRUE);
  mutex->recursion_allowed = FALSE;
  g_mutex_unlock (mutex->recursion_lock);
}

void
gst_omx_rec_mutex_recursive_lock (GstOMXRecMutex * mutex)
{
  g_mutex_lock (mutex->recursion_lock);
  if (!mutex->recursion_allowed) {
    /* no recursion allowed, lock the proper mutex */
    g_mutex_unlock (mutex->recursion_lock);
    g_mutex_lock (mutex->lock);
  }
}

void
gst_omx_rec_mutex_recursive_unlock (GstOMXRecMutex * mutex)
{
  /* It is safe to check recursion_allowed here because
   * we hold at least one of the two locks and
   * either lock protects it from being changed.
   */
  if (mutex->recursion_allowed) {
    g_mutex_unlock (mutex->recursion_lock);
  } else {
    g_mutex_unlock (mutex->lock);
  }
}
