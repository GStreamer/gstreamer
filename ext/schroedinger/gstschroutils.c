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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include "gstschroutils.h"

//#define SCHRO_ENABLE_UNSTABLE_API

#include <gst/gst.h>
#include <gst/video/video.h>
#include <schroedinger/schro.h>
#include <schroedinger/schrobitstream.h>
#include <schroedinger/schrovirtframe.h>
#include <string.h>

GST_DEBUG_CATEGORY_EXTERN (schro_debug);
#define GST_CAT_DEFAULT schro_debug

typedef struct
{
  GstVideoFrame frame;
} FrameData;


static void
gst_schro_frame_free (SchroFrame * frame, void *priv)
{
  FrameData *data = priv;

  gst_video_frame_unmap (&data->frame);

  g_slice_free (FrameData, data);
}

GstBuffer *
gst_schro_frame_get_buffer (SchroFrame * frame)
{
  if (frame->priv)
    return gst_buffer_ref (((FrameData *) frame->priv)->frame.buffer);

  return NULL;
}

SchroFrame *
gst_schro_buffer_wrap (GstBuffer * buf, gboolean write, GstVideoInfo * vinfo)
{
  SchroFrame *frame;
  GstVideoFrame vframe;
  FrameData *data;
  gint i;

  if (!gst_video_frame_map (&vframe, vinfo, buf,
          (write ? GST_MAP_READWRITE : GST_MAP_READ)))
    return NULL;

  frame = schro_frame_new ();

  frame->width = GST_VIDEO_FRAME_WIDTH (&vframe);
  frame->height = GST_VIDEO_FRAME_HEIGHT (&vframe);

  switch (GST_VIDEO_FRAME_FORMAT (&vframe)) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      frame->format = SCHRO_FRAME_FORMAT_U8_420;
      break;
    case GST_VIDEO_FORMAT_YUY2:
      frame->format = SCHRO_FRAME_FORMAT_YUYV;
      break;
    case GST_VIDEO_FORMAT_UYVY:
      frame->format = SCHRO_FRAME_FORMAT_UYVY;
      break;
    case GST_VIDEO_FORMAT_AYUV:
      frame->format = SCHRO_FRAME_FORMAT_AYUV;
      break;
#if SCHRO_CHECK_VERSION(1,0,12)
    case GST_VIDEO_FORMAT_ARGB:
      frame->format = SCHRO_FRAME_FORMAT_ARGB;
      break;
#endif
#if SCHRO_CHECK_VERSION(1,0,11)
    case GST_VIDEO_FORMAT_Y42B:
      frame->format = SCHRO_FRAME_FORMAT_U8_422;
      break;
    case GST_VIDEO_FORMAT_Y444:
      frame->format = SCHRO_FRAME_FORMAT_U8_444;
      break;
    case GST_VIDEO_FORMAT_v210:
      frame->format = SCHRO_FRAME_FORMAT_v210;
      break;
    case GST_VIDEO_FORMAT_v216:
      frame->format = SCHRO_FRAME_FORMAT_v216;
      break;
    case GST_VIDEO_FORMAT_AYUV64:
      frame->format = SCHRO_FRAME_FORMAT_AY64;
      break;
#endif
    default:
      g_assert_not_reached ();
      return NULL;
  }

  if (SCHRO_FRAME_IS_PACKED (frame->format)) {
    frame->components[0].format = frame->format;
    frame->components[0].width = frame->width;
    frame->components[0].height = frame->height;
    frame->components[0].stride = GST_VIDEO_FRAME_COMP_STRIDE (&vframe, 0);
    frame->components[0].length = frame->components[0].stride * frame->height;
    frame->components[0].data = vframe.data[0];
    frame->components[0].v_shift = 0;
    frame->components[0].h_shift = 0;
  } else {
    for (i = 0; i < GST_VIDEO_FRAME_N_COMPONENTS (&vframe); i++) {
      frame->components[i].format = frame->format;
      frame->components[i].width = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, i);
      frame->components[i].height = GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, i);
      frame->components[i].stride = GST_VIDEO_FRAME_COMP_STRIDE (&vframe, i);
      frame->components[i].length =
          frame->components[i].stride * frame->components[i].height;
      frame->components[i].data = GST_VIDEO_FRAME_COMP_DATA (&vframe, i);
      if (i == 0) {
        frame->components[i].v_shift = 0;
        frame->components[i].h_shift = 0;
      } else {
        frame->components[i].v_shift =
            SCHRO_FRAME_FORMAT_H_SHIFT (frame->format);
        frame->components[i].h_shift =
            SCHRO_FRAME_FORMAT_H_SHIFT (frame->format);
      }
    }
  }

  data = g_slice_new0 (FrameData);
  data->frame = vframe;
  schro_frame_set_free_callback (frame, gst_schro_frame_free, data);

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

typedef struct
{
  GstMemory *mem;
  GstMapInfo info;
} BufferData;

static void
gst_schro_buffer_free (SchroBuffer * buffer, void *priv)
{
  BufferData *data = priv;

  gst_memory_unmap (data->mem, &data->info);
  gst_memory_unref (data->mem);
  g_slice_free (BufferData, priv);
}

SchroBuffer *
gst_schro_wrap_gst_buffer (GstBuffer * buffer)
{
  SchroBuffer *schrobuf;
  GstMemory *mem;
  GstMapInfo info;
  BufferData *data;

  mem = gst_buffer_get_all_memory (buffer);
  if (!gst_memory_map (mem, &info, GST_MAP_READ)) {
    GST_ERROR ("Couldn't get readable memory from gstbuffer");
    return NULL;
  }

  /* FIXME : We can't control if data won't be read/write outside
   * of schro ... */
  data = g_slice_new0 (BufferData);
  data->info = info;
  data->mem = mem;

  schrobuf = schro_buffer_new_with_data (info.data, info.size);
  schrobuf->free = gst_schro_buffer_free;
  schrobuf->priv = data;

  return schrobuf;
}
