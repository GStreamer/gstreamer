/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) 2003 Arwed v. Merkatz <v.merkatz@gmx.net>
 * Copyright (C) 2006 Mark Nauwelaerts <manauw@skynet.be>
 * Copyright (C) 2010 Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

/*
 * This file was (probably) generated from
 * gstvideotemplate.c,v 1.12 2004/01/07 21:07:12 ds Exp 
 * and
 * make_filter,v 1.6 2004/01/07 21:33:01 ds Exp 
 */

/**
 * SECTION:element-gamma
 *
 * Performs gamma correction on a video stream.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch videotestsrc ! gamma gamma=2.0 ! ffmpegcolorspace ! ximagesink
 * ]| This pipeline will make the image "brighter".
 * |[
 * gst-launch videotestsrc ! gamma gamma=0.5 ! ffmpegcolorspace ! ximagesink
 * ]| This pipeline will make the image "darker".
 * </refsect2>
 *
 * Last reviewed on 2010-04-18 (0.10.22)
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgamma.h"
#include <string.h>
#include <math.h>

#include <gst/video/video.h>
#include <gst/controller/gstcontroller.h>

GST_DEBUG_CATEGORY_STATIC (gamma_debug);
#define GST_CAT_DEFAULT gamma_debug

/* GstGamma properties */
enum
{
  PROP_0,
  PROP_GAMMA
      /* FILL ME */
};

#define DEFAULT_PROP_GAMMA  1

static GstStaticPadTemplate gst_gamma_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV") ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ABGR ";" GST_VIDEO_CAPS_RGBA ";"
        GST_VIDEO_CAPS_YUV ("Y444") ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_xBGR ";" GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_YUV ("Y42B") ";"
        GST_VIDEO_CAPS_YUV ("NV12") ";"
        GST_VIDEO_CAPS_YUV ("NV21") ";"
        GST_VIDEO_CAPS_YUV ("YUY2") ";"
        GST_VIDEO_CAPS_YUV ("UYVY") ";"
        GST_VIDEO_CAPS_YUV ("YVYU") ";"
        GST_VIDEO_CAPS_YUV ("I420") ";"
        GST_VIDEO_CAPS_YUV ("YV12") ";"
        GST_VIDEO_CAPS_YUV ("IYUV") ";" GST_VIDEO_CAPS_YUV ("Y41B")
    )
    );

static GstStaticPadTemplate gst_gamma_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("AYUV") ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ABGR ";" GST_VIDEO_CAPS_RGBA ";"
        GST_VIDEO_CAPS_YUV ("Y444") ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_RGBx ";"
        GST_VIDEO_CAPS_xBGR ";" GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_YUV ("Y42B") ";"
        GST_VIDEO_CAPS_YUV ("NV12") ";"
        GST_VIDEO_CAPS_YUV ("NV21") ";"
        GST_VIDEO_CAPS_YUV ("YUY2") ";"
        GST_VIDEO_CAPS_YUV ("UYVY") ";"
        GST_VIDEO_CAPS_YUV ("YVYU") ";"
        GST_VIDEO_CAPS_YUV ("I420") ";"
        GST_VIDEO_CAPS_YUV ("YV12") ";"
        GST_VIDEO_CAPS_YUV ("IYUV") ";" GST_VIDEO_CAPS_YUV ("Y41B")
    )
    );

static void gst_gamma_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gamma_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gamma_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps);
static GstFlowReturn gst_gamma_transform_ip (GstBaseTransform * transform,
    GstBuffer * buf);
static void gst_gamma_before_transform (GstBaseTransform * transform,
    GstBuffer * buf);

static void gst_gamma_calculate_tables (GstGamma * gamma);

GST_BOILERPLATE (GstGamma, gst_gamma, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

static void
gst_gamma_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video gamma correction",
      "Filter/Effect/Video",
      "Adjusts gamma on a video stream",
      "Arwed v. Merkatz <v.merkatz@gmx.net>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_gamma_sink_template);
  gst_element_class_add_static_pad_template (element_class,
      &gst_gamma_src_template);
}

static void
gst_gamma_class_init (GstGammaClass * g_class)
{
  GObjectClass *gobject_class = (GObjectClass *) g_class;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) g_class;

  GST_DEBUG_CATEGORY_INIT (gamma_debug, "gamma", 0, "gamma");

  gobject_class->set_property = gst_gamma_set_property;
  gobject_class->get_property = gst_gamma_get_property;

  g_object_class_install_property (gobject_class, PROP_GAMMA,
      g_param_spec_double ("gamma", "Gamma", "gamma",
          0.01, 10, DEFAULT_PROP_GAMMA,
          GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_gamma_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_gamma_transform_ip);
  trans_class->before_transform =
      GST_DEBUG_FUNCPTR (gst_gamma_before_transform);
}

