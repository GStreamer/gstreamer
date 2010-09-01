/*
* GStreamer
* Copyright (C) 2009 Carl-Anton Ingmarsson <ca.ingmarsson@gmail.com>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Library General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
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

#include "gstvideoframe.h"

GST_DEBUG_CATEGORY_STATIC (gst_video_frame_debug);
#define GST_CAT_DEFAULT gst_video_frame_debug

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_video_frame_debug, "gstvideoframe", 0, "Video Frame");

GstVideoFrame *
gst_video_frame_new (void)
{
  GstVideoFrame *frame;

  frame = (GstVideoFrame *) gst_mini_object_new (GST_TYPE_VIDEO_FRAME);

  return frame;
}

static GObjectClass *gst_video_frame_parent_class;

static void
gst_video_frame_finalize (GstVideoFrame * frame)
{
  if (frame->sink_buffer)
    gst_buffer_unref (frame->sink_buffer);
  if (frame->src_buffer)
    gst_buffer_unref (frame->src_buffer);

  GST_MINI_OBJECT_CLASS (gst_video_frame_parent_class)->finalize
      (GST_MINI_OBJECT (frame));
}

static void
gst_video_frame_init (GstVideoFrame * frame, gpointer g_class)
{
  frame->upstream_timestamp = GST_CLOCK_TIME_NONE;
  frame->upstream_duration = GST_CLOCK_TIME_NONE;

  frame->parsed_timestamp = GST_CLOCK_TIME_NONE;

  frame->n_fields = 2;

  frame->sink_buffer = NULL;
  frame->src_buffer = NULL;
}

static void
gst_video_frame_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gst_video_frame_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_video_frame_finalize;
}


GType
gst_video_frame_get_type (void)
{
  static GType _gst_video_frame_type = 0;

  if (G_UNLIKELY (_gst_video_frame_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstVideoFrameClass),
      NULL,
      NULL,
      gst_video_frame_class_init,
      NULL,
      NULL,
      sizeof (GstVideoFrame),
      0,
      (GInstanceInitFunc) gst_video_frame_init,
      NULL
    };
    _gst_video_frame_type = g_type_register_static (GST_TYPE_MINI_OBJECT,
        "GstVideoFrame", &info, 0);

    DEBUG_INIT ();
  }
  return _gst_video_frame_type;
}
