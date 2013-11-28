/*
 *  gstvaapiencode_mpeg2.c - VA-API MPEG2 encoder
 *
 *  Copyright (C) 2012-2013 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiencoder_mpeg2.h>
#include "gst/vaapi/gstvaapiencoder_mpeg2_priv.h"
#include "gstvaapiencode_mpeg2.h"
#include "gstvaapipluginutil.h"
#if GST_CHECK_VERSION(1,0,0)
#include "gstvaapivideomemory.h"
#endif

#define GST_PLUGIN_NAME "vaapiencode_mpeg2"
#define GST_PLUGIN_DESC "A VA-API based MPEG-2 video encoder"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_mpeg2_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_mpeg2_encode_debug

#define GST_CAPS_CODEC(CODEC) CODEC "; "

static const char gst_vaapiencode_mpeg2_sink_caps_str[] =
#if GST_CHECK_VERSION(1,1,0)
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE,
      "{ ENCODED, NV12, I420, YV12 }") ", "
#else
  GST_VAAPI_SURFACE_CAPS ", "
#endif
  GST_CAPS_INTERLACED_FALSE "; "
#if GST_CHECK_VERSION(1,0,0)
  GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) ", "
#else
  "video/x-raw-yuv, "
  "width  = (int) [ 1, MAX ], "
  "height = (int) [ 1, MAX ], "
#endif
  GST_CAPS_INTERLACED_FALSE;

static const char gst_vaapiencode_mpeg2_src_caps_str[] =
  GST_CAPS_CODEC ("video/mpeg,"
    "mpegversion = (int) 2, " "systemstream = (boolean) false");

static GstStaticPadTemplate gst_vaapiencode_mpeg2_sink_factory =
  GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vaapiencode_mpeg2_sink_caps_str));

static GstStaticPadTemplate gst_vaapiencode_mpeg2_src_factory =
  GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vaapiencode_mpeg2_src_caps_str));

/* mpeg2 encode */
G_DEFINE_TYPE (GstVaapiEncodeMpeg2, gst_vaapiencode_mpeg2, GST_TYPE_VAAPIENCODE)

enum
{
  PROP_0,
  PROP_QUANTIZER,
  PROP_KEY_PERIOD,
  PROP_MAX_BFRAMES
};

static void
gst_vaapiencode_mpeg2_init (GstVaapiEncodeMpeg2 * encode)
{
  GstVaapiEncode *const base_encode = GST_VAAPIENCODE_CAST (encode);

  base_encode->rate_control = GST_VAAPI_ENCODER_MPEG2_DEFAULT_RATE_CONTROL;
  encode->quantizer = GST_VAAPI_ENCODER_MPEG2_DEFAULT_CQP;
  encode->intra_period = GST_VAAPI_ENCODER_MPEG2_DEFAULT_GOP_SIZE;
  encode->ip_period = GST_VAAPI_ENCODER_MPEG2_DEFAULT_MAX_BFRAMES;
}

static void
gst_vaapiencode_mpeg2_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_vaapiencode_mpeg2_parent_class)->finalize (object);
}

static void
gst_vaapiencode_mpeg2_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVaapiEncodeMpeg2 *const encode = GST_VAAPIENCODE_MPEG2_CAST (object);

  switch (prop_id) {
    case PROP_QUANTIZER:
      encode->quantizer = g_value_get_uint (value);
      break;
    case PROP_KEY_PERIOD:
      encode->intra_period = g_value_get_uint (value);
      break;
    case PROP_MAX_BFRAMES:
      encode->ip_period = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapiencode_mpeg2_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVaapiEncodeMpeg2 *const encode = GST_VAAPIENCODE_MPEG2_CAST (object);

  switch (prop_id) {
    case PROP_QUANTIZER:
      g_value_set_uint (value, encode->quantizer);
      break;
    case PROP_KEY_PERIOD:
      g_value_set_uint (value, encode->intra_period);
      break;
    case PROP_MAX_BFRAMES:
      g_value_set_uint (value, encode->ip_period);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstVaapiEncoder *
gst_vaapiencode_mpeg2_create_encoder (GstVaapiEncode * base,
    GstVaapiDisplay * display)
{
  GstVaapiEncodeMpeg2 *const encode = GST_VAAPIENCODE_MPEG2_CAST (base);
  GstVaapiEncode *const base_encode = GST_VAAPIENCODE_CAST (base);
  GstVaapiEncoder *base_encoder;
  GstVaapiEncoderMpeg2 *encoder;

  base_encoder = gst_vaapi_encoder_mpeg2_new (display);
  if (!base_encoder)
    return NULL;
  encoder = GST_VAAPI_ENCODER_MPEG2 (base_encoder);

  encoder->profile = GST_VAAPI_ENCODER_MPEG2_DEFAULT_PROFILE;
  encoder->level = GST_VAAPI_ENCODER_MPEG2_DEFAULT_LEVEL;
  GST_VAAPI_ENCODER_RATE_CONTROL (encoder) = base_encode->rate_control;
  encoder->bitrate = base_encode->bitrate;
  encoder->cqp = encode->quantizer;
  encoder->intra_period = encode->intra_period;
  encoder->ip_period = encode->ip_period;
  return base_encoder;
}

static gboolean
gst_vaapiencode_mpeg2_check_ratecontrol (GstVaapiEncode * encode,
    GstVaapiRateControl rate_control)
{
  /* XXX: get information from GstVaapiEncoder object */
  return rate_control == GST_VAAPI_RATECONTROL_CQP ||
      rate_control == GST_VAAPI_RATECONTROL_CBR;
}

static void
gst_vaapiencode_mpeg2_class_init (GstVaapiEncodeMpeg2Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_mpeg2_encode_debug,
      GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

  object_class->finalize = gst_vaapiencode_mpeg2_finalize;
  object_class->set_property = gst_vaapiencode_mpeg2_set_property;
  object_class->get_property = gst_vaapiencode_mpeg2_get_property;

  encode_class->create_encoder = gst_vaapiencode_mpeg2_create_encoder;
  encode_class->check_ratecontrol = gst_vaapiencode_mpeg2_check_ratecontrol;

  gst_element_class_set_static_metadata (element_class,
      "VA-API MPEG-2 encoder",
      "Codec/Encoder/Video",
      GST_PLUGIN_DESC, "Guangxin Xu <guangxin.xu@intel.com>");

  /* sink pad */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vaapiencode_mpeg2_sink_factory)
      );

  /* src pad */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vaapiencode_mpeg2_src_factory)
      );

  g_object_class_install_property (object_class,
      PROP_QUANTIZER,
      g_param_spec_uint ("quantizer",
          "Constant Quantizer",
          "Constant quantizer (if rate-control mode is CQP)",
          GST_VAAPI_ENCODER_MPEG2_MIN_CQP,
          GST_VAAPI_ENCODER_MPEG2_MAX_CQP,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_CQP,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_KEY_PERIOD,
      g_param_spec_uint ("key-period",
          "Key Period",
          "Maximal distance between two key-frames",
          1,
          GST_VAAPI_ENCODER_MPEG2_MAX_GOP_SIZE,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_GOP_SIZE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_MAX_BFRAMES,
      g_param_spec_uint ("max-bframes",
          "Max B-Frames",
          "Number of B-frames between I and P",
          0,
          GST_VAAPI_ENCODER_MPEG2_MAX_MAX_BFRAMES,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_MAX_BFRAMES,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
