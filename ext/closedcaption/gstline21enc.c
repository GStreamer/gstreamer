/*
 * GStreamer
 * Copyright (C) 2019 Mathieu Duponchelle <mathieu@centricular.com>
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

/**
 * SECTION:element-line21encoder
 * @title: line21encoder
 *
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>
#include <gst/video/video.h>
#include <string.h>

#include "gstline21enc.h"
#include "io-sim.h"

GST_DEBUG_CATEGORY_STATIC (gst_line_21_encoder_debug);
#define GST_CAT_DEFAULT gst_line_21_encoder_debug

#define CAPS "video/x-raw, format={ I420, YUY2, YVYU, UYVY, VYUY }, width=(int)720, height=(int)[ 23, MAX ], interlace-mode=interleaved"

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS));

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (CAPS));

G_DEFINE_TYPE (GstLine21Encoder, gst_line_21_encoder, GST_TYPE_VIDEO_FILTER);
#define parent_class gst_line_21_encoder_parent_class

static gboolean gst_line_21_encoder_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info);
static GstFlowReturn gst_line_21_encoder_transform_ip (GstVideoFilter * filter,
    GstVideoFrame * frame);

static void
gst_line_21_encoder_class_init (GstLine21EncoderClass * klass)
{
  GstElementClass *gstelement_class;
  GstVideoFilterClass *filter_class;

  gstelement_class = (GstElementClass *) klass;
  filter_class = (GstVideoFilterClass *) klass;

  gst_element_class_set_static_metadata (gstelement_class,
      "Line 21 CC Encoder",
      "Filter/Video/ClosedCaption",
      "Inject line21 CC in SD video streams",
      "Mathieu Duponchelle <mathieu@centricular.com>");

  gst_element_class_add_static_pad_template (gstelement_class, &sinktemplate);
  gst_element_class_add_static_pad_template (gstelement_class, &srctemplate);

  filter_class->set_info = gst_line_21_encoder_set_info;
  filter_class->transform_frame_ip = gst_line_21_encoder_transform_ip;

  GST_DEBUG_CATEGORY_INIT (gst_line_21_encoder_debug, "line21encoder",
      0, "Line 21 CC Encoder");
  vbi_initialize_gst_debug ();
}

static void
gst_line_21_encoder_init (GstLine21Encoder * filter)
{
}

static vbi_pixfmt
vbi_pixfmt_from_gst_video_format (GstVideoFormat format)
{
  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      return VBI_PIXFMT_YUV420;
    case GST_VIDEO_FORMAT_YUY2:
      return VBI_PIXFMT_YUYV;
    case GST_VIDEO_FORMAT_YVYU:
      return VBI_PIXFMT_YVYU;
    case GST_VIDEO_FORMAT_UYVY:
      return VBI_PIXFMT_UYVY;
    case GST_VIDEO_FORMAT_VYUY:
      return VBI_PIXFMT_VYUY;
    default:
      g_assert_not_reached ();
      return (vbi_pixfmt) 0;
  }
#undef NATIVE_VBI_FMT
}

static gboolean
gst_line_21_encoder_set_info (GstVideoFilter * filter,
    GstCaps * incaps, GstVideoInfo * in_info,
    GstCaps * outcaps, GstVideoInfo * out_info)
{
  GstLine21Encoder *self = GST_LINE21ENCODER (filter);

  self->info = *in_info;

  /*
   * Set up blank / black / white levels fit for NTSC, no actual relation
   * with the height of the video
   */
  self->sp.scanning = 525;
  /* The pixel format */
  self->sp.sampling_format =
      vbi_pixfmt_from_gst_video_format (GST_VIDEO_INFO_FORMAT (&self->info));
  /* Sampling rate. For BT.601 it's 13.5MHz */
  self->sp.sampling_rate = 13.5e6;
  /* Stride */
  self->sp.bytes_per_line = GST_VIDEO_INFO_COMP_STRIDE (&self->info, 0);
  /* Horizontal offset of the VBI image */
  self->sp.offset = 122;

  /* FIXME: magic numbers */
  self->sp.start[0] = 21;
  self->sp.count[0] = 1;
  self->sp.start[1] = 284;
  self->sp.count[1] = 1;

  self->sp.interlaced = FALSE;
  self->sp.synchronous = TRUE;

  return TRUE;
}

static GstFlowReturn
gst_line_21_encoder_transform_ip (GstVideoFilter * filter,
    GstVideoFrame * frame)
{
  GstLine21Encoder *self = GST_LINE21ENCODER (filter);
  GstVideoCaptionMeta *cc_meta;
  guint8 *buf;
  vbi_sliced sliced[2];
  gpointer iter = NULL;
  GstFlowReturn ret = GST_FLOW_ERROR;

  sliced[0].id = VBI_SLICED_CAPTION_525_F1;
  sliced[0].line = self->sp.start[0];
  sliced[1].id = VBI_SLICED_CAPTION_525_F2;
  sliced[1].line = self->sp.start[1];

  sliced[0].data[0] = 0x80;
  sliced[0].data[1] = 0x80;
  sliced[1].data[0] = 0x80;
  sliced[1].data[1] = 0x80;

  /* We loop over caption metas until we find the first CEA608 meta */
  while ((cc_meta = (GstVideoCaptionMeta *)
          gst_buffer_iterate_meta_filtered (frame->buffer, &iter,
              GST_VIDEO_CAPTION_META_API_TYPE))) {
    guint n = cc_meta->size;
    guint i;

    if (cc_meta->caption_type != GST_VIDEO_CAPTION_TYPE_CEA608_S334_1A)
      continue;

    if (n % 3 != 0) {
      GST_ERROR_OBJECT (filter, "Invalid S334-1A CEA608 buffer size");
      goto done;
    }

    n /= 3;

    if (n >= 3) {
      GST_ERROR_OBJECT (filter, "Too many S334-1A CEA608 triplets %u", n);
      goto done;
    }

    for (i = 0; i < n; i++) {
      if (cc_meta->data[i * 3] & 0x80) {
        sliced[0].data[0] = cc_meta->data[i * 3 + 1];
        sliced[0].data[1] = cc_meta->data[i * 3 + 2];
      } else {
        sliced[1].data[0] = cc_meta->data[i * 3 + 1];
        sliced[1].data[1] = cc_meta->data[i * 3 + 2];
      }
    }

    break;
  }

  /* We've encoded this meta, it can now be removed */
  if (cc_meta)
    gst_buffer_remove_meta (frame->buffer, (GstMeta *) cc_meta);

  buf =
      (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (frame,
      0) + 21 * GST_VIDEO_INFO_COMP_STRIDE (&self->info, 0);

  if (!vbi_raw_video_image (buf, GST_VIDEO_INFO_COMP_STRIDE (&self->info,
              0) * 2, &self->sp, 0, 0, 0, 0x000000FF, 0, sliced, 2)) {
    GST_ERROR_OBJECT (filter, "Failed to encode CC data");
    goto done;
  }

  ret = GST_FLOW_OK;

done:
  return ret;
}