static void
gst_gamma_init (GstGamma * gamma, GstGammaClass * g_class)
{
  /* properties */
  gamma->gamma = DEFAULT_PROP_GAMMA;
  gst_gamma_calculate_tables (gamma);
}

static void
gst_gamma_set_property (GObject * object, guint prop_id, const GValue * value,
    GParamSpec * pspec)
{
  GstGamma *gamma = GST_GAMMA (object);

  switch (prop_id) {
    case PROP_GAMMA:{
      gdouble val = g_value_get_double (value);

      GST_DEBUG_OBJECT (gamma, "Changing gamma from %lf to %lf", gamma->gamma,
          val);
      GST_OBJECT_LOCK (gamma);
      gamma->gamma = val;
      gst_gamma_calculate_tables (gamma);
      GST_OBJECT_UNLOCK (gamma);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gamma_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstGamma *gamma = GST_GAMMA (object);

  switch (prop_id) {
    case PROP_GAMMA:
      g_value_set_double (value, gamma->gamma);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gamma_calculate_tables (GstGamma * gamma)
{
  gint n;
  gdouble val;
  gdouble exp;

  if (gamma->gamma == 1.0) {
    GST_BASE_TRANSFORM (gamma)->passthrough = TRUE;
    return;
  }
  GST_BASE_TRANSFORM (gamma)->passthrough = FALSE;

  exp = 1.0 / gamma->gamma;
  for (n = 0; n < 256; n++) {
    val = n / 255.0;
    val = pow (val, exp);
    val = 255.0 * val;
    gamma->gamma_table[n] = (guint8) floor (val + 0.5);
  }
}

static void
gst_gamma_planar_yuv_ip (GstGamma * gamma, guint8 * data)
{
  gint i, j, height;
  gint width, row_stride, row_wrap;
  const guint8 *table = gamma->gamma_table;

  data =
      data + gst_video_format_get_component_offset (gamma->format, 0,
      gamma->width, gamma->height);

  width = gst_video_format_get_component_width (gamma->format, 0, gamma->width);
  height = gst_video_format_get_component_height (gamma->format, 0,
      gamma->height);
  row_stride = gst_video_format_get_row_stride (gamma->format, 0, gamma->width);
  row_wrap = row_stride - width;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *data = table[*data];
      data++;
    }
    data += row_wrap;
  }
}

static void
gst_gamma_packed_yuv_ip (GstGamma * gamma, guint8 * data)
{
  gint i, j, height;
  gint width, row_stride, row_wrap;
  gint pixel_stride;
  const guint8 *table = gamma->gamma_table;

  data = data + gst_video_format_get_component_offset (gamma->format, 0,
      gamma->width, gamma->height);

  width = gst_video_format_get_component_width (gamma->format, 0, gamma->width);
  height = gst_video_format_get_component_height (gamma->format, 0,
      gamma->height);
  row_stride = gst_video_format_get_row_stride (gamma->format, 0, gamma->width);
  pixel_stride = gst_video_format_get_pixel_stride (gamma->format, 0);
  row_wrap = row_stride - pixel_stride * width;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *data = table[*data];
      data += pixel_stride;
    }
    data += row_wrap;
  }
}

static const int cog_ycbcr_to_rgb_matrix_8bit_sdtv[] = {
  298, 0, 409, -57068,
  298, -100, -208, 34707,
  298, 516, 0, -70870,
};

static const gint cog_rgb_to_ycbcr_matrix_8bit_sdtv[] = {
  66, 129, 25, 4096,
  -38, -74, 112, 32768,
  112, -94, -18, 32768,
};

#define APPLY_MATRIX(m,o,v1,v2,v3) ((m[o*4] * v1 + m[o*4+1] * v2 + m[o*4+2] * v3 + m[o*4+3]) >> 8)

