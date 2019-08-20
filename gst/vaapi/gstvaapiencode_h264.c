/*
 *  gstvaapiencode_h264.c - VA-API H.264 encoder
 *
 *  Copyright (C) 2012-2014 Intel Corporation
 *    Author: Wind Yuan <feng.yuan@intel.com>
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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

/**
 * SECTION:element-vaapih264enc
 * @short_description: A VA-API based H.264 video encoder
 *
 * Encodes raw video streams into H.264 bitstreams.
 *
 * The #GstVaapiEncodeH264:rate-control property controls the type of
 * encoding.  In case of Constant Bitrate Encoding (CBR), the
 * #GstVaapiEncodeH264:bitrate will determine the quality of the
 * encoding.  Alternatively, one may choose to perform Constant
 * Quantizer or Variable Bitrate Encoding (VBR), in which case the
 * #GstVaapiEncodeH264:bitrate is the maximum bitrate.
 *
 * The H264 profile that is eventually used depends on a few settings.
 * If #GstVaapiEncodeH264:dct8x8 is enabled, then High profile is
 * used.  Otherwise, if #GstVaapiEncodeH264:cabac entropy coding is
 * enabled or #GstVaapiEncodeH264:max-bframes are allowed, then Main
 * Profile is in effect. The element will alway go with the maximal
 * profile available in the caps negotation and otherwise Baseline
 * profile applies. But in some cases (e.g. hardware platforms) a more
 * restrictedprofile/level may be necessary. The recommended way to
 * set a profile is to set it in the downstream caps.
 *
 * You can also set parameters to adjust the latency of encoding:
 * #GstVaapiEncodeH264:quality-level is a number between 1-7, in the
 * case of the Intel VAAPI driver, where a lower value will produce a
 * higher quality output but with more latency; meanwhile a hihg
 * number will produce a lower quality output with less latency. Also
 * you can set #GstVaapiEncodeH264:tune, if your backend supports it,
 * for low-power mode or high compression.
 *
 * ## Example launch line
 *
 * |[
 *  gst-launch-1.0 -ev videotestsrc num-buffers=60 ! timeoverlay ! vaapih264enc ! h264parse ! mp4mux ! filesink location=test.mp4
 * ]|
 */

#include "gstcompat.h"
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiencoder_h264.h>
#include <gst/vaapi/gstvaapiutils_h264.h>
#include "gstvaapiencode_h264.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideomemory.h"

#define GST_PLUGIN_NAME "vaapih264enc"
#define GST_PLUGIN_DESC "A VA-API based H264 video encoder"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_h264_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_h264_encode_debug

#define GST_CODEC_CAPS                              \
  "video/x-h264, "                                  \
  "stream-format = (string) { avc, byte-stream }, " \
  "alignment = (string) au"

/* *INDENT-OFF* */
static const char gst_vaapiencode_h264_sink_caps_str[] =
  GST_VAAPI_MAKE_SURFACE_CAPS ", "
  GST_CAPS_INTERLACED_FALSE "; "
  GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL) ", "
  GST_CAPS_INTERLACED_FALSE ";"
  GST_VAAPI_MAKE_DMABUF_CAPS ","
  GST_CAPS_INTERLACED_FALSE;
/* *INDENT-ON* */

/* *INDENT-OFF* */
static const char gst_vaapiencode_h264_src_caps_str[] =
  GST_CODEC_CAPS ", "
  "profile = (string) { constrained-baseline, baseline, main, high, multiview-high, stereo-high }";
/* *INDENT-ON* */

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_vaapiencode_h264_sink_factory =
  GST_STATIC_PAD_TEMPLATE ("sink",
      GST_PAD_SINK,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS (gst_vaapiencode_h264_sink_caps_str));
/* *INDENT-ON* */

/* *INDENT-OFF* */
static GstStaticPadTemplate gst_vaapiencode_h264_src_factory =
  GST_STATIC_PAD_TEMPLATE ("src",
      GST_PAD_SRC,
      GST_PAD_ALWAYS,
      GST_STATIC_CAPS (gst_vaapiencode_h264_src_caps_str));
/* *INDENT-ON* */

/* h264 encode */
G_DEFINE_TYPE (GstVaapiEncodeH264, gst_vaapiencode_h264, GST_TYPE_VAAPIENCODE);

static void
gst_vaapiencode_h264_init (GstVaapiEncodeH264 * encode)
{
  /* nothing to do here */
}

