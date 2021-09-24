/*
 * Copyright (C) 2017
 *   Author: Julien Isorce <julien.isorce@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstomxmp3enc.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_mp3_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_mp3_enc_debug_category

/* prototypes */
static void gst_omx_mp3_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_mp3_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_omx_mp3_enc_set_format (GstOMXAudioEnc * enc,
    GstOMXPort * port, GstAudioInfo * info);
static GstCaps *gst_omx_mp3_enc_get_caps (GstOMXAudioEnc * enc,
    GstOMXPort * port, GstAudioInfo * info);
static guint gst_omx_mp3_enc_get_num_samples (GstOMXAudioEnc * enc,
    GstOMXPort * port, GstAudioInfo * info, GstOMXBuffer * buf);

enum
{
  PROP_0,
  PROP_BITRATE
};

#define DEFAULT_BITRATE (128)

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_mp3_enc_debug_category, "omxmp3enc", 0, \
      "debug category for gst-omx audio encoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXMP3Enc, gst_omx_mp3_enc,
    GST_TYPE_OMX_AUDIO_ENC, DEBUG_INIT);


static void
gst_omx_mp3_enc_class_init (GstOMXMP3EncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXAudioEncClass *audioenc_class = GST_OMX_AUDIO_ENC_CLASS (klass);

  gobject_class->set_property = gst_omx_mp3_enc_set_property;
  gobject_class->get_property = gst_omx_mp3_enc_get_property;

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate (kb/s)",
          "Bitrate in kbit/sec",
          0, G_MAXUINT, DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  audioenc_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_mp3_enc_set_format);
  audioenc_class->get_caps = GST_DEBUG_FUNCPTR (gst_omx_mp3_enc_get_caps);
  audioenc_class->get_num_samples =
      GST_DEBUG_FUNCPTR (gst_omx_mp3_enc_get_num_samples);

  audioenc_class->cdata.default_src_template_caps = "audio/mpeg, "
      "mpegversion=(int)1, "
      "layer=(int)3, "
      "mpegaudioversion=(int)[1,3], "
      "rate=(int)[8000,48000], " "channels=(int)[1,2]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX MP3 Audio Encoder",
      "Codec/Encoder/Audio/Hardware",
      "Encode AAC audio streams", "Julien Isorce <julien.isorce@gmail.com>");

  gst_omx_set_default_role (&audioenc_class->cdata, "audio_encoder.mp3");
}

static void
gst_omx_mp3_enc_init (GstOMXMP3Enc * self)
{
  self->mpegaudioversion = 1;
  self->bitrate = DEFAULT_BITRATE;
}

static void
gst_omx_mp3_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXMP3Enc *self = GST_OMX_MP3_ENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      self->bitrate = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_mp3_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOMXMP3Enc *self = GST_OMX_MP3_ENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_omx_mp3_enc_set_format (GstOMXAudioEnc * enc, GstOMXPort * port,
    GstAudioInfo * info)
{
  GstOMXMP3Enc *self = GST_OMX_MP3_ENC (enc);
  OMX_AUDIO_PARAM_MP3TYPE mp3_param;
  GstCaps *peercaps;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&mp3_param);
  mp3_param.nPortIndex = enc->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (enc->enc, OMX_IndexParamAudioMp3,
      &mp3_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get MP# parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  peercaps = gst_pad_peer_query_caps (GST_AUDIO_ENCODER_SRC_PAD (self),
      gst_pad_get_pad_template_caps (GST_AUDIO_ENCODER_SRC_PAD (self)));
  if (peercaps) {
    GstStructure *s;
    gint mpegaudioversion = 0;

    if (gst_caps_is_empty (peercaps)) {
      gst_caps_unref (peercaps);
      GST_ERROR_OBJECT (self, "Empty caps");
      return FALSE;
    }

    s = gst_caps_get_structure (peercaps, 0);

    if (gst_structure_get_int (s, "mpegaudioversion", &mpegaudioversion)) {
      switch (mpegaudioversion) {
        case 1:
          mp3_param.eFormat = OMX_AUDIO_MP3StreamFormatMP1Layer3;
          break;
        case 2:
          mp3_param.eFormat = OMX_AUDIO_MP3StreamFormatMP2Layer3;
          break;
        case 3:
          mp3_param.eFormat = OMX_AUDIO_MP3StreamFormatMP2_5Layer3;
          break;
        default:
          GST_ERROR_OBJECT (self, "Unsupported mpegaudioversion '%d'",
              mpegaudioversion);
          gst_caps_unref (peercaps);
          return FALSE;
      }
      self->mpegaudioversion = mpegaudioversion;
    }

    gst_caps_unref (peercaps);

    mp3_param.nSampleRate = info->rate;
    mp3_param.nChannels = info->channels;

    mp3_param.eChannelMode =
        info->channels ==
        1 ? OMX_AUDIO_ChannelModeMono : OMX_AUDIO_ChannelModeStereo;
  }

  mp3_param.nBitRate = self->bitrate;

  err =
      gst_omx_component_set_parameter (enc->enc, OMX_IndexParamAudioMp3,
      &mp3_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting MP3 parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static GstCaps *
gst_omx_mp3_enc_get_caps (GstOMXAudioEnc * enc, GstOMXPort * port,
    GstAudioInfo * info)
{
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_AUDIO_PARAM_MP3TYPE mp3_param;
  gint mpegaudioversion = 0;

  GST_OMX_INIT_STRUCT (&mp3_param);
  mp3_param.nPortIndex = enc->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (enc->enc, OMX_IndexParamAudioMp3,
      &mp3_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (enc,
        "Failed to get MP3 parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return NULL;
  }

  switch (mp3_param.eFormat) {
    case OMX_AUDIO_MP3StreamFormatMP1Layer3:
      mpegaudioversion = 1;
      break;
    case OMX_AUDIO_MP3StreamFormatMP2Layer3:
      mpegaudioversion = 2;
      break;
    case OMX_AUDIO_MP3StreamFormatMP2_5Layer3:
      mpegaudioversion = 3;
      break;
    default:
      GST_ERROR_OBJECT (enc, "Unsupported mpegaudioversion %d",
          mp3_param.eFormat);
      break;
  }

  caps =
      gst_caps_new_simple ("audio/mpeg", "mpegversion", G_TYPE_INT, 1, "layer",
      G_TYPE_INT, 3, NULL);

  if (mpegaudioversion != 0)
    gst_caps_set_simple (caps, "mpegaudioversion", G_TYPE_INT, mpegaudioversion,
        NULL);
  if (mp3_param.nChannels != 0)
    gst_caps_set_simple (caps, "channels", G_TYPE_INT, mp3_param.nChannels,
        NULL);
  if (mp3_param.nSampleRate != 0)
    gst_caps_set_simple (caps, "rate", G_TYPE_INT, mp3_param.nSampleRate, NULL);

  return caps;

}

static guint
gst_omx_mp3_enc_get_num_samples (GstOMXAudioEnc * enc, GstOMXPort * port,
    GstAudioInfo * info, GstOMXBuffer * buf)
{
  GstOMXMP3Enc *self = GST_OMX_MP3_ENC (enc);
  return (self->mpegaudioversion == 1) ? 1152 : 576;
}
