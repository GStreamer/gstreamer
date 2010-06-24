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

#include "gstvdph264frame.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_h264_frame_debug);
#define GST_CAT_DEFAULT gst_vdp_h264_frame_debug

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_h264_frame_debug, "gstvdph264frame", 0, "Video Frame");

void
gst_vdp_h264_frame_add_slice (GstVdpH264Frame * h264_frame, GstBuffer * buf)
{
  gst_buffer_ref (buf);
  g_ptr_array_add (h264_frame->slices, buf);
}

GstVdpH264Frame *
gst_vdp_h264_frame_new (void)
{
  GstVdpH264Frame *frame;

  frame = (GstVdpH264Frame *) gst_mini_object_new (GST_TYPE_VDP_H264_FRAME);

  return frame;
}

static GObjectClass *gst_vdp_h264_frame_parent_class;

static void
gst_vdp_h264_frame_finalize (GstVdpH264Frame * h264_frame)
{
  g_ptr_array_unref (h264_frame->slices);

  GST_MINI_OBJECT_CLASS (gst_vdp_h264_frame_parent_class)->finalize
      (GST_MINI_OBJECT (h264_frame));
}

static void
gst_vdp_h264_frame_init (GstVdpH264Frame * h264_frame, gpointer g_class)
{
  h264_frame->slices = g_ptr_array_new_with_free_func (
      (GDestroyNotify) gst_buffer_unref);
}

static void
gst_vdp_h264_frame_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gst_vdp_h264_frame_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_vdp_h264_frame_finalize;
}


GType
gst_vdp_h264_frame_get_type (void)
{
  static GType _gst_vdp_h264_frame_type = 0;

  if (G_UNLIKELY (_gst_vdp_h264_frame_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstVdpH264FrameClass),
      NULL,
      NULL,
      gst_vdp_h264_frame_class_init,
      NULL,
      NULL,
      sizeof (GstVdpH264Frame),
      0,
      (GInstanceInitFunc) gst_vdp_h264_frame_init,
      NULL
    };
    _gst_vdp_h264_frame_type = g_type_register_static (SAT_TYPE_VIDEO_FRAME,
        "GstVdpH264Frame", &info, 0);

    DEBUG_INIT ();
  }
  return _gst_vdp_h264_frame_type;
}
