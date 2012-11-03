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
* Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
* Boston, MA 02110-1301, USA.
*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstmpeg4frame.h"

GST_DEBUG_CATEGORY_STATIC (gst_mpeg4_frame_debug);
#define GST_CAT_DEFAULT gst_mpeg4_frame_debug

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_mpeg4_frame_debug, "gstmpeg4frame", 0, "Mpeg4 Frame");

GstMpeg4Frame *
gst_mpeg4_frame_new (void)
{
  GstMpeg4Frame *frame;

  frame = (GstMpeg4Frame *) gst_mini_object_new (GST_TYPE_MPEG4_FRAME);

  return frame;
}

static GObjectClass *gst_mpeg4_frame_parent_class;

static void
gst_mpeg4_frame_finalize (GstMpeg4Frame * mpeg4_frame)
{
  if (mpeg4_frame->vos_buf)
    gst_buffer_unref (mpeg4_frame->vos_buf);

  if (mpeg4_frame->vo_buf)
    gst_buffer_unref (mpeg4_frame->vo_buf);

  if (mpeg4_frame->vol_buf)
    gst_buffer_unref (mpeg4_frame->vol_buf);

  if (mpeg4_frame->gov_buf)
    gst_buffer_unref (mpeg4_frame->gov_buf);

  if (mpeg4_frame->vop_buf)
    gst_buffer_unref (mpeg4_frame->vop_buf);


  GST_MINI_OBJECT_CLASS (gst_mpeg4_frame_parent_class)->finalize
      (GST_MINI_OBJECT (mpeg4_frame));
}

static void
gst_mpeg4_frame_init (GstMpeg4Frame * mpeg4_frame, gpointer g_class)
{
}

static void
gst_mpeg4_frame_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gst_mpeg4_frame_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_mpeg4_frame_finalize;
}


GType
gst_mpeg4_frame_get_type (void)
{
  static GType _gst_mpeg4_frame_type = 0;

  if (G_UNLIKELY (_gst_mpeg4_frame_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstMpeg4FrameClass),
      NULL,
      NULL,
      gst_mpeg4_frame_class_init,
      NULL,
      NULL,
      sizeof (GstMpeg4Frame),
      0,
      (GInstanceInitFunc) gst_mpeg4_frame_init,
      NULL
    };
    _gst_mpeg4_frame_type = g_type_register_static (GST_TYPE_VIDEO_FRAME,
        "GstMpeg4Frame", &info, 0);

    DEBUG_INIT ();
  }
  return _gst_mpeg4_frame_type;
}
