/*
 * Copyright (C) 2016 Sebastian Dröge <sebastian@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstfdkaacenc.h"

#include <gst/pbutils/pbutils.h>

#include <string.h>

/* TODO:
 * - Add support for other AOT / profiles
 * - Expose more properties, e.g. afterburner and vbr
 * - Signal encoder delay
 * - LOAS / LATM support
 */

enum
{
  PROP_0,
  PROP_BITRATE
};

#define DEFAULT_BITRATE (0)

#define SAMPLE_RATES " 8000, " \
                    "11025, " \
                    "12000, " \
                    "16000, " \
                    "22050, " \
                    "24000, " \
                    "32000, " \
                    "44100, " \
                    "48000, " \
                    "64000, " \
                    "88200, " \
                    "96000"

static const struct
{
  gint channels;
  CHANNEL_MODE mode;
  GstAudioChannelPosition positions[8];
} channel_layouts[] = {
  {
    1, MODE_1, {
  GST_AUDIO_CHANNEL_POSITION_MONO}}, {
    2, MODE_2, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}}, {
    3, MODE_1_2, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT}}, {
    3, MODE_2_1, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_LFE1}}, {
    4, MODE_1_2_1, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_REAR_CENTER}}, {
    5, MODE_1_2_2, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
          GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT}}, {
    6, MODE_1_2_2_1, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
          GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_LFE1}}
#ifdef HAVE_FDK_AAC_0_1_4
  , {
    8, MODE_7_1_REAR_SURROUND, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
          GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_REAR_LEFT,
          GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_LFE1}}, {
    8, MODE_7_1_FRONT_CENTER, {
  GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER,
          GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT,
          GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT,
          GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT,
          GST_AUDIO_CHANNEL_POSITION_LFE1}}
#endif
};

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) " GST_AUDIO_NE (S16) ", "
        "layout = (string) interleaved, "
        "rate = (int) { " SAMPLE_RATES " }, "
        "channels = (int) {1, 2, 3, 4, 5, 6, 8}")
    );

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/mpeg, "
        "mpegversion = (int) 4, "
        "rate = (int) { " SAMPLE_RATES " }, "
        "channels = (int) {1, 2, 3, 4, 5, 6, 8}, "
        "stream-format = (string) { adts, adif, raw }, "
        "base-profile = (string) lc, " "framed = (boolean) true")
    );

GST_DEBUG_CATEGORY_STATIC (gst_fdkaacenc_debug);
#define GST_CAT_DEFAULT gst_fdkaacenc_debug

static void gst_fdkaacenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fdkaacenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_fdkaacenc_start (GstAudioEncoder * enc);
static gboolean gst_fdkaacenc_stop (GstAudioEncoder * enc);
static gboolean gst_fdkaacenc_set_format (GstAudioEncoder * enc,
    GstAudioInfo * info);
static GstFlowReturn gst_fdkaacenc_handle_frame (GstAudioEncoder * enc,
    GstBuffer * in_buf);
static GstCaps *gst_fdkaacenc_get_caps (GstAudioEncoder * enc,
    GstCaps * filter);

G_DEFINE_TYPE (GstFdkAacEnc, gst_fdkaacenc, GST_TYPE_AUDIO_ENCODER);

static void
gst_fdkaacenc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFdkAacEnc *self = GST_FDKAACENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      self->bitrate = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static void
gst_fdkaacenc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFdkAacEnc *self = GST_FDKAACENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_int (value, self->bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
  return;
}

static gboolean
gst_fdkaacenc_start (GstAudioEncoder * enc)
{
  GstFdkAacEnc *self = GST_FDKAACENC (enc);

  GST_DEBUG_OBJECT (self, "start");

  return TRUE;
}

static gboolean
gst_fdkaacenc_stop (GstAudioEncoder * enc)
{
  GstFdkAacEnc *self = GST_FDKAACENC (enc);

  GST_DEBUG_OBJECT (self, "stop");

  if (self->enc)
    aacEncClose (&self->enc);

  return TRUE;
}

