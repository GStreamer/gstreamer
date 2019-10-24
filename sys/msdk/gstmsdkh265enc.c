/* GStreamer Intel MSDK plugin
 * Copyright (c) 2016, Oblong Industries, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <mfxplugin.h>

#include <gst/allocators/gstdmabuf.h>

#include "gstmsdkh265enc.h"

GST_DEBUG_CATEGORY_EXTERN (gst_msdkh265enc_debug);
#define GST_CAT_DEFAULT gst_msdkh265enc_debug

enum
{
  PROP_LOW_POWER = GST_MSDKENC_PROP_MAX,
  PROP_TILE_ROW,
  PROP_TILE_COL,
};

#define PROP_LOWPOWER_DEFAULT           FALSE
#define PROP_TILE_ROW_DEFAULT           1
#define PROP_TILE_COL_DEFAULT           1

#define RAW_FORMATS "NV12, I420, YV12, YUY2, UYVY, BGRA, P010_10LE, VUYA"

#if (MFX_VERSION >= 1027)
#define COMMON_FORMAT "{ " RAW_FORMATS ", Y410 }"
#else
#define COMMON_FORMAT "{ " RAW_FORMATS " }"
#endif

static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_MSDK_CAPS_STR (COMMON_FORMAT,
            "{ NV12, P010_10LE }")));

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h265, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "stream-format = (string) byte-stream , alignment = (string) au , "
        "profile = (string) { main, main-10, main-444, main-444-10 } ")
    );

#define gst_msdkh265enc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkH265Enc, gst_msdkh265enc, GST_TYPE_MSDKENC);

static gboolean
gst_msdkh265enc_set_format (GstMsdkEnc * encoder)
{
  return TRUE;
}

static gboolean
gst_msdkh265enc_configure (GstMsdkEnc * encoder)
{
  GstMsdkH265Enc *h265enc = GST_MSDKH265ENC (encoder);
  mfxSession session;
  mfxStatus status;
  const mfxPluginUID *uid;

  session = gst_msdk_context_get_session (encoder->context);

  if (encoder->hardware)
    uid = &MFX_PLUGINID_HEVCE_HW;
  else
    uid = &MFX_PLUGINID_HEVCE_SW;

  status = MFXVideoUSER_Load (session, uid, 1);
  if (status < MFX_ERR_NONE) {
    GST_ERROR_OBJECT (h265enc, "Media SDK Plugin load failed (%s)",
        msdk_status_to_string (status));
    return FALSE;
  } else if (status > MFX_ERR_NONE) {
    GST_WARNING_OBJECT (h265enc, "Media SDK Plugin load warning: %s",
        msdk_status_to_string (status));
  }

  encoder->param.mfx.CodecId = MFX_CODEC_HEVC;

  switch (encoder->param.mfx.FrameInfo.FourCC) {
    case MFX_FOURCC_P010:
      encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_MAIN10;
      break;
    case MFX_FOURCC_AYUV:
#if (MFX_VERSION >= 1027)
    case MFX_FOURCC_Y410:
#endif
      encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_REXT;
      break;
    default:
      encoder->param.mfx.CodecProfile = MFX_PROFILE_HEVC_MAIN;
  }

  /* IdrInterval field of MediaSDK HEVC encoder behaves differently
   * than other encoders. IdrInteval == 1 indicate every
   * I-frame should be an IDR, IdrInteval == 2 means every other
   * I-frame is an IDR etc. So we generalize the behaviour of property
   * "i-frames" by incrementing the value by one in each case*/
  encoder->param.mfx.IdrInterval += 1;

  /* Enable Extended coding options */
  gst_msdkenc_ensure_extended_coding_options (encoder);

  if (h265enc->num_tile_rows > 1 || h265enc->num_tile_cols > 1) {
    h265enc->ext_tiles.Header.BufferId = MFX_EXTBUFF_HEVC_TILES;
    h265enc->ext_tiles.Header.BufferSz = sizeof (h265enc->ext_tiles);
    h265enc->ext_tiles.NumTileRows = h265enc->num_tile_rows;
    h265enc->ext_tiles.NumTileColumns = h265enc->num_tile_cols;

    gst_msdkenc_add_extra_param (encoder,
        (mfxExtBuffer *) & h265enc->ext_tiles);

    /* Set a valid value to NumSlice */
    if (encoder->param.mfx.NumSlice == 0)
      encoder->param.mfx.NumSlice =
          h265enc->num_tile_rows * h265enc->num_tile_cols;
  }

  encoder->param.mfx.LowPower =
      (h265enc->lowpower ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF);

  return TRUE;
}