static void
gst_vaapiencode_h264_finalize (GObject * object)
{
  GstVaapiEncodeH264 *const encode = GST_VAAPIENCODE_H264_CAST (object);

  gst_caps_replace (&encode->available_caps, NULL);
  G_OBJECT_CLASS (gst_vaapiencode_h264_parent_class)->finalize (object);
}

static GstVaapiProfile
gst_vaapiencode_h264_get_profile (GstCaps * caps)
{
  guint i;

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *const structure = gst_caps_get_structure (caps, i);
    const GValue *const value = gst_structure_get_value (structure, "profile");

    if (value && G_VALUE_HOLDS_STRING (value)) {
      const gchar *str = g_value_get_string (value);
      if (str)
        return gst_vaapi_utils_h264_get_profile_from_string (str);
    }
  }

  return GST_VAAPI_PROFILE_UNKNOWN;
}

typedef struct
{
  GstVaapiProfile best_profile;
  guint best_score;
} FindBestProfileData;

static void
find_best_profile_value (FindBestProfileData * data, const GValue * value)
{
  const gchar *str;
  GstVaapiProfile profile;
  guint score;

  if (!value || !G_VALUE_HOLDS_STRING (value))
    return;

  str = g_value_get_string (value);
  if (!str)
    return;
  profile = gst_vaapi_utils_h264_get_profile_from_string (str);
  if (!profile)
    return;
  score = gst_vaapi_utils_h264_get_profile_score (profile);
  if (score < data->best_score)
    return;
  data->best_profile = profile;
  data->best_score = score;
}

static GstVaapiProfile
find_best_profile (GstCaps * caps)
{
  FindBestProfileData data;
  guint i, j, num_structures, num_values;

  data.best_profile = GST_VAAPI_PROFILE_UNKNOWN;
  data.best_score = 0;

  num_structures = gst_caps_get_size (caps);
  for (i = 0; i < num_structures; i++) {
    GstStructure *const structure = gst_caps_get_structure (caps, i);
    const GValue *const value = gst_structure_get_value (structure, "profile");

    if (!value)
      continue;
    if (G_VALUE_HOLDS_STRING (value))
      find_best_profile_value (&data, value);
    else if (GST_VALUE_HOLDS_LIST (value)) {
      num_values = gst_value_list_get_size (value);
      for (j = 0; j < num_values; j++)
        find_best_profile_value (&data, gst_value_list_get_value (value, j));
    }
  }
  return data.best_profile;
}

static GstCaps *
get_available_caps (GstVaapiEncodeH264 * encode)
{
  GstCaps *out_caps;
  GArray *profiles;
  GstVaapiProfile profile;
  const gchar *profile_str;
  GValue profile_v = G_VALUE_INIT;
  GValue profile_list = G_VALUE_INIT;
  guint i;

  if (encode->available_caps)
    return encode->available_caps;

  g_value_init (&profile_list, GST_TYPE_LIST);
  g_value_init (&profile_v, G_TYPE_STRING);

  profiles =
      gst_vaapi_display_get_encode_profiles
      (GST_VAAPI_PLUGIN_BASE_DISPLAY (encode));
  if (!profiles)
    return NULL;

  for (i = 0; i < profiles->len; i++) {
    profile = g_array_index (profiles, GstVaapiProfile, i);
    if (gst_vaapi_profile_get_codec (profile) != GST_VAAPI_CODEC_H264)
      continue;
    profile_str = gst_vaapi_profile_get_name (profile);
    if (!profile_str)
      continue;
    g_value_set_string (&profile_v, profile_str);
    gst_value_list_append_value (&profile_list, &profile_v);
  }
  g_array_unref (profiles);

  out_caps = gst_caps_from_string (GST_CODEC_CAPS);
  gst_caps_set_value (out_caps, "profile", &profile_list);
  g_value_unset (&profile_list);
  g_value_unset (&profile_v);

  encode->available_caps = out_caps;

  return encode->available_caps;
}

