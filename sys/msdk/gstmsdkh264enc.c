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

#include "gstmsdkh264enc.h"

#include <gst/base/base.h>
#include <gst/pbutils/pbutils.h>

GST_DEBUG_CATEGORY_EXTERN (gst_msdkh264enc_debug);
#define GST_CAT_DEFAULT gst_msdkh264enc_debug

enum
{
  PROP_CABAC = GST_MSDKENC_PROP_MAX,
  PROP_LOW_POWER,
  PROP_FRAME_PACKING,
  PROP_RC_LA_DOWNSAMPLING,
  PROP_TRELLIS,
  PROP_MAX_SLICE_SIZE,
  PROP_B_PYRAMID
};

#define PROP_CABAC_DEFAULT              TRUE
#define PROP_LOWPOWER_DEFAULT           FALSE
#define PROP_FRAME_PACKING_DEFAULT      -1
#define PROP_RC_LA_DOWNSAMPLING_DEFAULT MFX_LOOKAHEAD_DS_UNKNOWN
#define PROP_TRELLIS_DEFAULT            _MFX_TRELLIS_NONE
#define PROP_MAX_SLICE_SIZE_DEFAULT     0
#define PROP_B_PYRAMID_DEFAULT          FALSE

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "framerate = (fraction) [0/1, MAX], "
        "width = (int) [ 1, MAX ], height = (int) [ 1, MAX ], "
        "stream-format = (string) byte-stream , alignment = (string) au , "
        "profile = (string) { high, main, baseline, constrained-baseline }")
    );

static GType
gst_msdkh264enc_frame_packing_get_type (void)
{
  static GType format_type = 0;
  static const GEnumValue format_types[] = {
    {GST_VIDEO_MULTIVIEW_FRAME_PACKING_NONE, "None (default)", "none"},
    {GST_VIDEO_MULTIVIEW_FRAME_PACKING_SIDE_BY_SIDE, "Side by Side",
        "side-by-side"},
    {GST_VIDEO_MULTIVIEW_FRAME_PACKING_TOP_BOTTOM, "Top Bottom", "top-bottom"},
    {0, NULL, NULL}
  };

  if (!format_type) {
    format_type =
        g_enum_register_static ("GstMsdkH264EncFramePacking", format_types);
  }

  return format_type;
}

#define gst_msdkh264enc_parent_class parent_class
G_DEFINE_TYPE (GstMsdkH264Enc, gst_msdkh264enc, GST_TYPE_MSDKENC);