static inline const gchar *
level_to_string (gint level)
{
  switch (level) {
    case MFX_LEVEL_HEVC_1:
      return "1";
    case MFX_LEVEL_HEVC_2:
      return "2";
    case MFX_LEVEL_HEVC_21:
      return "2.1";
    case MFX_LEVEL_HEVC_3:
      return "3";
    case MFX_LEVEL_HEVC_31:
      return "3.1";
    case MFX_LEVEL_HEVC_4:
      return "4";
    case MFX_LEVEL_HEVC_41:
      return "4.1";
    case MFX_LEVEL_HEVC_5:
      return "5";
    case MFX_LEVEL_HEVC_51:
      return "5.1";
    case MFX_LEVEL_HEVC_52:
      return "5.2";
    case MFX_LEVEL_HEVC_6:
      return "6";
    case MFX_LEVEL_HEVC_61:
      return "6.1";
    case MFX_LEVEL_HEVC_62:
      return "6.2";
    default:
      break;
  }

  return NULL;
}

static GstCaps *
gst_msdkh265enc_set_src_caps (GstMsdkEnc * encoder)
{
  GstCaps *caps;
  GstStructure *structure;
  const gchar *level;

  caps = gst_caps_new_empty_simple ("video/x-h265");
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_set (structure, "stream-format", G_TYPE_STRING, "byte-stream",
      NULL);

  gst_structure_set (structure, "alignment", G_TYPE_STRING, "au", NULL);

  switch (encoder->param.mfx.FrameInfo.FourCC) {
    case MFX_FOURCC_P010:
      gst_structure_set (structure, "profile", G_TYPE_STRING, "main-10", NULL);
      break;
    case MFX_FOURCC_AYUV:
      gst_structure_set (structure, "profile", G_TYPE_STRING, "main-444", NULL);
      break;
#if (MFX_VERSION >= 1027)
    case MFX_FOURCC_Y410:
      gst_structure_set (structure, "profile", G_TYPE_STRING, "main-444-10",
          NULL);
      break;
#endif
    default:
      gst_structure_set (structure, "profile", G_TYPE_STRING, "main", NULL);
      break;
  }

  level = level_to_string (encoder->param.mfx.CodecLevel);
  if (level)
    gst_structure_set (structure, "level", G_TYPE_STRING, level, NULL);

  return caps;
}

static void
gst_msdkh265enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkH265Enc *thiz = GST_MSDKH265ENC (object);

  if (gst_msdkenc_set_common_property (object, prop_id, value, pspec))
    return;

  GST_OBJECT_LOCK (thiz);

  switch (prop_id) {
    case PROP_LOW_POWER:
      thiz->lowpower = g_value_get_boolean (value);
      break;

    case PROP_TILE_ROW:
      thiz->num_tile_rows = g_value_get_uint (value);
      break;

    case PROP_TILE_COL:
      thiz->num_tile_cols = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static void
gst_msdkh265enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkH265Enc *thiz = GST_MSDKH265ENC (object);

  if (gst_msdkenc_get_common_property (object, prop_id, value, pspec))
    return;

  GST_OBJECT_LOCK (thiz);
  switch (prop_id) {
    case PROP_LOW_POWER:
      g_value_set_boolean (value, thiz->lowpower);
      break;

    case PROP_TILE_ROW:
      g_value_set_uint (value, thiz->num_tile_rows);
      break;

    case PROP_TILE_COL:
      g_value_set_uint (value, thiz->num_tile_cols);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static void
gst_msdkh265enc_class_init (GstMsdkH265EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstMsdkEncClass *encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  encoder_class = GST_MSDKENC_CLASS (klass);

  gobject_class->set_property = gst_msdkh265enc_set_property;
  gobject_class->get_property = gst_msdkh265enc_get_property;

  encoder_class->set_format = gst_msdkh265enc_set_format;
  encoder_class->configure = gst_msdkh265enc_configure;
  encoder_class->set_src_caps = gst_msdkh265enc_set_src_caps;

  gst_msdkenc_install_common_properties (encoder_class);

  g_object_class_install_property (gobject_class, PROP_LOW_POWER,
      g_param_spec_boolean ("low-power", "Low power", "Enable low power mode",
          PROP_LOWPOWER_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TILE_ROW,
      g_param_spec_uint ("num-tile-rows", "number of rows for tiled encoding",
          "number of rows for tiled encoding",
          1, 8192, PROP_TILE_ROW_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TILE_COL,
      g_param_spec_uint ("num-tile-cols",
          "number of columns for tiled encoding",
          "number of columns for tiled encoding", 1, 8192,
          PROP_TILE_COL_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK H265 encoder",
      "Codec/Encoder/Video/Hardware",
      "H265 video encoder based on Intel Media SDK",
      "Josep Torra <jtorra@oblong.com>");

  gst_element_class_add_static_pad_template (element_class, &sink_factory);
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkh265enc_init (GstMsdkH265Enc * thiz)
{
  GstMsdkEnc *msdk_enc = (GstMsdkEnc *) thiz;
  thiz->lowpower = PROP_LOWPOWER_DEFAULT;
  thiz->num_tile_rows = PROP_TILE_ROW_DEFAULT;
  thiz->num_tile_cols = PROP_TILE_COL_DEFAULT;
  msdk_enc->num_extra_frames = 1;
}