static gboolean
gst_vaapiencode_h264_set_config (GstVaapiEncode * base_encode)
{
  GstVaapiEncodeH264 *const encode = GST_VAAPIENCODE_H264_CAST (base_encode);
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264 (base_encode->encoder);
  GstCaps *template_caps, *allowed_caps;
  gboolean ret = TRUE;

  template_caps =
      gst_static_pad_template_get_caps (&gst_vaapiencode_h264_src_factory);
  allowed_caps =
      gst_pad_get_allowed_caps (GST_VAAPI_PLUGIN_BASE_SRC_PAD (encode));

  if (allowed_caps == template_caps) {
    GST_INFO_OBJECT (encode, "downstream has ANY caps, outputting byte-stream");
    encode->is_avc = FALSE;
    gst_caps_unref (allowed_caps);
  } else if (!allowed_caps) {
    GST_INFO_OBJECT (encode,
        "downstream has NULL caps, outputting byte-stream");
    encode->is_avc = FALSE;
  } else if (gst_caps_is_empty (allowed_caps)) {
    GST_INFO_OBJECT (encode, "downstream has EMPTY caps");
    gst_caps_unref (template_caps);
    gst_caps_unref (allowed_caps);
    return FALSE;
  } else {
    const char *stream_format = NULL;
    GstStructure *structure;
    guint i, num_structures;
    GstVaapiProfile profile = GST_VAAPI_PROFILE_UNKNOWN;
    GstCaps *available_caps;

    available_caps = get_available_caps (encode);
    if (!available_caps) {
      gst_caps_unref (template_caps);
      gst_caps_unref (allowed_caps);
      return FALSE;
    }
    if (!gst_caps_can_intersect (allowed_caps, available_caps)) {
      GST_INFO_OBJECT (encode, "downstream requested an unsupported profile, "
          "but encoder will try to output a compatible one");

      /* Let's try the best profile in the allowed caps.
       * The internal encoder will fail later if it can't handle it */
      profile = find_best_profile (allowed_caps);

    } else {
      GstCaps *profile_caps;
      profile_caps = gst_caps_intersect (allowed_caps, available_caps);
      profile = find_best_profile (profile_caps);

      gst_caps_unref (profile_caps);
    }

    /* Check whether "stream-format" is avcC mode */
    num_structures = gst_caps_get_size (allowed_caps);
    for (i = 0; !stream_format && i < num_structures; i++) {
      structure = gst_caps_get_structure (allowed_caps, i);
      if (!gst_structure_has_field_typed (structure, "stream-format",
              G_TYPE_STRING))
        continue;
      stream_format = gst_structure_get_string (structure, "stream-format");
    }
    encode->is_avc = (g_strcmp0 (stream_format, "avc") == 0);

    if (profile != GST_VAAPI_PROFILE_UNKNOWN) {
      GST_INFO ("using %s profile as target decoder constraints",
          gst_vaapi_utils_h264_get_profile_string (profile));
      ret = gst_vaapi_encoder_h264_set_max_profile (encoder, profile);
    } else {
      ret = FALSE;
    }

    gst_caps_unref (allowed_caps);
  }
  gst_caps_unref (template_caps);

  base_encode->need_codec_data = encode->is_avc;

  return ret;
}

static void
set_compatible_profile (GstVaapiEncodeH264 * encode, GstCaps * caps,
    GstVaapiProfile profile)
{
  GstCaps *allowed_caps, *tmp_caps;
  gboolean ret = FALSE;

  allowed_caps =
      gst_pad_get_allowed_caps (GST_VAAPI_PLUGIN_BASE_SRC_PAD (encode));
  if (!allowed_caps || gst_caps_is_empty (allowed_caps)) {
    if (allowed_caps)
      gst_caps_unref (allowed_caps);
    return;
  }

  tmp_caps = gst_caps_from_string (GST_CODEC_CAPS);

  /* If profile doesn't exist in the allowed caps, let's find
   * compatible profile in the caps.
   *
   * If there is one, we can set it as a compatible profile and make
   * the negotiation.  We consider baseline compatible with
   * constrained-baseline, which is a strict subset of baseline
   * profile.
   */
retry:
  gst_caps_set_simple (tmp_caps, "profile", G_TYPE_STRING,
      gst_vaapi_utils_h264_get_profile_string (profile), NULL);

  if (!gst_caps_can_intersect (allowed_caps, tmp_caps)) {
    if (profile == GST_VAAPI_PROFILE_H264_CONSTRAINED_BASELINE) {
      profile = GST_VAAPI_PROFILE_H264_BASELINE;
      goto retry;
    }
  } else {
    gst_caps_set_simple (caps, "profile", G_TYPE_STRING,
        gst_vaapi_utils_h264_get_profile_string (profile), NULL);
    ret = TRUE;
  }

  GST_INFO_OBJECT (encode, "out caps %" GST_PTR_FORMAT, caps);

  if (!ret)
    GST_LOG ("There is no compatible profile in the requested caps.");

  gst_caps_unref (tmp_caps);
  gst_caps_unref (allowed_caps);
  return;
}

