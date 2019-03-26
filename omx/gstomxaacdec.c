/*
 * Copyright (C) 2014, Sebastian Dröge <sebastian@centricular.com>
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

#include "gstomxaacdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_aac_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_aac_dec_debug_category

/* prototypes */
static gboolean gst_omx_aac_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_aac_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_aac_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);
static gboolean gst_omx_aac_dec_get_channel_positions (GstOMXAudioDec * dec,
    GstOMXPort * port, GstAudioChannelPosition position[OMX_AUDIO_MAXCHANNELS]);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_aac_dec_debug_category, "omxaacdec", 0, \
      "debug category for gst-omx aac audio decoder");

G_DEFINE_TYPE_WITH_CODE (GstOMXAACDec, gst_omx_aac_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);

static void
gst_omx_aac_dec_class_init (GstOMXAACDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_aac_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_aac_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_aac_dec_get_samples_per_frame);
  audiodec_class->get_channel_positions =
      GST_DEBUG_FUNCPTR (gst_omx_aac_dec_get_channel_positions);

  audiodec_class->cdata.default_sink_template_caps = "audio/mpeg, "
      "mpegversion=(int){2, 4}, "
      "stream-format=(string) { raw, adts, adif, loas }, "
      "rate=(int)[8000,48000], "
      "channels=(int)[1,9], " "framed=(boolean) true";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX AAC Audio Decoder",
      "Codec/Decoder/Audio/Hardware",
      "Decode AAC audio streams",
      "Sebastian Dröge <sebastian@centricular.com>");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.aac");
}

static void
gst_omx_aac_dec_init (GstOMXAACDec * self)
{
  /* FIXME: Other values exist too! */
  self->spf = 1024;
}

static gboolean
gst_omx_aac_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXAACDec *self = GST_OMX_AAC_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_AACPROFILETYPE aac_param;
  OMX_ERRORTYPE err;
  GstStructure *s;
  gint rate, channels, mpegversion;
  const gchar *stream_format;

  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingAAC;
  err = gst_omx_port_update_port_definition (port, &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set AAC format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&aac_param);
  aac_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioAac,
      &aac_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get AAC parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "mpegversion", &mpegversion) ||
      !gst_structure_get_int (s, "rate", &rate) ||
      !gst_structure_get_int (s, "channels", &channels)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  stream_format = gst_structure_get_string (s, "stream-format");
  if (!stream_format) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  aac_param.nChannels = channels;
  aac_param.nSampleRate = rate;
  aac_param.nBitRate = 0;       /* unknown */
  aac_param.nAudioBandWidth = 0;        /* decoder decision */
  aac_param.eChannelMode = 0;   /* FIXME */
  if (mpegversion == 2)
    aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP2ADTS;
  else if (strcmp (stream_format, "adts") == 0)
    aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4ADTS;
  else if (strcmp (stream_format, "loas") == 0)
    aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4LOAS;
  else if (strcmp (stream_format, "adif") == 0)
    aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatADIF;
  else if (strcmp (stream_format, "raw") == 0)
    aac_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatRAW;
  else {
    GST_ERROR_OBJECT (self, "Unexpected format: %s", stream_format);
    return FALSE;
  }

  err =
      gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioAac,
      &aac_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting AAC parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_aac_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXAACDec *self = GST_OMX_AAC_DEC (dec);
  OMX_AUDIO_PARAM_AACPROFILETYPE aac_param;
  OMX_ERRORTYPE err;
  GstStructure *s;
  gint rate, channels, mpegversion;
  const gchar *stream_format;

  GST_OMX_INIT_STRUCT (&aac_param);
  aac_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioAac,
      &aac_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get AAC parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "mpegversion", &mpegversion) ||
      !gst_structure_get_int (s, "rate", &rate) ||
      !gst_structure_get_int (s, "channels", &channels)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  stream_format = gst_structure_get_string (s, "stream-format");
  if (!stream_format) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  if (aac_param.nChannels != channels)
    return TRUE;

  if (aac_param.nSampleRate != rate)
    return TRUE;

  if (mpegversion == 2
      && aac_param.eAACStreamFormat != OMX_AUDIO_AACStreamFormatMP2ADTS)
    return TRUE;
  if (aac_param.eAACStreamFormat == OMX_AUDIO_AACStreamFormatMP4ADTS &&
      strcmp (stream_format, "adts") != 0)
    return TRUE;
  if (aac_param.eAACStreamFormat == OMX_AUDIO_AACStreamFormatMP4LOAS &&
      strcmp (stream_format, "loas") != 0)
    return TRUE;
  if (aac_param.eAACStreamFormat == OMX_AUDIO_AACStreamFormatADIF &&
      strcmp (stream_format, "adif") != 0)
    return TRUE;
  if (aac_param.eAACStreamFormat == OMX_AUDIO_AACStreamFormatRAW &&
      strcmp (stream_format, "raw") != 0)
    return TRUE;

  return FALSE;
}

static gint
gst_omx_aac_dec_get_samples_per_frame (GstOMXAudioDec * dec, GstOMXPort * port)
{
  return GST_OMX_AAC_DEC (dec)->spf;
}

static gboolean
gst_omx_aac_dec_get_channel_positions (GstOMXAudioDec * dec,
    GstOMXPort * port, GstAudioChannelPosition position[OMX_AUDIO_MAXCHANNELS])
{
  OMX_AUDIO_PARAM_PCMMODETYPE pcm_param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&pcm_param);
  pcm_param.nPortIndex = port->index;
  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioPcm,
      &pcm_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (dec, "Failed to get PCM parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  /* FIXME: Rather arbitrary values here, based on what we do in gstfaac.c */
  switch (pcm_param.nChannels) {
    case 1:
      position[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
      break;
    case 2:
      position[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      position[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      break;
    case 3:
      position[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
      position[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      position[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      break;
    case 4:
      position[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
      position[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      position[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      position[3] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
      break;
    case 5:
      position[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
      position[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      position[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      position[3] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
      position[4] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      break;
    case 6:
      position[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
      position[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      position[2] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      position[3] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
      position[4] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
      position[5] = GST_AUDIO_CHANNEL_POSITION_LFE1;
      break;
    default:
      return FALSE;
  }

  return TRUE;
}
