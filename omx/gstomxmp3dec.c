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

#include "gstomxmp3dec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_mp3_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_mp3_dec_debug_category

/* prototypes */
static gboolean gst_omx_mp3_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_mp3_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_mp3_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);
static gboolean gst_omx_mp3_dec_get_channel_positions (GstOMXAudioDec * dec,
    GstOMXPort * port, GstAudioChannelPosition position[OMX_AUDIO_MAXCHANNELS]);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_mp3_dec_debug_category, "omxmp3dec", 0, \
      "debug category for gst-omx mp3 audio decoder");

G_DEFINE_TYPE_WITH_CODE (GstOMXMP3Dec, gst_omx_mp3_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);


static void
gst_omx_mp3_dec_class_init (GstOMXMP3DecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_mp3_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_mp3_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_mp3_dec_get_samples_per_frame);
  audiodec_class->get_channel_positions =
      GST_DEBUG_FUNCPTR (gst_omx_mp3_dec_get_channel_positions);

  audiodec_class->cdata.default_sink_template_caps = "audio/mpeg, "
      "mpegversion=(int)1, "
      "layer=(int)3, "
      "mpegaudioversion=(int)[1,3], "
      "rate=(int)[8000,48000], "
      "channels=(int)[1,2], " "parsed=(boolean) true";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX MP3 Audio Decoder",
      "Codec/Decoder/Audio/Hardware",
      "Decode MP3 audio streams",
      "Sebastian Dröge <sebastian@centricular.com>");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.mp3");
}

static void
gst_omx_mp3_dec_init (GstOMXMP3Dec * self)
{
  self->spf = -1;
}

static gboolean
gst_omx_mp3_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXMP3Dec *self = GST_OMX_MP3_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_MP3TYPE mp3_param;
  OMX_ERRORTYPE err;
  GstStructure *s;
  gint rate, channels, layer, mpegaudioversion;

  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingMP3;
  err = gst_omx_port_update_port_definition (port, &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set MP3 format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&mp3_param);
  mp3_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioMp3,
      &mp3_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get MP3 parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "mpegaudioversion", &mpegaudioversion) ||
      !gst_structure_get_int (s, "layer", &layer) ||
      !gst_structure_get_int (s, "rate", &rate) ||
      !gst_structure_get_int (s, "channels", &channels)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  self->spf = (mpegaudioversion == 1 ? 1152 : 576);

  mp3_param.nChannels = channels;
  mp3_param.nBitRate = 0;       /* unknown */
  mp3_param.nSampleRate = rate;
  mp3_param.nAudioBandWidth = 0;        /* decoder decision */
  mp3_param.eChannelMode = 0;   /* FIXME */
  if (mpegaudioversion == 1)
    mp3_param.eFormat = OMX_AUDIO_MP3StreamFormatMP1Layer3;
  else if (mpegaudioversion == 2)
    mp3_param.eFormat = OMX_AUDIO_MP3StreamFormatMP2Layer3;
  else
    mp3_param.eFormat = OMX_AUDIO_MP3StreamFormatMP2_5Layer3;

  err =
      gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioMp3,
      &mp3_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting MP3 parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_mp3_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXMP3Dec *self = GST_OMX_MP3_DEC (dec);
  OMX_AUDIO_PARAM_MP3TYPE mp3_param;
  OMX_ERRORTYPE err;
  GstStructure *s;
  gint rate, channels, layer, mpegaudioversion;

  GST_OMX_INIT_STRUCT (&mp3_param);
  mp3_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioMp3,
      &mp3_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get MP3 parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "mpegaudioversion", &mpegaudioversion) ||
      !gst_structure_get_int (s, "layer", &layer) ||
      !gst_structure_get_int (s, "rate", &rate) ||
      !gst_structure_get_int (s, "channels", &channels)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  if (mp3_param.nChannels != channels)
    return TRUE;

  if (mp3_param.nSampleRate != rate)
    return TRUE;

  if (mpegaudioversion == 1
      && mp3_param.eFormat != OMX_AUDIO_MP3StreamFormatMP1Layer3)
    return TRUE;
  if (mpegaudioversion == 2
      && mp3_param.eFormat != OMX_AUDIO_MP3StreamFormatMP2Layer3)
    return TRUE;
  if (mpegaudioversion == 3
      && mp3_param.eFormat != OMX_AUDIO_MP3StreamFormatMP2_5Layer3)
    return TRUE;

  return FALSE;
}

static gint
gst_omx_mp3_dec_get_samples_per_frame (GstOMXAudioDec * dec, GstOMXPort * port)
{
  return GST_OMX_MP3_DEC (dec)->spf;
}

static gboolean
gst_omx_mp3_dec_get_channel_positions (GstOMXAudioDec * dec,
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

  switch (pcm_param.nChannels) {
    case 1:
      position[0] = GST_AUDIO_CHANNEL_POSITION_MONO;
      break;
    case 2:
      position[0] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
      position[1] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
      break;
    default:
      return FALSE;
  }

  return TRUE;
}