static GstCaps *
gst_vaapiencode_h264_get_caps (GstVaapiEncode * base_encode)
{
  GstVaapiEncodeH264 *const encode = GST_VAAPIENCODE_H264_CAST (base_encode);
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264 (base_encode->encoder);
  GstVaapiProfile profile;
  GstCaps *caps;

  caps = gst_caps_from_string (GST_CODEC_CAPS);

  gst_caps_set_simple (caps, "stream-format", G_TYPE_STRING,
      encode->is_avc ? "avc" : "byte-stream", NULL);

  /* Update profile determined by encoder */
  gst_vaapi_encoder_h264_get_profile_and_level (encoder, &profile, NULL);
  if (profile != GST_VAAPI_PROFILE_UNKNOWN)
    set_compatible_profile (encode, caps, profile);

  /* XXX: update level information */
  return caps;
}

static GstVaapiEncoder *
gst_vaapiencode_h264_alloc_encoder (GstVaapiEncode * base,
    GstVaapiDisplay * display)
{
  return gst_vaapi_encoder_h264_new (display);
}

/* h264 NAL byte stream operations */
static guint8 *
_h264_byte_stream_next_nal (guint8 * buffer, guint32 len, guint32 * nal_size)
{
  const guint8 *cur = buffer;
  const guint8 *const end = buffer + len;
  guint8 *nal_start = NULL;
  guint32 flag = 0xFFFFFFFF;
  guint32 nal_start_len = 0;

  g_assert (buffer && nal_size);
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

  /* ERRORS */
error:
  {
    gst_buffer_unmap (buf, &info);
    return FALSE;
  }
}

static GstFlowReturn
gst_vaapiencode_h264_alloc_buffer (GstVaapiEncode * base_encode,
    GstVaapiCodedBuffer * coded_buf, GstBuffer ** out_buffer_ptr)
{
  GstVaapiEncodeH264 *const encode = GST_VAAPIENCODE_H264_CAST (base_encode);
  GstVaapiEncoderH264 *const encoder =
      GST_VAAPI_ENCODER_H264 (base_encode->encoder);
  GstFlowReturn ret;

  g_return_val_if_fail (encoder != NULL, GST_FLOW_ERROR);

  ret =
      GST_VAAPIENCODE_CLASS (gst_vaapiencode_h264_parent_class)->alloc_buffer
      (base_encode, coded_buf, out_buffer_ptr);
  if (ret != GST_FLOW_OK)
    return ret;

  if (!encode->is_avc)
    return GST_FLOW_OK;

  /* Convert to avcC format */
  if (!_h264_convert_byte_stream_to_avc (*out_buffer_ptr))
    goto error_convert_buffer;
  return GST_FLOW_OK;

  /* ERRORS */
error_convert_buffer:
  {
    GST_ERROR ("failed to convert from bytestream format to avcC format");
    gst_buffer_replace (out_buffer_ptr, NULL);
    return GST_FLOW_ERROR;
  }
}

static void
gst_vaapiencode_h264_class_init (GstVaapiEncodeH264Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_CLASS (klass);
  gpointer encoder_class;

  GST_DEBUG_CATEGORY_INIT (gst_vaapi_h264_encode_debug,
      GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

  object_class->finalize = gst_vaapiencode_h264_finalize;
  object_class->set_property = gst_vaapiencode_set_property_subclass;
  object_class->get_property = gst_vaapiencode_get_property_subclass;

  encode_class->get_profile = gst_vaapiencode_h264_get_profile;
  encode_class->set_config = gst_vaapiencode_h264_set_config;
  encode_class->get_caps = gst_vaapiencode_h264_get_caps;
  encode_class->alloc_encoder = gst_vaapiencode_h264_alloc_encoder;
  encode_class->alloc_buffer = gst_vaapiencode_h264_alloc_buffer;

  gst_element_class_set_static_metadata (element_class,
      "VA-API H264 encoder",
      "Codec/Encoder/Video/Hardware",
      GST_PLUGIN_DESC, "Wind Yuan <feng.yuan@intel.com>");

  /* sink pad */
  gst_element_class_add_static_pad_template (element_class,
      &gst_vaapiencode_h264_sink_factory);

  /* src pad */
  gst_element_class_add_static_pad_template (element_class,
      &gst_vaapiencode_h264_src_factory);

  encoder_class = g_type_class_ref (GST_TYPE_VAAPI_ENCODER_H264);
  g_assert (encoder_class);
  gst_vaapiencode_class_install_properties (encode_class, encoder_class);
  g_type_class_unref (encoder_class);
}
