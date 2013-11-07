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
#include "gst/vaapi/gstvaapicompat.h"

#include "gstvaapiencode_mpeg2.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideomemory.h"
#include "gst/vaapi/gstvaapiencoder_mpeg2.h"
#include "gst/vaapi/gstvaapiencoder_mpeg2_priv.h"
#include "gst/vaapi/gstvaapidisplay.h"
#include "gst/vaapi/gstvaapivalue.h"
#include "gst/vaapi/gstvaapisurface.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_mpeg2_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_mpeg2_encode_debug

#define GST_CAPS_CODEC(CODEC) CODEC "; "

static const char gst_vaapiencode_mpeg2_sink_caps_str[] =
#if GST_CHECK_VERSION(1,1,0)
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES(GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE,
      "{ ENCODED, NV12, I420, YV12 }") ", "
  GST_CAPS_INTERLACED_FALSE;
#else
  GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) "; "
  GST_VAAPI_SURFACE_CAPS;
#endif

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
  PROP_RATE_CONTROL,
  PROP_BITRATE,
  PROP_QUANTIZER,
  PROP_KEY_PERIOD,
  PROP_MAX_BFRAMES
};

static void
gst_vaapiencode_mpeg2_init (GstVaapiEncodeMpeg2 * mpeg2_encode)
{
  mpeg2_encode->rate_control = GST_VAAPI_ENCODER_MPEG2_DEFAULT_RATE_CONTROL;
  mpeg2_encode->bitrate = 0;
  mpeg2_encode->quantizer = GST_VAAPI_ENCODER_MPEG2_DEFAULT_CQP;
  mpeg2_encode->intra_period = GST_VAAPI_ENCODER_MPEG2_DEFAULT_GOP_SIZE;
  mpeg2_encode->ip_period = GST_VAAPI_ENCODER_MPEG2_DEFAULT_MAX_BFRAMES;
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
  GstVaapiEncodeMpeg2 *encode = GST_VAAPIENCODE_MPEG2 (object);

  switch (prop_id) {
    case PROP_RATE_CONTROL:
    {
      GstVaapiRateControl rate_control = g_value_get_enum (value);
      if (rate_control == GST_VAAPI_RATECONTROL_CBR ||
          rate_control == GST_VAAPI_RATECONTROL_CQP) {
        encode->rate_control = g_value_get_enum (value);
      } else {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
    }
    case PROP_BITRATE:
      encode->bitrate = g_value_get_uint (value);
      break;
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
  GstVaapiEncodeMpeg2 *encode = GST_VAAPIENCODE_MPEG2 (object);

  switch (prop_id) {
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, encode->rate_control);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, encode->bitrate);
      break;
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
  GstVaapiEncodeMpeg2 *encode = GST_VAAPIENCODE_MPEG2 (base);
  GstVaapiEncoder *ret;
  GstVaapiEncoderMpeg2 *mpeg2encoder;

  ret = gst_vaapi_encoder_mpeg2_new (display);
  mpeg2encoder = GST_VAAPI_ENCODER_MPEG2 (ret);

  mpeg2encoder->profile = GST_VAAPI_ENCODER_MPEG2_DEFAULT_PROFILE;
  mpeg2encoder->level = GST_VAAPI_ENCODER_MPEG2_DEFAULT_LEVEL;
  GST_VAAPI_ENCODER_RATE_CONTROL (mpeg2encoder) = encode->rate_control;
  mpeg2encoder->bitrate = encode->bitrate;
  mpeg2encoder->cqp = encode->quantizer;
  mpeg2encoder->intra_period = encode->intra_period;
  mpeg2encoder->ip_period = encode->ip_period;

  return ret;
}

static void
gst_vaapiencode_mpeg2_class_init (GstVaapiEncodeMpeg2Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_mpeg2_encode_debug,
      "vaapimpeg2encode", 0, "vaapimpeg2encode element");

  object_class->finalize = gst_vaapiencode_mpeg2_finalize;
  object_class->set_property = gst_vaapiencode_mpeg2_set_property;
  object_class->get_property = gst_vaapiencode_mpeg2_get_property;

  encode_class->create_encoder = gst_vaapiencode_mpeg2_create_encoder;

  gst_element_class_set_static_metadata (element_class,
      "VA-API mpeg2 encoder",
      "Codec/Encoder/Video",
      "A VA-API based video encoder", "Guangxin Xu <guangxin.xu@intel.com>");

  /* sink pad */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vaapiencode_mpeg2_sink_factory)
      );

  /* src pad */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vaapiencode_mpeg2_src_factory)
      );

  g_object_class_install_property (object_class,
      PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control",
          "Rate Control",
          "Rate control mode (CQP or CBR only)",
          GST_VAAPI_TYPE_RATE_CONTROL,
          GST_VAAPI_RATECONTROL_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_BITRATE,
      g_param_spec_uint ("bitrate",
          "Bitrate (kbps)",
          "The desired bitrate expressed in kbps (0: auto-calculate)",
          0, GST_VAAPI_ENCODER_MPEG2_MAX_BITRATE, 0, G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
      PROP_QUANTIZER,
      g_param_spec_uint ("quantizer",
          "Constant Quantizer",
          "Constant quantizer (if rate-control mode is CQP)",
          GST_VAAPI_ENCODER_MPEG2_MIN_CQP,
          GST_VAAPI_ENCODER_MPEG2_MAX_CQP,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_CQP, G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
      PROP_KEY_PERIOD,
      g_param_spec_uint ("key-period",
          "Key Period",
          "Maximal distance between two key-frames",
          1,
          GST_VAAPI_ENCODER_MPEG2_MAX_GOP_SIZE,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_GOP_SIZE, G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
      PROP_MAX_BFRAMES,
      g_param_spec_uint ("max-bframes",
          "Max B-Frames",
          "Number of B-frames between I and P",
          0,
          GST_VAAPI_ENCODER_MPEG2_MAX_MAX_BFRAMES,
          GST_VAAPI_ENCODER_MPEG2_DEFAULT_MAX_BFRAMES, G_PARAM_READWRITE));
}
