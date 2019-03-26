/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
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

#include "gstomxaacenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_aac_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_aac_enc_debug_category

/* prototypes */
static void gst_omx_aac_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_aac_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_omx_aac_enc_set_format (GstOMXAudioEnc * enc,
    GstOMXPort * port, GstAudioInfo * info);
static GstCaps *gst_omx_aac_enc_get_caps (GstOMXAudioEnc * enc,
    GstOMXPort * port, GstAudioInfo * info);
static guint gst_omx_aac_enc_get_num_samples (GstOMXAudioEnc * enc,
    GstOMXPort * port, GstAudioInfo * info, GstOMXBuffer * buf);

enum
{
  PROP_0,
  PROP_BITRATE,
  PROP_AAC_TOOLS,
  PROP_AAC_ERROR_RESILIENCE_TOOLS
};

#define DEFAULT_BITRATE (128000)
#define DEFAULT_AAC_TOOLS (OMX_AUDIO_AACToolMS | OMX_AUDIO_AACToolIS | OMX_AUDIO_AACToolTNS | OMX_AUDIO_AACToolPNS | OMX_AUDIO_AACToolLTP)
#define DEFAULT_AAC_ER_TOOLS (OMX_AUDIO_AACERNone)

#define GST_TYPE_OMX_AAC_TOOLS (gst_omx_aac_tools_get_type ())
static GType
gst_omx_aac_tools_get_type (void)
{
  static gsize id = 0;
  static const GFlagsValue values[] = {
    {OMX_AUDIO_AACToolMS, "Mid/side joint coding", "ms"},
    {OMX_AUDIO_AACToolIS, "Intensity stereo", "is"},
    {OMX_AUDIO_AACToolTNS, "Temporal noise shaping", "tns"},
    {OMX_AUDIO_AACToolPNS, "Perceptual noise substitution", "pns"},
    {OMX_AUDIO_AACToolLTP, "Long term prediction", "ltp"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_flags_register_static ("GstOMXAACTools", values);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

#define GST_TYPE_OMX_AAC_ER_TOOLS (gst_omx_aac_er_tools_get_type ())
static GType
gst_omx_aac_er_tools_get_type (void)
{
  static gsize id = 0;
  static const GFlagsValue values[] = {
    {OMX_AUDIO_AACERVCB11, "Virtual code books", "vcb11"},
    {OMX_AUDIO_AACERRVLC, "Reversible variable length coding", "rvlc"},
    {OMX_AUDIO_AACERHCR, "Huffman codeword reordering", "hcr"},
    {0, NULL, NULL}
  };

  if (g_once_init_enter (&id)) {
    GType tmp = g_flags_register_static ("GstOMXAACERTools", values);
    g_once_init_leave (&id, tmp);
  }

  return (GType) id;
}

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_aac_enc_debug_category, "omxaacenc", 0, \
      "debug category for gst-omx audio encoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXAACEnc, gst_omx_aac_enc,
    GST_TYPE_OMX_AUDIO_ENC, DEBUG_INIT);


static void
gst_omx_aac_enc_class_init (GstOMXAACEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXAudioEncClass *audioenc_class = GST_OMX_AUDIO_ENC_CLASS (klass);

  gobject_class->set_property = gst_omx_aac_enc_set_property;
  gobject_class->get_property = gst_omx_aac_enc_get_property;

  g_object_class_install_property (gobject_class, PROP_BITRATE,
      g_param_spec_uint ("bitrate", "Bitrate",
          "Bitrate",
          0, G_MAXUINT, DEFAULT_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_AAC_TOOLS,
      g_param_spec_flags ("aac-tools", "AAC Tools",
          "Allowed AAC tools",
          GST_TYPE_OMX_AAC_TOOLS,
          DEFAULT_AAC_TOOLS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class,
      PROP_AAC_ERROR_RESILIENCE_TOOLS,
      g_param_spec_flags ("aac-error-resilience-tools",
          "AAC Error Resilience Tools", "Allowed AAC error resilience tools",
          GST_TYPE_OMX_AAC_ER_TOOLS, DEFAULT_AAC_ER_TOOLS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  audioenc_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_aac_enc_set_format);
  audioenc_class->get_caps = GST_DEBUG_FUNCPTR (gst_omx_aac_enc_get_caps);
  audioenc_class->get_num_samples =
      GST_DEBUG_FUNCPTR (gst_omx_aac_enc_get_num_samples);

  audioenc_class->cdata.default_src_template_caps = "audio/mpeg, "
      "mpegversion=(int){2, 4}, "
      "stream-format=(string){raw, adts, adif, loas, latm}";


  gst_element_class_set_static_metadata (element_class,
      "OpenMAX AAC Audio Encoder",
      "Codec/Encoder/Audio/Hardware",
      "Encode AAC audio streams",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_omx_set_default_role (&audioenc_class->cdata, "audio_encoder.aac");
}

static void
gst_omx_aac_enc_init (GstOMXAACEnc * self)
{
  self->bitrate = DEFAULT_BITRATE;
  self->aac_tools = DEFAULT_AAC_TOOLS;
  self->aac_er_tools = DEFAULT_AAC_ER_TOOLS;
}

static void
gst_omx_aac_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXAACEnc *self = GST_OMX_AAC_ENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      self->bitrate = g_value_get_uint (value);
      break;
    case PROP_AAC_TOOLS:
      self->aac_tools = g_value_get_flags (value);
      break;
    case PROP_AAC_ERROR_RESILIENCE_TOOLS:
      self->aac_er_tools = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_aac_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOMXAACEnc *self = GST_OMX_AAC_ENC (object);

  switch (prop_id) {
    case PROP_BITRATE:
      g_value_set_uint (value, self->bitrate);
      break;
    case PROP_AAC_TOOLS:
      g_value_set_flags (value, self->aac_tools);
      break;
    case PROP_AAC_ERROR_RESILIENCE_TOOLS:
      g_value_set_flags (value, self->aac_er_tools);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_omx_aac_enc_set_format (GstOMXAudioEnc * enc, GstOMXPort * port,
    GstAudioInfo * info)
{
  GstOMXAACEnc *self = GST_OMX_AAC_ENC (enc);
  OMX_AUDIO_PARAM_AACPROFILETYPE aac_profile;
  GstCaps *peercaps;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&aac_profile);
  aac_profile.nPortIndex = enc->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (enc->enc, OMX_IndexParamAudioAac,
      &aac_profile);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get AAC parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  peercaps = gst_pad_peer_query_caps (GST_AUDIO_ENCODER_SRC_PAD (self),
      gst_pad_get_pad_template_caps (GST_AUDIO_ENCODER_SRC_PAD (self)));
  if (peercaps) {
    GstStructure *s;
    gint mpegversion = 0;
    const gchar *profile_string, *stream_format_string;

    if (gst_caps_is_empty (peercaps)) {
      gst_caps_unref (peercaps);
      GST_ERROR_OBJECT (self, "Empty caps");
      return FALSE;
    }

    s = gst_caps_get_structure (peercaps, 0);

    if (gst_structure_get_int (s, "mpegversion", &mpegversion)) {
      profile_string =
          gst_structure_get_string (s,
          ((mpegversion == 2) ? "profile" : "base-profile"));

      if (profile_string) {
        if (g_str_equal (profile_string, "main")) {
          aac_profile.eAACProfile = OMX_AUDIO_AACObjectMain;
        } else if (g_str_equal (profile_string, "lc")) {
          aac_profile.eAACProfile = OMX_AUDIO_AACObjectLC;
        } else if (g_str_equal (profile_string, "ssr")) {
          aac_profile.eAACProfile = OMX_AUDIO_AACObjectSSR;
        } else if (g_str_equal (profile_string, "ltp")) {
          aac_profile.eAACProfile = OMX_AUDIO_AACObjectLTP;
        } else {
          GST_ERROR_OBJECT (self, "Unsupported profile '%s'", profile_string);
          gst_caps_unref (peercaps);
          return FALSE;
        }
      }
    }

    stream_format_string = gst_structure_get_string (s, "stream-format");
    if (stream_format_string) {
      if (g_str_equal (stream_format_string, "raw")) {
        aac_profile.eAACStreamFormat = OMX_AUDIO_AACStreamFormatRAW;
      } else if (g_str_equal (stream_format_string, "adts")) {
        if (mpegversion == 2) {
          aac_profile.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP2ADTS;
        } else {
          aac_profile.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4ADTS;
        }
      } else if (g_str_equal (stream_format_string, "loas")) {
        aac_profile.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4LOAS;
      } else if (g_str_equal (stream_format_string, "latm")) {
        aac_profile.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4LATM;
      } else if (g_str_equal (stream_format_string, "adif")) {
        aac_profile.eAACStreamFormat = OMX_AUDIO_AACStreamFormatADIF;
      } else {
        GST_ERROR_OBJECT (self, "Unsupported stream-format '%s'",
            stream_format_string);
        gst_caps_unref (peercaps);
        return FALSE;
      }
    }

    gst_caps_unref (peercaps);

    aac_profile.nSampleRate = info->rate;
    aac_profile.nChannels = info->channels;
  }

  aac_profile.nAACtools = self->aac_tools;
  aac_profile.nAACERtools = self->aac_er_tools;

  aac_profile.nBitRate = self->bitrate;

  err =
      gst_omx_component_set_parameter (enc->enc, OMX_IndexParamAudioAac,
      &aac_profile);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting AAC parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

typedef enum adts_sample_index__
{
  ADTS_SAMPLE_INDEX_96000 = 0x0,
  ADTS_SAMPLE_INDEX_88200,
  ADTS_SAMPLE_INDEX_64000,
  ADTS_SAMPLE_INDEX_48000,
  ADTS_SAMPLE_INDEX_44100,
  ADTS_SAMPLE_INDEX_32000,
  ADTS_SAMPLE_INDEX_24000,
  ADTS_SAMPLE_INDEX_22050,
  ADTS_SAMPLE_INDEX_16000,
  ADTS_SAMPLE_INDEX_12000,
  ADTS_SAMPLE_INDEX_11025,
  ADTS_SAMPLE_INDEX_8000,
  ADTS_SAMPLE_INDEX_7350,
  ADTS_SAMPLE_INDEX_MAX
} adts_sample_index;

static adts_sample_index
map_adts_sample_index (guint32 srate)
{
  adts_sample_index ret;

  switch (srate) {

    case 96000:
      ret = ADTS_SAMPLE_INDEX_96000;
      break;
    case 88200:
      ret = ADTS_SAMPLE_INDEX_88200;
      break;
    case 64000:
      ret = ADTS_SAMPLE_INDEX_64000;
      break;
    case 48000:
      ret = ADTS_SAMPLE_INDEX_48000;
      break;
    case 44100:
      ret = ADTS_SAMPLE_INDEX_44100;
      break;
    case 32000:
      ret = ADTS_SAMPLE_INDEX_32000;
      break;
    case 24000:
      ret = ADTS_SAMPLE_INDEX_24000;
      break;
    case 22050:
      ret = ADTS_SAMPLE_INDEX_22050;
      break;
    case 16000:
      ret = ADTS_SAMPLE_INDEX_16000;
      break;
    case 12000:
      ret = ADTS_SAMPLE_INDEX_12000;
      break;
    case 11025:
      ret = ADTS_SAMPLE_INDEX_11025;
      break;
    case 8000:
      ret = ADTS_SAMPLE_INDEX_8000;
      break;
    case 7350:
      ret = ADTS_SAMPLE_INDEX_7350;
      break;
    default:
      ret = ADTS_SAMPLE_INDEX_44100;
      break;
  }
  return ret;
}

static GstCaps *
gst_omx_aac_enc_get_caps (GstOMXAudioEnc * enc, GstOMXPort * port,
    GstAudioInfo * info)
{
  GstCaps *caps;
  OMX_ERRORTYPE err;
  OMX_AUDIO_PARAM_AACPROFILETYPE aac_profile;
  gint mpegversion = 4;
  const gchar *stream_format = NULL, *profile = NULL;

  GST_OMX_INIT_STRUCT (&aac_profile);
  aac_profile.nPortIndex = enc->enc_out_port->index;

  err =
      gst_omx_component_get_parameter (enc->enc, OMX_IndexParamAudioAac,
      &aac_profile);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (enc,
        "Failed to get AAC parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return NULL;
  }

  switch (aac_profile.eAACProfile) {
    case OMX_AUDIO_AACObjectMain:
      profile = "main";
      break;
    case OMX_AUDIO_AACObjectLC:
      profile = "lc";
      break;
    case OMX_AUDIO_AACObjectSSR:
      profile = "ssr";
      break;
    case OMX_AUDIO_AACObjectLTP:
      profile = "ltp";
      break;
    case OMX_AUDIO_AACObjectHE:
    case OMX_AUDIO_AACObjectScalable:
    case OMX_AUDIO_AACObjectERLC:
    case OMX_AUDIO_AACObjectLD:
    case OMX_AUDIO_AACObjectHE_PS:
    default:
      GST_ERROR_OBJECT (enc, "Unsupported profile %d", aac_profile.eAACProfile);
      break;
  }

  switch (aac_profile.eAACStreamFormat) {
    case OMX_AUDIO_AACStreamFormatMP2ADTS:
      mpegversion = 2;
      stream_format = "adts";
      break;
    case OMX_AUDIO_AACStreamFormatMP4ADTS:
      mpegversion = 4;
      stream_format = "adts";
      break;
    case OMX_AUDIO_AACStreamFormatMP4LOAS:
      mpegversion = 4;
      stream_format = "loas";
      break;
    case OMX_AUDIO_AACStreamFormatMP4LATM:
      mpegversion = 4;
      stream_format = "latm";
      break;
    case OMX_AUDIO_AACStreamFormatADIF:
      mpegversion = 4;
      stream_format = "adif";
      break;
    case OMX_AUDIO_AACStreamFormatRAW:
      mpegversion = 4;
      stream_format = "raw";
      break;
    case OMX_AUDIO_AACStreamFormatMP4FF:
    default:
      GST_ERROR_OBJECT (enc, "Unsupported stream-format %u",
          aac_profile.eAACStreamFormat);
      break;
  }

  caps = gst_caps_new_empty_simple ("audio/mpeg");

  if (mpegversion != 0)
    gst_caps_set_simple (caps, "mpegversion", G_TYPE_INT, mpegversion,
        "stream-format", G_TYPE_STRING, stream_format, NULL);
  if (profile != NULL && (mpegversion == 2 || mpegversion == 4))
    gst_caps_set_simple (caps, "profile", G_TYPE_STRING, profile, NULL);
  if (profile != NULL && mpegversion == 4)
    gst_caps_set_simple (caps, "base-profile", G_TYPE_STRING, profile, NULL);
  if (aac_profile.nChannels != 0)
    gst_caps_set_simple (caps, "channels", G_TYPE_INT, aac_profile.nChannels,
        NULL);
  if (aac_profile.nSampleRate != 0)
    gst_caps_set_simple (caps, "rate", G_TYPE_INT, aac_profile.nSampleRate,
        NULL);

  if (aac_profile.eAACStreamFormat == OMX_AUDIO_AACStreamFormatRAW) {
    GstBuffer *codec_data;
    adts_sample_index sr_idx;
    GstMapInfo map = GST_MAP_INFO_INIT;

    codec_data = gst_buffer_new_and_alloc (2);
    gst_buffer_map (codec_data, &map, GST_MAP_WRITE);
    sr_idx = map_adts_sample_index (aac_profile.nSampleRate);
    map.data[0] = ((aac_profile.eAACProfile & 0x1F) << 3) |
        ((sr_idx & 0xE) >> 1);
    map.data[1] = ((sr_idx & 0x1) << 7) | ((aac_profile.nChannels & 0xF) << 3);
    gst_buffer_unmap (codec_data, &map);

    GST_DEBUG_OBJECT (enc, "setting new codec_data");
    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data, NULL);

    gst_buffer_unref (codec_data);
  }
  return caps;

}

static guint
gst_omx_aac_enc_get_num_samples (GstOMXAudioEnc * enc, GstOMXPort * port,
    GstAudioInfo * info, GstOMXBuffer * buf)
{
  /* FIXME: Depends on the profile at least */
  return 1024;
}
