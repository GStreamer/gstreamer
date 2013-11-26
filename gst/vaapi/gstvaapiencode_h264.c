/*
 *  gstvaapiencode_h264.c - VA-API H.264 encoder
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

#include "gstvaapiencode_h264.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideomemory.h"
#include "gst/vaapi/gstvaapiencoder_h264.h"
#include "gst/vaapi/gstvaapiencoder_h264_priv.h"
#include "gst/vaapi/gstvaapidisplay.h"
#include "gst/vaapi/gstvaapivalue.h"
#include "gst/vaapi/gstvaapisurface.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_h264_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_h264_encode_debug

#define GST_CAPS_CODEC(CODEC) CODEC "; "

static const char gst_vaapiencode_h264_sink_caps_str[] =
#if GST_CHECK_VERSION(1,1,0)
  GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE,
      "{ ENCODED, NV12, I420, YV12 }") ", "
#else
  GST_VAAPI_SURFACE_CAPS ", "
#endif
  GST_CAPS_INTERLACED_FALSE "; "
  GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) ", "
  GST_CAPS_INTERLACED_FALSE;

static const char gst_vaapiencode_h264_src_caps_str[] =
  GST_CAPS_CODEC ("video/x-h264");

static GstStaticPadTemplate gst_vaapiencode_h264_sink_factory =
  GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vaapiencode_h264_sink_caps_str));

static GstStaticPadTemplate gst_vaapiencode_h264_src_factory =
  GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vaapiencode_h264_src_caps_str));

/* h264 encode */
G_DEFINE_TYPE (GstVaapiEncodeH264, gst_vaapiencode_h264, GST_TYPE_VAAPIENCODE)

enum
{
  H264_PROP_0,
  H264_PROP_RATE_CONTROL,
  H264_PROP_BITRATE,
  H264_PROP_KEY_PERIOD,
  H264_PROP_MAX_BFRAMES,
  H264_PROP_INIT_QP,
  H264_PROP_MIN_QP,
  H264_PROP_NUM_SLICES,
};

static void
gst_vaapiencode_h264_init (GstVaapiEncodeH264 * encode)
{
}

static void
gst_vaapiencode_h264_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_vaapiencode_h264_parent_class)->finalize (object);
}