static void
insert_frame_packing_sei (GstMsdkH264Enc * thiz, GstVideoCodecFrame * frame,
    GstVideoMultiviewMode mode)
{
  GstMapInfo map;
  GstByteReader reader;
  guint offset;

  if (mode != GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE
      && mode != GST_VIDEO_MULTIVIEW_MODE_TOP_BOTTOM) {
    GST_ERROR_OBJECT (thiz, "Unsupported multiview mode %d", mode);
    return;
  }

  GST_DEBUG ("Inserting SEI Frame Packing for multiview mode %d", mode);

  gst_buffer_map (frame->output_buffer, &map, GST_MAP_READ);
  gst_byte_reader_init (&reader, map.data, map.size);

  while ((offset =
          gst_byte_reader_masked_scan_uint32 (&reader, 0xffffff00, 0x00000100,
              0, gst_byte_reader_get_remaining (&reader))) != -1) {
    guint8 type;
    guint offset2;

    gst_byte_reader_skip_unchecked (&reader, offset + 3);
    if (!gst_byte_reader_get_uint8 (&reader, &type))
      goto done;
    type = type & 0x1f;

    offset2 =
        gst_byte_reader_masked_scan_uint32 (&reader, 0xffffff00, 0x00000100, 0,
        gst_byte_reader_get_remaining (&reader));
    if (offset2 == -1)
      offset2 = gst_byte_reader_get_remaining (&reader);

    /* Slice, should really be an IDR slice (5) */
    if (type >= 1 && type <= 5) {
      GstBuffer *new_buffer;
      GstMemory *mem;
      static const guint8 sei_top_bottom[] =
          { 0x00, 0x00, 0x01, 0x06, 0x2d, 0x07, 0x82, 0x01,
        0x00, 0x00, 0x03, 0x00, 0x01, 0x20, 0x80
      };
      static const guint8 sei_side_by_side[] =
          { 0x00, 0x00, 0x01, 0x06, 0x2d, 0x07, 0x81, 0x81,
        0x00, 0x00, 0x03, 0x00, 0x01, 0x20, 0x80
      };
      const guint8 *sei;
      guint sei_size;

      if (mode == GST_VIDEO_MULTIVIEW_MODE_SIDE_BY_SIDE) {
        sei = sei_side_by_side;
        sei_size = sizeof (sei_side_by_side);
      } else {
        sei = sei_top_bottom;
        sei_size = sizeof (sei_top_bottom);
      }

      /* Create frame packing SEI
       * FIXME: This assumes it does not exist in the stream, which is not
       * going to be true anymore once this is fixed:
       * https://github.com/Intel-Media-SDK/MediaSDK/issues/13
       */
      new_buffer = gst_buffer_new ();

      /* Copy all metadata */
      gst_buffer_copy_into (new_buffer, frame->output_buffer,
          GST_BUFFER_COPY_METADATA, 0, -1);

      /* Copy previous NALs */
      gst_buffer_copy_into (new_buffer, frame->output_buffer,
          GST_BUFFER_COPY_MEMORY, 0, gst_byte_reader_get_pos (&reader) - 4);

      mem =
          gst_memory_new_wrapped (0, g_memdup (sei, sei_size), sei_size, 0,
          sei_size, NULL, g_free);
      gst_buffer_append_memory (new_buffer, mem);
      gst_buffer_copy_into (new_buffer, frame->output_buffer,
          GST_BUFFER_COPY_MEMORY, gst_byte_reader_get_pos (&reader) - 4, -1);

      gst_buffer_unmap (frame->output_buffer, &map);
      gst_buffer_unref (frame->output_buffer);
      frame->output_buffer = new_buffer;
      return;
    }
  }

done:
  gst_buffer_unmap (frame->output_buffer, &map);
}

static GstFlowReturn
gst_msdkh264enc_pre_push (GstVideoEncoder * encoder, GstVideoCodecFrame * frame)
{
  GstMsdkH264Enc *thiz = GST_MSDKH264ENC (encoder);

  if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame) &&
      (thiz->frame_packing != GST_VIDEO_MULTIVIEW_MODE_NONE ||
          ((GST_VIDEO_INFO_MULTIVIEW_MODE (&thiz->base.input_state->info) !=
                  GST_VIDEO_MULTIVIEW_MODE_NONE)
              && GST_VIDEO_INFO_MULTIVIEW_MODE (&thiz->base.
                  input_state->info) != GST_VIDEO_MULTIVIEW_MODE_MONO))) {
    insert_frame_packing_sei (thiz, frame,
        thiz->frame_packing !=
        GST_VIDEO_MULTIVIEW_MODE_NONE ? thiz->frame_packing :
        GST_VIDEO_INFO_MULTIVIEW_MODE (&thiz->base.input_state->info));
  }

  return GST_FLOW_OK;
}