static GstCaps *
gst_fdkaacenc_get_caps (GstAudioEncoder * enc, GstCaps * filter)
{
  GstCaps *res, *caps;
  gint i;

  caps = gst_caps_new_empty ();

  for (i = 0; i < G_N_ELEMENTS (channel_layouts); i++) {
    guint64 channel_mask;
    GstCaps *tmp =
        gst_caps_make_writable (gst_pad_get_pad_template_caps
        (GST_AUDIO_ENCODER_SINK_PAD (enc)));

    if (channel_layouts[i].channels == 1) {
      gst_caps_set_simple (tmp, "channels", G_TYPE_INT,
          channel_layouts[i].channels, NULL);
    } else {
      gst_audio_channel_positions_to_mask (channel_layouts[i].positions,
          channel_layouts[i].channels, FALSE, &channel_mask);
      gst_caps_set_simple (tmp, "channels", G_TYPE_INT,
          channel_layouts[i].channels, "channel-mask", GST_TYPE_BITMASK,
          channel_mask, NULL);
    }

    gst_caps_append (caps, tmp);
  }

  res = gst_audio_encoder_proxy_getcaps (enc, caps, filter);
  gst_caps_unref (caps);

  return res;
}

static gboolean
gst_fdkaacenc_set_format (GstAudioEncoder * enc, GstAudioInfo * info)
{
  GstFdkAacEnc *self = GST_FDKAACENC (enc);
  gboolean ret = FALSE;
  GstCaps *allowed_caps;
  GstCaps *src_caps;
  AACENC_ERROR err;
  gint transmux = 0, aot = AOT_AAC_LC;
  gint mpegversion = 4;
  CHANNEL_MODE channel_mode;
  AACENC_InfoStruct enc_info = { 0 };
  gint bitrate;

  if (self->enc) {
    /* drain */
    gst_fdkaacenc_handle_frame (enc, NULL);
    aacEncClose (&self->enc);
  }

  allowed_caps = gst_pad_get_allowed_caps (GST_AUDIO_ENCODER_SRC_PAD (self));

  GST_DEBUG_OBJECT (self, "allowed caps: %" GST_PTR_FORMAT, allowed_caps);

  if (allowed_caps && gst_caps_get_size (allowed_caps) > 0) {
    GstStructure *s = gst_caps_get_structure (allowed_caps, 0);
    const gchar *str = NULL;

    if ((str = gst_structure_get_string (s, "stream-format"))) {
      if (strcmp (str, "adts") == 0) {
        GST_DEBUG_OBJECT (self, "use ADTS format for output");
        transmux = 2;
      } else if (strcmp (str, "adif") == 0) {
        GST_DEBUG_OBJECT (self, "use ADIF format for output");
        transmux = 1;
      } else if (strcmp (str, "raw") == 0) {
        GST_DEBUG_OBJECT (self, "use RAW format for output");
        transmux = 0;
      }
    }

    gst_structure_get_int (s, "mpegversion", &mpegversion);
  }
  if (allowed_caps)
    gst_caps_unref (allowed_caps);

  if ((err =
          aacEncOpen (&self->enc, 0,
              GST_AUDIO_INFO_CHANNELS (info))) != AACENC_OK) {
    GST_ERROR_OBJECT (self, "Unable to open encoder: %d\n", err);
    return FALSE;
  }

  aot = AOT_AAC_LC;

  if ((err = aacEncoder_SetParam (self->enc, AACENC_AOT, aot)) != AACENC_OK) {
    GST_ERROR_OBJECT (self, "Unable to set AOT %d: %d\n", aot, err);
    return FALSE;
  }

  if ((err = aacEncoder_SetParam (self->enc, AACENC_SAMPLERATE,
              GST_AUDIO_INFO_RATE (info))) != AACENC_OK) {
    GST_ERROR_OBJECT (self, "Unable to set sample rate %d: %d\n",
        GST_AUDIO_INFO_RATE (info), err);
    return FALSE;
  }

  if (GST_AUDIO_INFO_CHANNELS (info) == 1) {
    channel_mode = MODE_1;
    self->need_reorder = FALSE;
    self->aac_positions = NULL;
  } else {
    guint64 in_channel_mask, out_channel_mask;
    gint i;

    for (i = 0; i < G_N_ELEMENTS (channel_layouts); i++) {
      if (channel_layouts[i].channels != GST_AUDIO_INFO_CHANNELS (info))
        continue;

      gst_audio_channel_positions_to_mask (&GST_AUDIO_INFO_POSITION (info, 0),
          GST_AUDIO_INFO_CHANNELS (info), FALSE, &in_channel_mask);
      gst_audio_channel_positions_to_mask (channel_layouts[i].positions,
          channel_layouts[i].channels, FALSE, &out_channel_mask);
      if (in_channel_mask == out_channel_mask) {
        channel_mode = channel_layouts[i].mode;
        self->need_reorder =
            memcmp (channel_layouts[i].positions,
            &GST_AUDIO_INFO_POSITION (info, 0),
            GST_AUDIO_INFO_CHANNELS (info) *
            sizeof (GstAudioChannelPosition)) != 0;
        self->aac_positions = channel_layouts[i].positions;
        break;
      }
    }

    if (i == G_N_ELEMENTS (channel_layouts)) {
      GST_ERROR_OBJECT (self, "Couldn't find a valid channel layout");
      return FALSE;
    }
  }

  if ((err = aacEncoder_SetParam (self->enc, AACENC_CHANNELMODE,
              channel_mode)) != AACENC_OK) {
    GST_ERROR_OBJECT (self, "Unable to set channel mode %d: %d", channel_mode,
        err);
    return FALSE;
  }

  /* MPEG channel order */
  if ((err = aacEncoder_SetParam (self->enc, AACENC_CHANNELORDER,
              0)) != AACENC_OK) {
    GST_ERROR_OBJECT (self, "Unable to set channel order %d: %d", channel_mode,
        err);
    return FALSE;
  }

  bitrate = self->bitrate;
  /* See
   * http://wiki.hydrogenaud.io/index.php?title=Fraunhofer_FDK_AAC#Recommended_Sampling_Rate_and_Bitrate_Combinations
   */
  if (bitrate == 0) {
    if (GST_AUDIO_INFO_CHANNELS (info) == 1) {
      if (GST_AUDIO_INFO_RATE (info) < 16000) {
        bitrate = 8000;
      } else if (GST_AUDIO_INFO_RATE (info) == 16000) {
        bitrate = 16000;
      } else if (GST_AUDIO_INFO_RATE (info) < 32000) {
        bitrate = 24000;
      } else if (GST_AUDIO_INFO_RATE (info) == 32000) {
        bitrate = 32000;
      } else if (GST_AUDIO_INFO_RATE (info) <= 44100) {
        bitrate = 56000;
      } else {
        bitrate = 160000;
      }
    } else if (GST_AUDIO_INFO_CHANNELS (info) == 2) {
      if (GST_AUDIO_INFO_RATE (info) < 16000) {
        bitrate = 16000;
      } else if (GST_AUDIO_INFO_RATE (info) == 16000) {
        bitrate = 24000;
      } else if (GST_AUDIO_INFO_RATE (info) < 22050) {
        bitrate = 32000;
      } else if (GST_AUDIO_INFO_RATE (info) < 32000) {
        bitrate = 40000;
      } else if (GST_AUDIO_INFO_RATE (info) == 32000) {
        bitrate = 96000;
      } else if (GST_AUDIO_INFO_RATE (info) <= 44100) {
        bitrate = 112000;
      } else {
        bitrate = 320000;
      }
    } else {
      /* 5, 5.1 */
      if (GST_AUDIO_INFO_RATE (info) < 32000) {
        bitrate = 160000;
      } else if (GST_AUDIO_INFO_RATE (info) <= 44100) {
        bitrate = 240000;
      } else {
        bitrate = 320000;
      }
    }
  }

  if ((err = aacEncoder_SetParam (self->enc, AACENC_TRANSMUX,
              transmux)) != AACENC_OK) {
    GST_ERROR_OBJECT (self, "Unable to set transmux %d: %d", transmux, err);
    return FALSE;
  }

  if ((err = aacEncoder_SetParam (self->enc, AACENC_BITRATE,
              bitrate)) != AACENC_OK) {
    GST_ERROR_OBJECT (self, "Unable to set bitrate %d: %d", bitrate, err);
    return FALSE;
  }

  if ((err = aacEncEncode (self->enc, NULL, NULL, NULL, NULL)) != AACENC_OK) {
    GST_ERROR_OBJECT (self, "Unable to initialize encoder: %d", err);
    return FALSE;
  }

  if ((err = aacEncInfo (self->enc, &enc_info)) != AACENC_OK) {
    GST_ERROR_OBJECT (self, "Unable to get encoder info: %d", err);
    return FALSE;
  }

  gst_audio_encoder_set_frame_max (enc, 1);
  gst_audio_encoder_set_frame_samples_min (enc, enc_info.frameLength);
  gst_audio_encoder_set_frame_samples_max (enc, enc_info.frameLength);
  gst_audio_encoder_set_hard_min (enc, FALSE);
  self->outbuf_size = enc_info.maxOutBufBytes;
  self->samples_per_frame = enc_info.frameLength;

  src_caps = gst_caps_new_simple ("audio/mpeg",
      "mpegversion", G_TYPE_INT, mpegversion,
      "channels", G_TYPE_INT, GST_AUDIO_INFO_CHANNELS (info),
      "framed", G_TYPE_BOOLEAN, TRUE,
      "rate", G_TYPE_INT, GST_AUDIO_INFO_RATE (info), NULL);

  /* raw */
  if (transmux == 0) {
    GstBuffer *codec_data =
        gst_buffer_new_wrapped (g_memdup (enc_info.confBuf, enc_info.confSize),
        enc_info.confSize);
    gst_caps_set_simple (src_caps, "codec_data", GST_TYPE_BUFFER, codec_data,
        "stream-format", G_TYPE_STRING, "raw", NULL);
    gst_buffer_unref (codec_data);
  } else if (transmux == 1) {
    gst_caps_set_simple (src_caps, "stream-format", G_TYPE_STRING, "adif",
        NULL);
  } else if (transmux == 2) {
    gst_caps_set_simple (src_caps, "stream-format", G_TYPE_STRING, "adts",
        NULL);
  } else {
    g_assert_not_reached ();
  }

  gst_codec_utils_aac_caps_set_level_and_profile (src_caps, enc_info.confBuf,
      enc_info.confSize);

  ret = gst_audio_encoder_set_output_format (enc, src_caps);
  gst_caps_unref (src_caps);

  return ret;
}

