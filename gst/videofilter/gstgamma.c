/* GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) <2003> David Schleef <ds@schleef.org>
 * Copyright (C) 2003 Arwed v. Merkatz <v.merkatz@gmx.net>
 * Copyright (C) 2006 Mark Nauwelaerts <manauw@skynet.be>
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
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgamma.h"
#include <liboil/liboil.h>
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
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ IYUV, I420, YV12 }"))
    );

static GstStaticPadTemplate gst_gamma_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_YUV ("{ IYUV, I420, YV12 }"))
    );

static void gst_gamma_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gamma_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gamma_set_caps (GstBaseTransform * base, GstCaps * incaps,
    GstCaps * outcaps);
static GstFlowReturn gst_gamma_transform_ip (GstBaseTransform * transform,
    GstBuffer * buf);

static void gst_gamma_calculate_tables (GstGamma * gamma);

GST_BOILERPLATE (GstGamma, gst_gamma, GstVideoFilter, GST_TYPE_VIDEO_FILTER);

static void
gst_gamma_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details_simple (element_class, "Video gamma correction",
      "Filter/Effect/Video",
      "Adjusts gamma on a video stream", "Arwed v. Merkatz <v.merkatz@gmx.net");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gamma_sink_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_gamma_src_template));
}

static void
gst_gamma_class_init (GstGammaClass * g_class)
{
  GObjectClass *gobject_class = (GObjectClass *) g_class;
  GstBaseTransformClass *trans_class = (GstBaseTransformClass *) g_class;

  gobject_class->set_property = gst_gamma_set_property;
  gobject_class->get_property = gst_gamma_get_property;

  g_object_class_install_property (gobject_class, PROP_GAMMA,
      g_param_spec_double ("gamma", "Gamma", "gamma",
          0.01, 10, DEFAULT_PROP_GAMMA,
          GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS | G_PARAM_READWRITE));

  trans_class->set_caps = GST_DEBUG_FUNCPTR (gst_gamma_set_caps);
  trans_class->transform_ip = GST_DEBUG_FUNCPTR (gst_gamma_transform_ip);
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
gst_gamma_planar_ip (GstGamma * gamma, guint8 * data)
{
  gint size;

  data =
      data + gst_video_format_get_component_offset (gamma->format, 0,
      gamma->width, gamma->height);
  size =
      gst_video_format_get_row_stride (gamma->format, 0,
      gamma->width) * gst_video_format_get_component_height (gamma->format, 0,
      gamma->height);

  oil_tablelookup_u8 (data, 1, data, 1, gamma->gamma_table, 1, size);
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
      gamma->process = gst_gamma_planar_ip;
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

static GstFlowReturn
gst_gamma_transform_ip (GstBaseTransform * base, GstBuffer * outbuf)
{
  GstGamma *gamma = GST_GAMMA (base);
  guint8 *data;
  guint size;
  GstClockTime timestamp, stream_time;

  if (!gamma->process)
    goto not_negotiated;

  timestamp = GST_BUFFER_TIMESTAMP (outbuf);
  stream_time =
      gst_segment_to_stream_time (&base->segment, GST_FORMAT_TIME, timestamp);

  GST_DEBUG_OBJECT (gamma, "sync to %" GST_TIME_FORMAT,
      GST_TIME_ARGS (timestamp));

  if (GST_CLOCK_TIME_IS_VALID (stream_time))
    gst_object_sync_values (G_OBJECT (gamma), stream_time);

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

static gboolean
plugin_init (GstPlugin * plugin)
{
  GST_DEBUG_CATEGORY_INIT (gamma_debug, "gamma", 0, "gamma");

  oil_init ();
  gst_controller_init (NULL, NULL);

  return gst_element_register (plugin, "gamma", GST_RANK_NONE, GST_TYPE_GAMMA);
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    "gamma",
    "Changes gamma on video images",
    plugin_init, VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
