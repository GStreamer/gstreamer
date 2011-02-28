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

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      frame =
          schro_frame_new_from_data_I420 (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_YV12:
      frame =
          schro_frame_new_from_data_YV12 (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      frame =
          schro_frame_new_from_data_YUY2 (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      frame =
          schro_frame_new_from_data_UYVY (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      frame =
          schro_frame_new_from_data_AYUV (GST_BUFFER_DATA (buf), width, height);
      break;
#if 0
    case GST_VIDEO_FORMAT_ARGB:
    {
      SchroFrame *rgbframe =
          schro_frame_new_from_data_AYUV (GST_BUFFER_DATA (buf), width, height);
      SchroFrame *vframe1;
      SchroFrame *vframe2;
      SchroFrame *vframe3;

      vframe1 = schro_virt_frame_new_unpack (rgbframe);
      vframe2 = schro_virt_frame_new_color_matrix (vframe1);
      vframe3 =
          schro_virt_frame_new_subsample (vframe2, SCHRO_FRAME_FORMAT_U8_420);

      frame = schro_frame_new_and_alloc (NULL, SCHRO_FRAME_FORMAT_U8_420,
          width, height);
      schro_virt_frame_render (vframe3, frame);
      schro_frame_unref (vframe3);
    }
      break;
#endif
    default:
      g_assert_not_reached ();
      return NULL;
  }
  schro_frame_set_free_callback (frame, gst_schro_frame_free, buf);

  return frame;
}

#ifdef GST_BUFFER_FREE_FUNC
static void
schro_buf_free_func (gpointer priv)
{
  SchroBuffer *buffer = (SchroBuffer *) priv;

  schro_buffer_unref (buffer);
}
#endif

/* takes the reference */
GstBuffer *
gst_schro_wrap_schro_buffer (SchroBuffer * buffer)
{
  GstBuffer *gstbuf;

#ifdef GST_BUFFER_FREE_FUNC
  gstbuf = gst_buffer_new ();
  GST_BUFFER_DATA (gstbuf) = buffer->data;
  GST_BUFFER_SIZE (gstbuf) = buffer->length;
  GST_BUFFER_MALLOCDATA (gstbuf) = (void *) buffer;
  GST_BUFFER_FREE_FUNC (gstbuf) = schro_buf_free_func;
#else
  gstbuf = gst_buffer_new_and_alloc (buffer->length);
  memcpy (GST_BUFFER_DATA (gstbuf), buffer->data, buffer->length);
#endif

  return gstbuf;
}

static void
gst_schro_buffer_free (SchroBuffer * buffer, void *priv)
{
  gst_buffer_unref (GST_BUFFER (priv));
}

SchroBuffer *
gst_schro_wrap_gst_buffer (GstBuffer * buffer)
{
  SchroBuffer *schrobuf;

  schrobuf = schro_buffer_new_with_data (GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
  schrobuf->free = gst_schro_buffer_free;
  schrobuf->priv = buffer;

  return schrobuf;
}