static gboolean
gst_msdkh264enc_set_format (GstMsdkEnc * encoder)
{
  GstMsdkH264Enc *thiz = GST_MSDKH264ENC (encoder);
  GstCaps *template_caps;
  GstCaps *allowed_caps = NULL;

  thiz->profile = 0;
  thiz->level = 0;

  template_caps = gst_static_pad_template_get_caps (&src_factory);
  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_ENCODER_SRC_PAD (encoder));

  /* If downstream has ANY caps let encoder decide profile and level */
  if (allowed_caps == template_caps) {
    GST_INFO_OBJECT (thiz,
        "downstream has ANY caps, profile/level set to auto");
  } else if (allowed_caps) {
    GstStructure *s;
    const gchar *profile;
    const gchar *level;

    if (gst_caps_is_empty (allowed_caps)) {
      gst_caps_unref (allowed_caps);
      gst_caps_unref (template_caps);
      return FALSE;
    }

    allowed_caps = gst_caps_make_writable (allowed_caps);
    allowed_caps = gst_caps_fixate (allowed_caps);
    s = gst_caps_get_structure (allowed_caps, 0);

    profile = gst_structure_get_string (s, "profile");
    if (profile) {
      if (!strcmp (profile, "high")) {
        thiz->profile = MFX_PROFILE_AVC_HIGH;
      } else if (!strcmp (profile, "main")) {
        thiz->profile = MFX_PROFILE_AVC_MAIN;
      } else if (!strcmp (profile, "baseline")) {
        thiz->profile = MFX_PROFILE_AVC_BASELINE;
      } else if (!strcmp (profile, "constrained-baseline")) {
        thiz->profile = MFX_PROFILE_AVC_CONSTRAINED_BASELINE;
      } else {
        g_assert_not_reached ();
      }
    }

    level = gst_structure_get_string (s, "level");
    if (level) {
      thiz->level = gst_codec_utils_h264_get_level_idc (level);
    }

    gst_caps_unref (allowed_caps);
  }

  gst_caps_unref (template_caps);

  return TRUE;
}

static gboolean
gst_msdkh264enc_configure (GstMsdkEnc * encoder)
{
  GstMsdkH264Enc *thiz = GST_MSDKH264ENC (encoder);

  encoder->param.mfx.LowPower =
      (thiz->lowpower ? MFX_CODINGOPTION_ON : MFX_CODINGOPTION_OFF);
  encoder->param.mfx.CodecId = MFX_CODEC_AVC;
  encoder->param.mfx.CodecProfile = thiz->profile;
  encoder->param.mfx.CodecLevel = thiz->level;

  thiz->option.Header.BufferId = MFX_EXTBUFF_CODING_OPTION;
  thiz->option.Header.BufferSz = sizeof (thiz->option);
  if (thiz->profile == MFX_PROFILE_AVC_CONSTRAINED_BASELINE ||
      thiz->profile == MFX_PROFILE_AVC_BASELINE ||
      thiz->profile == MFX_PROFILE_AVC_EXTENDED) {
    thiz->option.CAVLC = MFX_CODINGOPTION_ON;
  } else {
    thiz->option.CAVLC =
        (thiz->cabac ? MFX_CODINGOPTION_OFF : MFX_CODINGOPTION_ON);
  }

  gst_msdkenc_add_extra_param (encoder, (mfxExtBuffer *) & thiz->option);

  encoder->option2.Trellis = thiz->trellis ? thiz->trellis : MFX_TRELLIS_OFF;
  encoder->option2.MaxSliceSize = thiz->max_slice_size;
  if (encoder->rate_control == MFX_RATECONTROL_LA ||
      encoder->rate_control == MFX_RATECONTROL_LA_HRD ||
      encoder->rate_control == MFX_RATECONTROL_LA_ICQ)
    encoder->option2.LookAheadDS = thiz->lookahead_ds;

  if (thiz->b_pyramid) {
    encoder->option2.BRefType = MFX_B_REF_PYRAMID;
    /* Don't define Gop structure for B-pyramid, otherwise EncodeInit
     * will throw Invalid param error */
    encoder->param.mfx.GopRefDist = 0;
  }

  /* Enable Extended coding options */
  gst_msdkenc_ensure_extended_coding_options (encoder);

  return TRUE;
}

static inline const gchar *
profile_to_string (gint profile)
{
  switch (profile) {
    case MFX_PROFILE_AVC_HIGH:
      return "high";
    case MFX_PROFILE_AVC_MAIN:
      return "main";
    case MFX_PROFILE_AVC_BASELINE:
      return "baseline";
    case MFX_PROFILE_AVC_CONSTRAINED_BASELINE:
      return "constrained-baseline";
    default:
      break;
  }

  return NULL;
}

