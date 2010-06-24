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

#include "satvideoframe.h"

GST_DEBUG_CATEGORY_STATIC (sat_video_frame_debug);
#define GST_CAT_DEFAULT sat_video_frame_debug

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (sat_video_frame_debug, "satvideoframe", 0, "Video Frame");

SatVideoFrame *
sat_video_frame_new (void)
{
  SatVideoFrame *frame;

  frame = (SatVideoFrame *) gst_mini_object_new (SAT_TYPE_VIDEO_FRAME);

  return frame;
}

static GObjectClass *sat_video_frame_parent_class;

static void
sat_video_frame_finalize (SatVideoFrame * frame)
{
  if (frame->sink_buffer)
    gst_buffer_unref (frame->sink_buffer);
  if (frame->src_buffer)
    gst_buffer_unref (frame->src_buffer);

  GST_MINI_OBJECT_CLASS (sat_video_frame_parent_class)->finalize
      (GST_MINI_OBJECT (frame));
}

static void
sat_video_frame_init (SatVideoFrame * frame, gpointer g_class)
{
  frame->decode_timestamp = GST_CLOCK_TIME_NONE;
  frame->presentation_timestamp = GST_CLOCK_TIME_NONE;
  frame->presentation_duration = GST_CLOCK_TIME_NONE;
  frame->n_fields = 2;

  frame->sink_buffer = NULL;
  frame->src_buffer = NULL;
}

static void
sat_video_frame_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  sat_video_frame_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      sat_video_frame_finalize;
}


GType
sat_video_frame_get_type (void)
{
  static GType _sat_video_frame_type = 0;

  if (G_UNLIKELY (_sat_video_frame_type == 0)) {
    static const GTypeInfo info = {
      sizeof (SatVideoFrameClass),
      NULL,
      NULL,
      sat_video_frame_class_init,
      NULL,
      NULL,
      sizeof (SatVideoFrame),
      0,
      (GInstanceInitFunc) sat_video_frame_init,
      NULL
    };
    _sat_video_frame_type = g_type_register_static (GST_TYPE_MINI_OBJECT,
        "SatVideoFrame", &info, 0);

    DEBUG_INIT ();
  }
  return _sat_video_frame_type;
}
