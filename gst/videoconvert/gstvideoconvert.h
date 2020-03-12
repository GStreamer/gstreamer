/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * This file:
 * Copyright (C) 2003 Ronald Bultje <rbultje@ronald.bitfreak.net>
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

#ifndef __GST_VIDEOCONVERT_H__
#define __GST_VIDEOCONVERT_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_VIDEO_CONVERT (gst_video_convert_get_type())
#define GST_VIDEO_CONVERT_CAST(obj) ((GstVideoConvert *)(obj))
G_DECLARE_FINAL_TYPE (GstVideoConvert, gst_video_convert, GST, VIDEO_CONVERT,
    GstVideoFilter)

/**
 * GstVideoConvert:
 *
 * Opaque object data structure.
 */
struct _GstVideoConvert {
  GstVideoFilter element;

  GstVideoConverter *convert;
  GstVideoDitherMethod dither;
  guint dither_quantization;
  GstVideoResamplerMethod chroma_resampler;
  GstVideoAlphaMode alpha_mode;
  GstVideoChromaMode chroma_mode;
  GstVideoMatrixMode matrix_mode;
  GstVideoGammaMode gamma_mode;
  GstVideoPrimariesMode primaries_mode;
  gdouble alpha_value;
  gint n_threads;
};

G_END_DECLS

#endif /* __GST_VIDEOCONVERT_H__ */