static void
gst_gamma_packed_rgb_ip (GstGamma * gamma, guint8 * data)
{
  gint i, j, height;
  gint width, row_stride, row_wrap;
  gint pixel_stride;
  const guint8 *table = gamma->gamma_table;
  gint offsets[3];
  gint r, g, b;
  gint y, u, v;

  offsets[0] = gst_video_format_get_component_offset (gamma->format, 0,
      gamma->width, gamma->height);
  offsets[1] = gst_video_format_get_component_offset (gamma->format, 1,
      gamma->width, gamma->height);
  offsets[2] = gst_video_format_get_component_offset (gamma->format, 2,
      gamma->width, gamma->height);

  width = gst_video_format_get_component_width (gamma->format, 0, gamma->width);
  height = gst_video_format_get_component_height (gamma->format, 0,
      gamma->height);
  row_stride = gst_video_format_get_row_stride (gamma->format, 0, gamma->width);
  pixel_stride = gst_video_format_get_pixel_stride (gamma->format, 0);
  row_wrap = row_stride - pixel_stride * width;

  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      r = data[offsets[0]];
      g = data[offsets[1]];
      b = data[offsets[2]];

      y = APPLY_MATRIX (cog_rgb_to_ycbcr_matrix_8bit_sdtv, 0, r, g, b);
      u = APPLY_MATRIX (cog_rgb_to_ycbcr_matrix_8bit_sdtv, 1, r, g, b);
      v = APPLY_MATRIX (cog_rgb_to_ycbcr_matrix_8bit_sdtv, 2, r, g, b);

      y = table[CLAMP (y, 0, 255)];
      r = APPLY_MATRIX (cog_ycbcr_to_rgb_matrix_8bit_sdtv, 0, y, u, v);
      g = APPLY_MATRIX (cog_ycbcr_to_rgb_matrix_8bit_sdtv, 1, y, u, v);
      b = APPLY_MATRIX (cog_ycbcr_to_rgb_matrix_8bit_sdtv, 2, y, u, v);

      data[offsets[0]] = CLAMP (r, 0, 255);
      data[offsets[1]] = CLAMP (g, 0, 255);
      data[offsets[2]] = CLAMP (b, 0, 255);
      data += pixel_stride;
    }
    data += row_wrap;
  }
}

static gboolean
gst_gamma_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGamma *gamma = GST_GAMMA (base);

  GST_DEBUG_OBJECT (gamma,
      "setting caps: in %" GST_PTR_FORMAT " out %" GST_PTR_FORMAT, incaps,
      outcaps);

  if (!gst_video_format_parse_caps (incaps, &gamma->format, &gamma->width,
          &gamma->height))
    goto invalid_caps;

  gamma->size =
      gst_video_format_get_size (gamma->format, gamma->width, gamma->height);

  switch (gamma->format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y41B:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
      gamma->process = gst_gamma_planar_yuv_ip;
      break;
    case GST_VIDEO_FORMAT_YUY2:
    case GST_VIDEO_FORMAT_UYVY:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_YVYU:
      gamma->process = gst_gamma_packed_yuv_ip;
      break;
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
      gamma->process = gst_gamma_packed_rgb_ip;
      break;
    default:
      goto invalid_caps;
      break;
  }

  return TRUE;

invalid_caps:
  GST_ERROR_OBJECT (gamma, "Invalid caps: %" GST_PTR_FORMAT, incaps);
  return FALSE;
}

static void
gst_gamma_before_transform (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstGamma *gamma = GST_GAMMA (base);
  GstClockTime timestamp, stream_time;

  timestamp = GST_BUFFER_TIMESTAMP (outbuf);
  stream_time =
      gst_segment_to_stream_time (&base->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (gamma, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (G_OBJECT (gamma), stream_time);
}

static GstFlowReturn
gst_gamma_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstGamma *gamma = GST_GAMMA (base);
  guint8 *data;
  guint size;

  if (!gamma->process)
    goto not_negotiated;

  if (base->passthrough)
    goto done;

  data = GST_BUFFER_DATA (outbuf);
  size = GST_BUFFER_SIZE (outbuf);

  if (size != gamma->size)
    goto wrong_size;

  GST_OBJECT_LOCK (gamma);
  gamma->process (gamma, data);
  GST_OBJECT_UNLOCK (gamma);

done:
  return GST_FLOW_OK;

  /* ERRORS */
wrong_size:
  {
    GST_ELEMENT_ERROR (gamma, STREAM, FORMAT,
        (NULL), ("Invalid buffer size %d, expected %d", size, gamma->size));
    return GST_FLOW_ERROR;
  }
not_negotiated:
  {
    GST_ERROR_OBJECT (gamma, "Not negotiated yet");
    return GST_FLOW_NOT_NEGOTIATED;
  }
}
