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

#include "gstvdpmpegframe.h"

GST_DEBUG_CATEGORY_STATIC (gst_vdp_mpeg_frame_debug);
#define GST_CAT_DEFAULT gst_vdp_mpeg_frame_debug

#define DEBUG_INIT(bla) \
GST_DEBUG_CATEGORY_INIT (gst_vdp_mpeg_frame_debug, "gstvdpmpegframe", 0, "Video Frame");

void
gst_vdp_mpeg_frame_add_slice (GstVdpMpegFrame * mpeg_frame, GstBuffer * buf)
{
  if (!mpeg_frame->slices)
    mpeg_frame->slices = buf;
  else
    mpeg_frame->slices = gst_buffer_join (mpeg_frame->slices, buf);
  mpeg_frame->n_slices++;
}

GstVdpMpegFrame *
gst_vdp_mpeg_frame_new (void)
{
  GstVdpMpegFrame *frame;

  frame =
      GST_VDP_MPEG_FRAME_CAST (gst_mini_object_new (GST_TYPE_VDP_MPEG_FRAME));

  return frame;
}

static GObjectClass *gst_vdp_mpeg_frame_parent_class;

static void
gst_vdp_mpeg_frame_finalize (GstVdpMpegFrame * mpeg_frame)
{
  if (mpeg_frame->seq)
    gst_buffer_unref (mpeg_frame->seq);
  if (mpeg_frame->seq_ext)
    gst_buffer_unref (mpeg_frame->seq_ext);

  if (mpeg_frame->pic)
    gst_buffer_unref (mpeg_frame->pic);
  if (mpeg_frame->pic_ext)
    gst_buffer_unref (mpeg_frame->pic_ext);

  if (mpeg_frame->gop)
    gst_buffer_unref (mpeg_frame->gop);
  if (mpeg_frame->qm_ext)
    gst_buffer_unref (mpeg_frame->qm_ext);

  if (mpeg_frame->slices)
    gst_buffer_unref (mpeg_frame->slices);


  GST_MINI_OBJECT_CLASS (gst_vdp_mpeg_frame_parent_class)->finalize
      (GST_MINI_OBJECT (mpeg_frame));
}

static void
gst_vdp_mpeg_frame_init (GstVdpMpegFrame * mpeg_frame, gpointer g_class)
{
  mpeg_frame->seq = NULL;
  mpeg_frame->seq_ext = NULL;

  mpeg_frame->pic = NULL;
  mpeg_frame->pic_ext = NULL;

  mpeg_frame->gop = NULL;
  mpeg_frame->qm_ext = NULL;

  mpeg_frame->n_slices = 0;
  mpeg_frame->slices = NULL;
}

static void
gst_vdp_mpeg_frame_class_init (gpointer g_class, gpointer class_data)
{
  GstMiniObjectClass *mini_object_class = GST_MINI_OBJECT_CLASS (g_class);

  gst_vdp_mpeg_frame_parent_class = g_type_class_peek_parent (g_class);

  mini_object_class->finalize = (GstMiniObjectFinalizeFunction)
      gst_vdp_mpeg_frame_finalize;
}


GType
gst_vdp_mpeg_frame_get_type (void)
{
  static GType _gst_vdp_mpeg_frame_type = 0;

  if (G_UNLIKELY (_gst_vdp_mpeg_frame_type == 0)) {
    static const GTypeInfo info = {
      sizeof (GstVdpMpegFrameClass),
      NULL,
      NULL,
      gst_vdp_mpeg_frame_class_init,
      NULL,
      NULL,
      sizeof (GstVdpMpegFrame),
      0,
      (GInstanceInitFunc) gst_vdp_mpeg_frame_init,
      NULL
    };
    _gst_vdp_mpeg_frame_type = g_type_register_static (GST_TYPE_VIDEO_FRAME,
        "GstVdpMpegFrame", &info, 0);

    DEBUG_INIT ();
  }
  return _gst_vdp_mpeg_frame_type;
}
