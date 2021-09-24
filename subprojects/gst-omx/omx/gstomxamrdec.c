/*
 * Copyright (C) 2014, Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2014, LG Electronics, Inc. <jun.ji@lge.com>
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

#include "gstomxamrdec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_amr_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_amr_dec_debug_category

/* prototypes */
static gboolean gst_omx_amr_dec_set_format (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gboolean gst_omx_amr_dec_is_format_change (GstOMXAudioDec * dec,
    GstOMXPort * port, GstCaps * caps);
static gint gst_omx_amr_dec_get_samples_per_frame (GstOMXAudioDec * dec,
    GstOMXPort * port);
static gboolean gst_omx_amr_dec_get_channel_positions (GstOMXAudioDec * dec,
    GstOMXPort * port, GstAudioChannelPosition position[OMX_AUDIO_MAXCHANNELS]);

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_amr_dec_debug_category, "omxamrdec", 0, \
      "debug category for gst-omx amr audio decoder");

G_DEFINE_TYPE_WITH_CODE (GstOMXAMRDec, gst_omx_amr_dec,
    GST_TYPE_OMX_AUDIO_DEC, DEBUG_INIT);

static void
gst_omx_amr_dec_class_init (GstOMXAMRDecClass * klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstOMXAudioDecClass *audiodec_class = GST_OMX_AUDIO_DEC_CLASS (klass);

  audiodec_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_amr_dec_set_format);
  audiodec_class->is_format_change =
      GST_DEBUG_FUNCPTR (gst_omx_amr_dec_is_format_change);
  audiodec_class->get_samples_per_frame =
      GST_DEBUG_FUNCPTR (gst_omx_amr_dec_get_samples_per_frame);
  audiodec_class->get_channel_positions =
      GST_DEBUG_FUNCPTR (gst_omx_amr_dec_get_channel_positions);

  audiodec_class->cdata.default_sink_template_caps =
      "audio/AMR, rate=(int)8000, channels=(int)1; "
      "audio/AMR-WB, rate=(int)16000, channels=(int)1";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX AMR Audio Decoder",
      "Codec/Decoder/Audio/Hardware",
      "Decode AMR audio streams",
      "Sebastian Dröge <sebastian@centricular.com>");

  gst_omx_set_default_role (&audiodec_class->cdata, "audio_decoder.amrnb");
}

static void
gst_omx_amr_dec_init (GstOMXAMRDec * self)
{
  self->spf = -1;
}

static gboolean
gst_omx_amr_dec_set_format (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXAMRDec *self = GST_OMX_AMR_DEC (dec);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_AMRTYPE amr_param;
  OMX_ERRORTYPE err;
  GstStructure *s;
  gint rate, channels;

  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingAMR;        /* not tested for AMRWB */
  err = gst_omx_port_update_port_definition (port, &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to set AMR format on component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_OMX_INIT_STRUCT (&amr_param);
  amr_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioAmr,
      &amr_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get AMR parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "rate", &rate) ||
      !gst_structure_get_int (s, "channels", &channels)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  self->rate = rate;

  if (rate == 8000)
    self->spf = 160;            /* (8000/50) */
  else if (rate == 16000)
    self->spf = 320;            /* (16000/50) */

  amr_param.nChannels = channels;
  amr_param.eAMRBandMode = 0;   /*FIXME: It may require a specific value */
  amr_param.eAMRDTXMode = 0;
  amr_param.eAMRFrameFormat = 0;

  err =
      gst_omx_component_set_parameter (dec->dec, OMX_IndexParamAudioAmr,
      &amr_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Error setting AMR parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_amr_dec_is_format_change (GstOMXAudioDec * dec, GstOMXPort * port,
    GstCaps * caps)
{
  GstOMXAMRDec *self = GST_OMX_AMR_DEC (dec);
  OMX_AUDIO_PARAM_AMRTYPE amr_param;
  OMX_ERRORTYPE err;
  GstStructure *s;
  gint rate, channels;

  GST_OMX_INIT_STRUCT (&amr_param);
  amr_param.nPortIndex = port->index;

  err =
      gst_omx_component_get_parameter (dec->dec, OMX_IndexParamAudioAmr,
      &amr_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self,
        "Failed to get AMR parameters from component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  s = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (s, "rate", &rate) ||
      !gst_structure_get_int (s, "channels", &channels)) {
    GST_ERROR_OBJECT (self, "Incomplete caps");
    return FALSE;
  }

  if (self->rate != rate)
    return TRUE;

  if (amr_param.nChannels != channels)
    return TRUE;

  return FALSE;
}

static gint
gst_omx_amr_dec_get_samples_per_frame (GstOMXAudioDec * dec, GstOMXPort * port)
{
  return GST_OMX_AMR_DEC (dec)->spf;
}

static gboolean
gst_omx_amr_dec_get_channel_positions (GstOMXAudioDec * dec,
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


  g_return_val_if_fail (pcm_param.nChannels == 1, FALSE);       /* AMR supports only mono */

  position[0] = GST_AUDIO_CHANNEL_POSITION_MONO;

  return TRUE;
}
