/*
 *  gstvaapiencode_vp9.c - VA-API VP9 encoder
 *
 *  Copyright (C) 2016 Intel Corporation
 *    Author: Sreerenj Balachandran <sreerenj.balachandran@intel.com>
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
 * SECTION:element-vaapivp9enc
 * @short_description: A VA-API based VP9 video encoder
 *
 * Encodes raw video streams into VP9 bitstreams.
 *
 * ## Example launch line
 *
 * |[
 *  gst-launch-1.0 -ev videotestsrc num-buffers=60 ! timeoverlay ! vaapivp9enc ! matroskamux ! filesink location=test.mkv
 * ]|
 */

#include "gstcompat.h"
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiencoder_vp9.h>
#include <gst/vaapi/gstvaapiutils_vpx.h>
#include "gstvaapiencode_vp9.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideomemory.h"

#define GST_PLUGIN_NAME "vaapivp9enc"
#define GST_PLUGIN_DESC "A VA-API based VP9 video encoder"

GST_DEBUG_CATEGORY_STATIC (gst_vaapi_vp9_encode_debug);
#define GST_CAT_DEFAULT gst_vaapi_vp9_encode_debug

#define GST_CODEC_CAPS                          \
  "video/x-vp9"

#define EXTRA_FORMATS {}

/* vp9 encode */
GST_VAAPI_ENCODE_REGISTER_TYPE (vp9, VP9, VP9, EXTRA_FORMATS,
    gst_vaapi_utils_vp9_get_profile_string);

static void
gst_vaapiencode_vp9_init (GstVaapiEncodeVP9 * encode)
{
  /* nothing to do here */
}

static void
gst_vaapiencode_vp9_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_vaapiencode_vp9_parent_class)->finalize (object);
}

static GArray *
gst_vaapiencode_vp9_get_allowed_profiles (GstVaapiEncode * encode,
    GstCaps * allowed)
{
  return gst_vaapi_encoder_get_profiles_from_caps (allowed,
      gst_vaapi_utils_vp9_get_profile_from_string);
}

static GstCaps *
gst_vaapiencode_vp9_get_caps (GstVaapiEncode * base_encode)
{
  GstCaps *caps;
  GstVaapiProfile profile;
  const gchar *profile_str;

  caps = gst_caps_from_string (GST_CODEC_CAPS);
  profile = gst_vaapi_encoder_get_profile (base_encode->encoder);
  profile_str = gst_vaapi_utils_vp9_get_profile_string (profile);
  if (profile_str)
    gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile_str, NULL);

  return caps;
}

static GstVaapiEncoder *
gst_vaapiencode_vp9_alloc_encoder (GstVaapiEncode * base,
    GstVaapiDisplay * display)
{
  return gst_vaapi_encoder_vp9_new (display);
}