static inline const gchar *
level_to_string (gint level)
{
  switch (level) {
    case MFX_LEVEL_AVC_1:
      return "1";
    case MFX_LEVEL_AVC_1b:
      return "1.1";
    case MFX_LEVEL_AVC_11:
      return "1.1";
    case MFX_LEVEL_AVC_12:
      return "1.2";
    case MFX_LEVEL_AVC_13:
      return "1.3";
    case MFX_LEVEL_AVC_2:
      return "2";
    case MFX_LEVEL_AVC_21:
      return "2.1";
    case MFX_LEVEL_AVC_22:
      return "2.2";
    case MFX_LEVEL_AVC_3:
      return "3";
    case MFX_LEVEL_AVC_31:
      return "3.1";
    case MFX_LEVEL_AVC_32:
      return "3.2";
    case MFX_LEVEL_AVC_4:
      return "4";
    case MFX_LEVEL_AVC_41:
      return "4.1";
    case MFX_LEVEL_AVC_42:
      return "4.2";
    case MFX_LEVEL_AVC_5:
      return "5";
    case MFX_LEVEL_AVC_51:
      return "5.1";
    case MFX_LEVEL_AVC_52:
      return "5.2";
    default:
      break;
  }

  return NULL;
}

static GstCaps *
gst_msdkh264enc_set_src_caps (GstMsdkEnc * encoder)
{
  GstCaps *caps;
  GstStructure *structure;
  const gchar *profile;
  const gchar *level;

  caps = gst_caps_new_empty_simple ("video/x-h264");
  structure = gst_caps_get_structure (caps, 0);

  gst_structure_set (structure, "stream-format", G_TYPE_STRING, "byte-stream",
      NULL);

  gst_structure_set (structure, "alignment", G_TYPE_STRING, "au", NULL);

  profile = profile_to_string (encoder->param.mfx.CodecProfile);
  if (profile)
    gst_structure_set (structure, "profile", G_TYPE_STRING, profile, NULL);

  level = level_to_string (encoder->param.mfx.CodecLevel);
  if (level)
    gst_structure_set (structure, "level", G_TYPE_STRING, level, NULL);

  return caps;
}

static void
gst_msdkh264enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstMsdkH264Enc *thiz = GST_MSDKH264ENC (object);

  if (gst_msdkenc_set_common_property (object, prop_id, value, pspec))
    return;

  GST_OBJECT_LOCK (thiz);

  switch (prop_id) {
    case PROP_CABAC:
      thiz->cabac = g_value_get_boolean (value);
      break;
    case PROP_LOW_POWER:
      thiz->lowpower = g_value_get_boolean (value);
      break;
    case PROP_FRAME_PACKING:
      thiz->frame_packing = g_value_get_enum (value);
      break;
    case PROP_RC_LA_DOWNSAMPLING:
      thiz->lookahead_ds = g_value_get_enum (value);
      break;
    case PROP_TRELLIS:
      thiz->trellis = g_value_get_flags (value);
      break;
    case PROP_MAX_SLICE_SIZE:
      thiz->max_slice_size = g_value_get_uint (value);
      break;
    case PROP_B_PYRAMID:
      thiz->b_pyramid = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
  return;
}

static void
gst_msdkh264enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstMsdkH264Enc *thiz = GST_MSDKH264ENC (object);

  if (gst_msdkenc_get_common_property (object, prop_id, value, pspec))
    return;

  GST_OBJECT_LOCK (thiz);
  switch (prop_id) {
    case PROP_CABAC:
      g_value_set_boolean (value, thiz->cabac);
      break;
    case PROP_LOW_POWER:
      g_value_set_boolean (value, thiz->lowpower);
      break;
    case PROP_FRAME_PACKING:
      g_value_set_enum (value, thiz->frame_packing);
      break;
    case PROP_RC_LA_DOWNSAMPLING:
      g_value_set_enum (value, thiz->lookahead_ds);
      break;
    case PROP_TRELLIS:
      g_value_set_flags (value, thiz->trellis);
      break;
    case PROP_MAX_SLICE_SIZE:
      g_value_set_uint (value, thiz->max_slice_size);
      break;
    case PROP_B_PYRAMID:
      g_value_set_boolean (value, thiz->b_pyramid);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  GST_OBJECT_UNLOCK (thiz);
}

