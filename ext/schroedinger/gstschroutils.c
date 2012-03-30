/* Schrodinger
 * Copyright (C) 2008 David Schleef <ds@schleef.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstschroutils.h"

//#define SCHRO_ENABLE_UNSTABLE_API

#include <gst/gst.h>
#include <gst/video/video.h>
#include <schroedinger/schro.h>
#include <schroedinger/schrobitstream.h>
#include <schroedinger/schrovirtframe.h>
#include <math.h>
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (schro_debug);
#define GST_CAT_DEFAULT schro_debug




static void
gst_schro_frame_free (SchroFrame * frame, void *priv)
{
  gst_buffer_unref (GST_BUFFER (priv));
}

SchroFrame *
gst_schro_buffer_wrap (GstBuffer * buf, GstVideoFormat format, int width,
    int height)
{
  SchroFrame *frame;
  GstMapInfo info;

  if (!gst_buffer_map (buf, &info, GST_MAP_READ))
    return NULL;

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      frame = schro_frame_new_from_data_I420 (info.data, width, height);
      break;
    case GST_VIDEO_FORMAT_YV12:
      frame = schro_frame_new_from_data_YV12 (info.data, width, height);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      frame = schro_frame_new_from_data_YUY2 (info.data, width, height);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      frame = schro_frame_new_from_data_UYVY (info.data, width, height);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      frame = schro_frame_new_from_data_AYUV (info.data, width, height);
      break;
#if SCHRO_CHECK_VERSION(1,0,12)
    case GST_VIDEO_FORMAT_ARGB:
      frame = schro_frame_new_from_data_ARGB (info.data, width, height);
      break;
#endif
#if SCHRO_CHECK_VERSION(1,0,11)
    case GST_VIDEO_FORMAT_Y42B:
      frame = schro_frame_new_from_data_Y42B (info.data, width, height);
      break;
    case GST_VIDEO_FORMAT_Y444:
      frame = schro_frame_new_from_data_Y444 (info.data, width, height);
      break;
    case GST_VIDEO_FORMAT_v210:
      frame = schro_frame_new_from_data_v210 (info.data, width, height);
      break;
    case GST_VIDEO_FORMAT_v216:
      frame = schro_frame_new_from_data_v216 (info.data, width, height);
      break;
    case GST_VIDEO_FORMAT_AYUV64:
      frame = schro_frame_new_from_data_AY64 (info.data, width, height);
      break;
#endif
    default:
      g_assert_not_reached ();
      return NULL;
  }
  schro_frame_set_free_callback (frame, gst_schro_frame_free, buf);

  gst_buffer_unmap (buf, &info);

  return frame;
}

static void
schro_buf_free_func (gpointer priv)
{
  SchroBuffer *buffer = (SchroBuffer *) priv;

  schro_buffer_unref (buffer);
}

/* takes the reference */
GstBuffer *
gst_schro_wrap_schro_buffer (SchroBuffer * buffer)
{
  GstMemory *mem;
  GstBuffer *buf;

  mem =
      gst_memory_new_wrapped (0, buffer->data, buffer->length, 0,
      buffer->length, buffer, schro_buf_free_func);
  buf = gst_buffer_new ();
  gst_buffer_append_memory (buf, mem);

  return buf;
}

static void
gst_schro_buffer_free (SchroBuffer * buffer, void *priv)
{
  gst_memory_unref (GST_MEMORY_CAST (priv));
}

SchroBuffer *
gst_schro_wrap_gst_buffer (GstBuffer * buffer)
{
  SchroBuffer *schrobuf;
  GstMemory *mem;
  GstMapInfo info;

  mem = gst_buffer_get_all_memory (buffer);
  if (!gst_memory_map (mem, &info, GST_MAP_READ)) {
    GST_ERROR ("Couldn't get readable memory from gstbuffer");
    return NULL;
  }

  /* FIXME : We can't control if data won't be read/write outside
   * of schro ... */
  schrobuf = schro_buffer_new_with_data (info.data, info.size);
  gst_memory_unmap (mem, &info);
  schrobuf->free = gst_schro_buffer_free;
  schrobuf->priv = mem;

  return schrobuf;
}