static gboolean
gst_vaapiencode_vp9_set_config (GstVaapiEncode * base_encode)
{
  GstVaapiEncoderVP9 *const encoder =
      GST_VAAPI_ENCODER_VP9 (base_encode->encoder);
  GstCaps *allowed_caps = NULL;
  GstCaps *template_caps = NULL;
  GArray *profiles = NULL;
  GArray *profiles_hw = NULL;
  GArray *profiles_allowed = NULL;
  GstVaapiProfile profile;
  gboolean ret = TRUE;
  guint i, j;

  profiles_hw = gst_vaapi_display_get_encode_profiles_by_codec
      (GST_VAAPI_PLUGIN_BASE_DISPLAY (base_encode), GST_VAAPI_CODEC_VP9);
  if (!profiles_hw) {
    ret = FALSE;
    goto out;
  }

  template_caps =
      gst_pad_get_pad_template_caps (GST_VAAPI_PLUGIN_BASE_SRC_PAD
      (base_encode));
  allowed_caps =
      gst_pad_get_allowed_caps (GST_VAAPI_PLUGIN_BASE_SRC_PAD (base_encode));
  if (!allowed_caps || allowed_caps == template_caps) {
    ret = gst_vaapi_encoder_vp9_set_allowed_profiles (encoder, profiles_hw);
    goto out;
  } else if (gst_caps_is_empty (allowed_caps)) {
    ret = FALSE;
    goto out;
  }

  profiles = gst_vaapi_encoder_get_profiles_from_caps (allowed_caps,
      gst_vaapi_utils_vp9_get_profile_from_string);
  if (!profiles) {
    ret = FALSE;
    goto out;
  }

  profiles_allowed = g_array_new (FALSE, FALSE, sizeof (GstVaapiProfile));
  if (!profiles_allowed) {
    ret = FALSE;
    goto out;
  }

  for (i = 0; i < profiles->len; i++) {
    profile = g_array_index (profiles, GstVaapiProfile, i);
    for (j = 0; j < profiles_hw->len; j++) {
      GstVaapiProfile p = g_array_index (profiles_hw, GstVaapiProfile, j);
      if (p == profile) {
        g_array_append_val (profiles_allowed, profile);
        break;
      }
    }
  }
  if (profiles_allowed->len == 0) {
    ret = FALSE;
    goto out;
  }

  ret = gst_vaapi_encoder_vp9_set_allowed_profiles (encoder, profiles_allowed);

out:
  if (allowed_caps)
    gst_caps_unref (allowed_caps);
  if (template_caps)
    gst_caps_unref (template_caps);
  if (profiles)
    g_array_unref (profiles);
  if (profiles_hw)
    g_array_unref (profiles_hw);
  if (profiles_allowed)
    g_array_unref (profiles_allowed);

  return ret;
}

static void
gst_vaapiencode_vp9_class_init (GstVaapiEncodeVP9Class * klass, gpointer data)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstVaapiEncodeClass *const encode_class = GST_VAAPIENCODE_CLASS (klass);
  GstCaps *sink_caps = ((GstVaapiEncodeInitData *) data)->sink_caps;
  GstCaps *src_caps = ((GstVaapiEncodeInitData *) data)->src_caps;
  GstPadTemplate *templ;
  GstCaps *static_caps;
  gpointer encoder_class;

  object_class->finalize = gst_vaapiencode_vp9_finalize;
  object_class->set_property = gst_vaapiencode_set_property_subclass;
  object_class->get_property = gst_vaapiencode_get_property_subclass;

  encode_class->get_allowed_profiles = gst_vaapiencode_vp9_get_allowed_profiles;
  encode_class->get_caps = gst_vaapiencode_vp9_get_caps;
  encode_class->alloc_encoder = gst_vaapiencode_vp9_alloc_encoder;
  encode_class->set_config = gst_vaapiencode_vp9_set_config;

  gst_element_class_set_static_metadata (element_class,
      "VA-API VP9 encoder",
      "Codec/Encoder/Video/Hardware",
      GST_PLUGIN_DESC,
      "Sreerenj Balachandran <sreerenj.balachandran@intel.com>");

  /* sink pad */
  g_assert (sink_caps);
  static_caps = gst_caps_from_string (GST_VAAPI_ENCODE_STATIC_SINK_CAPS);
  templ =
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, sink_caps);
  gst_pad_template_set_documentation_caps (templ, static_caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_caps_unref (static_caps);
  gst_caps_unref (sink_caps);

  /* src pad */
  g_assert (src_caps);
  static_caps = gst_caps_from_string (GST_CODEC_CAPS);
  templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, src_caps);
  gst_pad_template_set_documentation_caps (templ, static_caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_caps_unref (static_caps);
  gst_caps_unref (src_caps);

  encoder_class = g_type_class_ref (GST_TYPE_VAAPI_ENCODER_VP9);
  g_assert (encoder_class);
  gst_vaapiencode_class_install_properties (encode_class, encoder_class);
  g_type_class_unref (encoder_class);
}
