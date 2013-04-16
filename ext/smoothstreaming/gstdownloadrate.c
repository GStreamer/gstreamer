/* GStreamer
 * Copyright (C) 2011 Andoni Morales Alastruey <ylatuya@gmail.com>
 * Copyright (C) 2012 Smart TV Alliance
 *  Author: Louis-Francis Ratté-Boulianne <lfrb@collabora.com>, Collabora Ltd.
 *
 * gstfragment.c:
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

#include <glib.h>
#include "gstdownloadrate.h"

static void
_gst_download_rate_check_remove_rates (GstDownloadRate * rate)
{
  if (rate->max_length == 0)
    return;

  while (g_queue_get_length (&rate->queue) > rate->max_length) {
    guint bitrate = GPOINTER_TO_UINT (g_queue_pop_head (&rate->queue));

    rate->total -= bitrate;
  }
}

void
gst_download_rate_init (GstDownloadRate * rate)
{
  g_queue_init (&rate->queue);
  g_mutex_init (&rate->mutex);
  rate->total = 0;
  rate->max_length = 0;
}

void
gst_download_rate_deinit (GstDownloadRate * rate)
{
  gst_download_rate_clear (rate);
  g_mutex_clear (&rate->mutex);
}

void
gst_download_rate_set_max_length (GstDownloadRate * rate, gint max_length)
{
  g_mutex_lock (&rate->mutex);
  rate->max_length = max_length;
  _gst_download_rate_check_remove_rates (rate);
  g_mutex_unlock (&rate->mutex);
}

gint
gst_download_rate_get_max_length (GstDownloadRate * rate)
{
  guint ret;
  g_mutex_lock (&rate->mutex);
  ret = rate->max_length;
  g_mutex_unlock (&rate->mutex);

  return ret;
}

void
gst_download_rate_clear (GstDownloadRate * rate)
{
  g_mutex_lock (&rate->mutex);
  g_queue_clear (&rate->queue);
  rate->total = 0;
  g_mutex_unlock (&rate->mutex);
}

void
gst_download_rate_add_rate (GstDownloadRate * rate, guint bytes, guint64 time)
{
  guint64 bitrate;
  g_mutex_lock (&rate->mutex);

  /* convert from bytes / nanoseconds to bits per second */
  bitrate = G_GUINT64_CONSTANT (8000000000) * bytes / time;

  g_queue_push_tail (&rate->queue, GUINT_TO_POINTER ((guint) bitrate));
  rate->total += bitrate;

  _gst_download_rate_check_remove_rates (rate);
  g_mutex_unlock (&rate->mutex);
}

guint
gst_download_rate_get_current_rate (GstDownloadRate * rate)
{
  guint ret;
  g_mutex_lock (&rate->mutex);
  if (g_queue_get_length (&rate->queue))
    ret = rate->total / g_queue_get_length (&rate->queue);
  else
    ret = G_MAXUINT;
  g_mutex_unlock (&rate->mutex);

  return ret;
}
