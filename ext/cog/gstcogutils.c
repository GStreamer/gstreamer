/* Cog
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

#include "gstcogutils.h"

#include <gst/gst.h>
#include <gst/video/video.h>
#include <cog/cog.h>
#include <cog/cogvirtframe.h>
#include <math.h>
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (cog_debug);
#define GST_CAT_DEFAULT cog_debug




static void
gst_cog_frame_free (CogFrame * frame, void *priv)
{
  gst_buffer_unref (GST_BUFFER (priv));
}

CogFrame *
gst_cog_buffer_wrap (GstBuffer * buf, GstVideoFormat format, int width,
    int height)
{
  CogFrame *frame;
  int size;

  size = gst_video_format_get_size (format, width, height);
  if (GST_BUFFER_SIZE (buf) != size) {
    GST_ERROR ("size incorrect, expected %d, got %d", size,
        GST_BUFFER_SIZE (buf));
  }

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      frame =
          cog_frame_new_from_data_I420 (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_YV12:
      frame =
          cog_frame_new_from_data_YV12 (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_YUY2:
      frame =
          cog_frame_new_from_data_YUY2 (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_UYVY:
      frame =
          cog_frame_new_from_data_UYVY (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_AYUV:
      frame =
          cog_frame_new_from_data_AYUV (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_Y42B:
      frame =
          cog_frame_new_from_data_Y42B (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_Y444:
      frame =
          cog_frame_new_from_data_Y444 (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_v210:
      frame =
          cog_frame_new_from_data_v210 (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_v216:
      frame =
          cog_frame_new_from_data_v216 (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_RGBx:
      frame =
          cog_frame_new_from_data_RGBx (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_BGRx:
      frame =
          cog_frame_new_from_data_BGRx (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_xRGB:
      frame =
          cog_frame_new_from_data_xRGB (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_xBGR:
      frame =
          cog_frame_new_from_data_xBGR (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_RGBA:
      frame =
          cog_frame_new_from_data_RGBA (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_BGRA:
      frame =
          cog_frame_new_from_data_BGRA (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_ARGB:
      frame =
          cog_frame_new_from_data_ARGB (GST_BUFFER_DATA (buf), width, height);
      break;
    case GST_VIDEO_FORMAT_ABGR:
      frame =
          cog_frame_new_from_data_ABGR (GST_BUFFER_DATA (buf), width, height);
      break;
    default:
      g_assert_not_reached ();
      return NULL;
  }
  cog_frame_set_free_callback (frame, gst_cog_frame_free, buf);

  return frame;
}

#if 0
#ifdef GST_BUFFER_FREE_FUNC
static void
cog_buf_free_func (gpointer priv)
{
  CogBuffer *buffer = (CogBuffer *) priv;

  cog_buffer_unref (buffer);
}
#endif

/* takes the reference */
GstBuffer *
gst_cog_wrap_cog_buffer (CogBuffer * buffer)
{
  GstBuffer *gstbuf;

#ifdef GST_BUFFER_FREE_FUNC
  gstbuf = gst_buffer_new ();
  GST_BUFFER_DATA (gstbuf) = buffer->data;
  GST_BUFFER_SIZE (gstbuf) = buffer->length;
  GST_BUFFER_MALLOCDATA (gstbuf) = (void *) buffer;
  GST_BUFFER_FREE_FUNC (gstbuf) = cog_buf_free_func;
#else
  gstbuf = gst_buffer_new_and_alloc (buffer->length);
  memcpy (GST_BUFFER_DATA (gstbuf), buffer->data, buffer->length);
#endif

  return gstbuf;
}

static void
gst_cog_buffer_free (CogBuffer * buffer, void *priv)
{
  gst_buffer_unref (GST_BUFFER (priv));
}

CogBuffer *
gst_cog_wrap_gst_buffer (GstBuffer * buffer)
{
  CogBuffer *cogbuf;

  cogbuf = cog_buffer_new_with_data (GST_BUFFER_DATA (buffer),
      GST_BUFFER_SIZE (buffer));
  cogbuf->free = gst_cog_buffer_free;
  cogbuf->priv = buffer;

  return cogbuf;
}
#endif