static GstFlowReturn
gst_fdkaacenc_handle_frame (GstAudioEncoder * enc, GstBuffer * inbuf)
{
  GstFdkAacEnc *self = GST_FDKAACENC (enc);
  GstFlowReturn ret = GST_FLOW_OK;
  GstAudioInfo *info;
  GstMapInfo imap, omap;
  GstBuffer *outbuf;
  AACENC_BufDesc in_desc = { 0 };
  AACENC_BufDesc out_desc = { 0 };
  AACENC_InArgs in_args = { 0 };
  AACENC_OutArgs out_args = { 0 };
  gint in_id = IN_AUDIO_DATA, out_id = OUT_BITSTREAM_DATA;
  gint in_sizes, out_sizes;
  gint in_el_sizes, out_el_sizes;
  AACENC_ERROR err;

  info = gst_audio_encoder_get_audio_info (enc);

  if (!inbuf) {
    in_args.numInSamples = -1;
  } else {
    if (self->need_reorder) {
      inbuf = gst_buffer_copy (inbuf);
      gst_buffer_map (inbuf, &imap, GST_MAP_READWRITE);
      gst_audio_reorder_channels (imap.data, imap.size,
          GST_AUDIO_INFO_FORMAT (info), GST_AUDIO_INFO_CHANNELS (info),
          &GST_AUDIO_INFO_POSITION (info, 0), self->aac_positions);
    } else {
      gst_buffer_map (inbuf, &imap, GST_MAP_READ);
    }

    in_args.numInSamples = imap.size / GST_AUDIO_INFO_BPS (info);

    in_sizes = imap.size;
    in_el_sizes = 2;
    in_desc.bufferIdentifiers = &in_id;
    in_desc.numBufs = 1;
    in_desc.bufs = (void *) &imap.data;
    in_desc.bufSizes = &in_sizes;
    in_desc.bufElSizes = &in_el_sizes;
  }

  outbuf = gst_audio_encoder_allocate_output_buffer (enc, self->outbuf_size);
  if (!outbuf) {
    ret = GST_FLOW_ERROR;
    goto out;
  }

  gst_buffer_map (outbuf, &omap, GST_MAP_WRITE);
  out_sizes = omap.size;
  out_el_sizes = 1;
  out_desc.bufferIdentifiers = &out_id;
  out_desc.numBufs = 1;
  out_desc.bufs = (void *) &omap.data;
  out_desc.bufSizes = &out_sizes;
  out_desc.bufElSizes = &out_el_sizes;

  if ((err = aacEncEncode (self->enc, &in_desc, &out_desc, &in_args,
              &out_args)) != AACENC_OK) {
    if (!inbuf && err == AACENC_ENCODE_EOF)
      goto out;

    GST_ERROR_OBJECT (self, "Failed to encode data: %d", err);
    ret = GST_FLOW_ERROR;
    goto out;
  }

  if (inbuf) {
    gst_buffer_unmap (inbuf, &imap);
    if (self->need_reorder)
      gst_buffer_unref (inbuf);
    inbuf = NULL;
  }

  if (!out_args.numOutBytes)
    goto out;

  gst_buffer_unmap (outbuf, &omap);
  gst_buffer_set_size (outbuf, out_args.numOutBytes);

  ret = gst_audio_encoder_finish_frame (enc, outbuf, self->samples_per_frame);
  outbuf = NULL;

out:
  if (outbuf) {
    gst_buffer_unmap (outbuf, &omap);
    gst_buffer_unref (outbuf);
  }
  if (inbuf) {
    gst_buffer_unmap (inbuf, &imap);
    if (self->need_reorder)
      gst_buffer_unref (inbuf);
  }

  return ret;
}

