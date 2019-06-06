/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2007> Wim Taymans <wim.taymans@collabora.co.uk>
 * Copyright (C) <2007> Edward Hervey <edward.hervey@collabora.co.uk>
 * Copyright (C) <2007> Jan Schmidt <thaytan@noraisin.net>
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

#ifndef __GST_ALPHA_H__
#define __GST_ALPHA_H__

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/gstvideofilter.h>

G_BEGIN_DECLS

#define GST_TYPE_ALPHA (gst_alpha_get_type ())

G_DECLARE_FINAL_TYPE (GstAlpha, gst_alpha, GST, ALPHA, GstVideoFilter)

/**
 * GstAlphaMethod:
 * @ALPHA_METHOD_SET: Set/adjust alpha channel
 * @ALPHA_METHOD_GREEN: Chroma Key green
 * @ALPHA_METHOD_BLUE: Chroma Key blue
 * @ALPHA_METHOD_CUSTOM: Chroma Key on target_r/g/b
 */
typedef enum
{
  ALPHA_METHOD_SET,
  ALPHA_METHOD_GREEN,
  ALPHA_METHOD_BLUE,
  ALPHA_METHOD_CUSTOM,
}
GstAlphaMethod;

GST_DEBUG_CATEGORY_STATIC (gst_alpha_debug);
#define GST_CAT_DEFAULT gst_alpha_debug

struct _GstAlpha
{
  GstVideoFilter parent;

  /* <private> */

  /* caps */
  GMutex lock;

  gboolean in_sdtv, out_sdtv;

  /* properties */
  gdouble alpha;

  guint target_r;
  guint target_g;
  guint target_b;

  GstAlphaMethod method;

  gfloat angle;
  gfloat noise_level;
  guint black_sensitivity;
  guint white_sensitivity;

  gboolean prefer_passthrough;

  /* processing function */
  void (*process) (const GstVideoFrame *in_frame, GstVideoFrame *out_frame, GstAlpha *alpha);

  /* precalculated values for chroma keying */
  gint8 cb, cr;
  gint8 kg;
  guint8 accept_angle_tg;
  guint8 accept_angle_ctg;
  guint8 one_over_kc;
  guint8 kfgy_scale;
  guint noise_level2;
};

G_END_DECLS

#endif /* __GST_ALPHA_H__ */