static void
gst_msdkh264enc_class_init (GstMsdkH264EncClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;
  GstVideoEncoderClass *videoencoder_class;
  GstMsdkEncClass *encoder_class;

  gobject_class = G_OBJECT_CLASS (klass);
  element_class = GST_ELEMENT_CLASS (klass);
  videoencoder_class = GST_VIDEO_ENCODER_CLASS (klass);
  encoder_class = GST_MSDKENC_CLASS (klass);

  gobject_class->set_property = gst_msdkh264enc_set_property;
  gobject_class->get_property = gst_msdkh264enc_get_property;

  videoencoder_class->pre_push = gst_msdkh264enc_pre_push;

  encoder_class->set_format = gst_msdkh264enc_set_format;
  encoder_class->configure = gst_msdkh264enc_configure;
  encoder_class->set_src_caps = gst_msdkh264enc_set_src_caps;

  gst_msdkenc_install_common_properties (encoder_class);

  g_object_class_install_property (gobject_class, PROP_CABAC,
      g_param_spec_boolean ("cabac", "CABAC", "Enable CABAC entropy coding",
          PROP_CABAC_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_LOW_POWER,
      g_param_spec_boolean ("low-power", "Low power", "Enable low power mode",
          PROP_LOWPOWER_DEFAULT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FRAME_PACKING,
      g_param_spec_enum ("frame-packing", "Frame Packing",
          "Set frame packing mode for Stereoscopic content",
          gst_msdkh264enc_frame_packing_get_type (), PROP_FRAME_PACKING_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_RC_LA_DOWNSAMPLING,
      g_param_spec_enum ("rc-lookahead-ds", "Look-ahead Downsampling",
          "Down sampling mode in look ahead bitrate control",
          gst_msdkenc_rc_lookahead_ds_get_type (),
          PROP_RC_LA_DOWNSAMPLING_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_TRELLIS,
      g_param_spec_flags ("trellis", "Trellis",
          "Enable Trellis Quantization",
          gst_msdkenc_trellis_quantization_get_type (), _MFX_TRELLIS_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_MAX_SLICE_SIZE,
      g_param_spec_uint ("max-slice-size", "Max Slice Size",
          "Maximum slice size in bytes (if enabled MSDK will ignore the control over num-slices)",
          0, G_MAXUINT32, PROP_MAX_SLICE_SIZE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_B_PYRAMID,
      g_param_spec_boolean ("b-pyramid", "B-pyramid",
          "Enable B-Pyramid Referene structure", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (element_class,
      "Intel MSDK H264 encoder", "Codec/Encoder/Video",
      "H264 video encoder based on Intel Media SDK",
      "Josep Torra <jtorra@oblong.com>");
  gst_element_class_add_static_pad_template (element_class, &src_factory);
}

static void
gst_msdkh264enc_init (GstMsdkH264Enc * thiz)
{
  thiz->cabac = PROP_CABAC_DEFAULT;
  thiz->lowpower = PROP_LOWPOWER_DEFAULT;
  thiz->frame_packing = PROP_FRAME_PACKING_DEFAULT;
  thiz->lookahead_ds = PROP_RC_LA_DOWNSAMPLING_DEFAULT;
  thiz->trellis = PROP_TRELLIS_DEFAULT;
  thiz->max_slice_size = PROP_MAX_SLICE_SIZE_DEFAULT;
  thiz->b_pyramid = PROP_B_PYRAMID_DEFAULT;
}