static void
gst_fdkaacenc_init (GstFdkAacEnc * self)
{
  self->bitrate = DEFAULT_BITRATE;
  self->enc = NULL;

  gst_audio_encoder_set_drainable (GST_AUDIO_ENCODER (self), TRUE);
}

static void
gst_fdkaacenc_class_init (GstFdkAacEncClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioEncoderClass *base_class = GST_AUDIO_ENCODER_CLASS (klass);

  object_class->set_property = GST_DEBUG_FUNCPTR (gst_fdkaacenc_set_property);
  object_class->get_property = GST_DEBUG_FUNCPTR (gst_fdkaacenc_get_property);

  base_class->start = GST_DEBUG_FUNCPTR (gst_fdkaacenc_start);
  base_class->stop = GST_DEBUG_FUNCPTR (gst_fdkaacenc_stop);
  base_class->set_format = GST_DEBUG_FUNCPTR (gst_fdkaacenc_set_format);
  base_class->getcaps = GST_DEBUG_FUNCPTR (gst_fdkaacenc_get_caps);
  base_class->handle_frame = GST_DEBUG_FUNCPTR (gst_fdkaacenc_handle_frame);

  g_object_class_install_property (object_class, PROP_BITRATE,
      g_param_spec_int ("bitrate",
          "Bitrate",
          "Target Audio Bitrate (0 = fixed value based on "
          " sample rate and channel count)",
          0, G_MAXINT, DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_static_pad_template (element_class, &sink_template);
  gst_element_class_add_static_pad_template (element_class, &src_template);

  gst_element_class_set_static_metadata (element_class, "FDK AAC audio encoder",
      "Codec/Encoder/Audio", "FDK AAC audio encoder",
      "Sebastian Dröge <sebastian@centricular.com>");

  GST_DEBUG_CATEGORY_INIT (gst_fdkaacenc_debug, "fdkaacenc", 0,
      "fdkaac encoder");
}