static void
gst_vaapiencode_h264_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVaapiEncodeH264 *const encode = GST_VAAPIENCODE_H264 (object);

  switch (prop_id) {
    case H264_PROP_RATE_CONTROL:
      encode->rate_control = g_value_get_enum (value);
      break;
    case H264_PROP_BITRATE:
      encode->bitrate = g_value_get_uint (value);
      break;
    case H264_PROP_KEY_PERIOD:
      encode->intra_period = g_value_get_uint (value);
      break;
    case H264_PROP_INIT_QP:
      encode->init_qp = g_value_get_uint (value);
      break;
    case H264_PROP_MIN_QP:
      encode->min_qp = g_value_get_uint (value);
      break;
    case H264_PROP_NUM_SLICES:
      encode->num_slices = g_value_get_uint (value);
      break;
    case H264_PROP_MAX_BFRAMES:
      encode->max_bframes = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapiencode_h264_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVaapiEncodeH264 *const encode = GST_VAAPIENCODE_H264 (object);

  switch (prop_id) {
    case H264_PROP_RATE_CONTROL:
      g_value_set_enum (value, encode->rate_control);
      break;
    case H264_PROP_BITRATE:
      g_value_set_uint (value, encode->bitrate);
      break;
    case H264_PROP_KEY_PERIOD:
      g_value_set_uint (value, encode->intra_period);
      break;
    case H264_PROP_INIT_QP:
      g_value_set_uint (value, encode->init_qp);
      break;
    case H264_PROP_MIN_QP:
      g_value_set_uint (value, encode->min_qp);
      break;
    case H264_PROP_NUM_SLICES:
      g_value_set_uint (value, encode->num_slices);
      break;
    case H264_PROP_MAX_BFRAMES:
      g_value_set_uint (value, encode->max_bframes);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstVaapiEncoder *
gst_vaapiencode_h264_create_encoder (GstVaapiEncode * base,
    GstVaapiDisplay * display)
{
  GstVaapiEncodeH264 *const encode = GST_VAAPIENCODE_H264 (base);
  GstVaapiEncoder *ret;
  GstVaapiEncoderH264 *h264encoder;

  ret = gst_vaapi_encoder_h264_new (display);
  h264encoder = GST_VAAPI_ENCODER_H264 (ret);

  h264encoder->profile = GST_VAAPI_PROFILE_UNKNOWN;
  h264encoder->level = GST_VAAPI_ENCODER_H264_DEFAULT_LEVEL;
  GST_VAAPI_ENCODER_RATE_CONTROL (h264encoder) = encode->rate_control;
  h264encoder->bitrate = encode->bitrate;
  h264encoder->intra_period = encode->intra_period;
  h264encoder->init_qp = encode->init_qp;
  h264encoder->min_qp = encode->min_qp;
  h264encoder->slice_num = encode->num_slices;
  h264encoder->b_frame_num = encode->max_bframes;
  return ret;
}

/* h264 NAL byte stream operations */
static guint8 *
_h264_byte_stream_next_nal (guint8 * buffer, guint32 len, guint32 * nal_size)
{
  const guint8 *cur = buffer;
  const guint8 *end = buffer + len;
  guint8 *nal_start = NULL;
  guint32 flag = 0xFFFFFFFF;
  guint32 nal_start_len = 0;

  g_assert (len >= 0 && buffer && nal_size);
  if (len < 3) {
    *nal_size = len;
    nal_start = (len ? buffer : NULL);
    return nal_start;
  }

  /*locate head postion */
  if (!buffer[0] && !buffer[1]) {
    if (buffer[2] == 1) {       /* 0x000001 */
      nal_start_len = 3;
    } else if (!buffer[2] && len >= 4 && buffer[3] == 1) {      /* 0x00000001 */
      nal_start_len = 4;
    }
  }
  nal_start = buffer + nal_start_len;
  cur = nal_start;

  /*find next nal start position */
  while (cur < end) {
    flag = ((flag << 8) | ((*cur++) & 0xFF));
    if ((flag & 0x00FFFFFF) == 0x00000001) {
      if (flag == 0x00000001)
        *nal_size = cur - 4 - nal_start;
      else
        *nal_size = cur - 3 - nal_start;
      break;
    }
  }
  if (cur >= end) {
    *nal_size = end - nal_start;
    if (nal_start >= end) {
      nal_start = NULL;
    }
  }
  return nal_start;
}

static inline void
_start_code_to_size (guint8 nal_start_code[4], guint32 nal_size)
{
  nal_start_code[0] = ((nal_size >> 24) & 0xFF);
  nal_start_code[1] = ((nal_size >> 16) & 0xFF);
  nal_start_code[2] = ((nal_size >> 8) & 0xFF);
  nal_start_code[3] = (nal_size & 0xFF);
}

static gboolean
_h264_convert_byte_stream_to_avc (GstBuffer * buf)
{
  GstMapInfo info;
  guint32 nal_size;
  guint8 *nal_start_code, *nal_body;
  guint8 *frame_end;

  g_assert (buf);

  if (!gst_buffer_map (buf, &info, GST_MAP_READ | GST_MAP_WRITE))
    return FALSE;

  nal_start_code = info.data;
  frame_end = info.data + info.size;
  nal_size = 0;

  while ((frame_end > nal_start_code) &&
      (nal_body = _h264_byte_stream_next_nal (nal_start_code,
              frame_end - nal_start_code, &nal_size)) != NULL) {
    if (!nal_size)
      goto error;

    g_assert (nal_body - nal_start_code == 4);
    _start_code_to_size (nal_start_code, nal_size);
    nal_start_code = nal_body + nal_size;
  }
  gst_buffer_unmap (buf, &info);
  return TRUE;

error:
  gst_buffer_unmap (buf, &info);
  return FALSE;
}

static GstFlowReturn
gst_vaapiencode_h264_alloc_buffer (GstVaapiEncode * encode,
    GstVaapiCodedBuffer * coded_buf, GstBuffer ** out_buf)
{
  GstFlowReturn ret;
  GstVaapiEncoderH264 *h264encoder;

  g_return_val_if_fail (encode->encoder, GST_FLOW_ERROR);

  ret =
      GST_VAAPIENCODE_CLASS (gst_vaapiencode_h264_parent_class)->allocate_buffer
      (encode, coded_buf, out_buf);
  if (ret != GST_FLOW_OK)
    return ret;

  h264encoder = GST_VAAPI_ENCODER_H264 (encode->encoder);
  if (!gst_vaapi_encoder_h264_is_avc (h264encoder))
    return ret;

  /* convert to avc format */
  if (!_h264_convert_byte_stream_to_avc (*out_buf)) {
    GST_ERROR ("convert H.264 bytestream to avc buf failed.");
    gst_buffer_replace (out_buf, NULL);
  }
  return GST_FLOW_OK;
}

static void
gst_vaapiencode_h264_class_init (GstVaapiEncodeH264Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_h264_encode_debug,
      "vaapih264encode", 0, "vaapih264encode element");

  object_class->finalize = gst_vaapiencode_h264_finalize;
  object_class->set_property = gst_vaapiencode_h264_set_property;
  object_class->get_property = gst_vaapiencode_h264_get_property;

  encode_class->create_encoder = gst_vaapiencode_h264_create_encoder;
  encode_class->allocate_buffer = gst_vaapiencode_h264_alloc_buffer;

  gst_element_class_set_static_metadata (element_class,
      "VA-API h264 encoder",
      "Codec/Encoder/Video",
      "A VA-API based video encoder", "Wind Yuan <feng.yuan@intel.com>");

  /* sink pad */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vaapiencode_h264_sink_factory)
      );

  /* src pad */
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_vaapiencode_h264_src_factory)
      );

  g_object_class_install_property (object_class,
      H264_PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control",
          "Rate Control",
          "Rate control mode",
          GST_VAAPI_TYPE_RATE_CONTROL,
          GST_VAAPI_RATECONTROL_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      H264_PROP_BITRATE,
      g_param_spec_uint ("bitrate",
          "Bitrate (kbps)",
          "The desired bitrate expressed in kbps (0: auto-calculate)",
          0, 100 * 1024, 0, G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
      H264_PROP_KEY_PERIOD,
      g_param_spec_uint ("key-period",
          "Key Period",
          "Maximal distance between two key-frames",
          1,
          300, GST_VAAPI_ENCODER_H264_DEFAULT_INTRA_PERIOD, G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
      H264_PROP_MAX_BFRAMES,
      g_param_spec_uint ("max-bframes",
          "Max B-Frames",
          "Number of B-frames between I and P", 0, 10, 0, G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
      H264_PROP_INIT_QP,
      g_param_spec_uint ("init-qp",
          "Initial QP",
          "Initial quantizer value",
          1, 51, GST_VAAPI_ENCODER_H264_DEFAULT_INIT_QP, G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
      H264_PROP_MIN_QP,
      g_param_spec_uint ("min-qp",
          "Minimum QP",
          "Minimum quantizer value",
          1, 51, GST_VAAPI_ENCODER_H264_DEFAULT_MIN_QP, G_PARAM_READWRITE));

  g_object_class_install_property (object_class,
      H264_PROP_NUM_SLICES,
      g_param_spec_uint ("num-slices",
          "Number of Slices", "Number of slices per frame", 1, 200,
          1, G_PARAM_READWRITE));
}
