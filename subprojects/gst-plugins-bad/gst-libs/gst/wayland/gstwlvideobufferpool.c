/* GStreamer Wayland Library
 *
 * Copyright (C) 2017 Collabora Ltd.
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwlvideobufferpool.h"

G_DEFINE_TYPE (GstWlVideoBufferPool, gst_wl_video_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL);

static const gchar **
gst_wl_video_buffer_pool_get_options (GstBufferPool * pool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL };
  return options;
}

static void
gst_wl_video_buffer_pool_class_init (GstWlVideoBufferPoolClass * klass)
{
  GstBufferPoolClass *pool_class = GST_BUFFER_POOL_CLASS (klass);
  pool_class->get_options = gst_wl_video_buffer_pool_get_options;
}

static void
gst_wl_video_buffer_pool_init (GstWlVideoBufferPool * pool)
{
}

GstBufferPool *
gst_wl_video_buffer_pool_new (void)
{
  return (GstBufferPool *) g_object_new (GST_TYPE_WL_VIDEO_BUFFER_POOL, NULL);
}
