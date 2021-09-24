/*
 * Copyright (C) 2014, Fluendo, S.A.
 * Copyright (C) 2014, Metrological Media Innovations B.V.
 *   Author: Josep Torra <josep@fluendo.com>
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
#include <gst/audio/audio.h>

#include <math.h>

#include "gstomxaudiosink.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_audio_sink_debug_category);
#define GST_CAT_DEFAULT gst_omx_audio_sink_debug_category

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_audio_sink_debug_category, "omxaudiosink", \
      0, "debug category for gst-omx audio sink base class");

#define DEFAULT_PROP_MUTE       FALSE
#define DEFAULT_PROP_VOLUME     1.0

#define VOLUME_MAX_DOUBLE       10.0
#define OUT_CHANNELS(num_channels) ((num_channels) > 4 ? 8: (num_channels) > 2 ? 4: (num_channels))

enum
{
  PROP_0,
  PROP_MUTE,
  PROP_VOLUME
};

#define gst_omx_audio_sink_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXAudioSink, gst_omx_audio_sink,
    GST_TYPE_AUDIO_SINK, G_IMPLEMENT_INTERFACE (GST_TYPE_STREAM_VOLUME, NULL);
    DEBUG_INIT);

#define transform_3_4(type) \
static inline void \
transform_3_4_##type (gpointer psrc, gpointer pdst, guint len) \
{ \
  g##type *src = (g##type *) psrc; \
  g##type *dst = (g##type *) pdst; \
  for (; len > 0; len--) { \
    dst[0] = src[0]; \
    dst[1] = src[1]; \
    dst[2] = src[2]; \
    dst[3] = 0; \
    src += 3; \
    dst += 4; \
  } \
}

#define transform_5_8(type) \
static inline void \
transform_5_8_##type (gpointer psrc, gpointer pdst, guint len) \
{ \
  g##type *src = (g##type *) psrc; \
  g##type *dst = (g##type *) pdst; \
  for (; len > 0; len--) { \
    dst[0] = src[0]; \
    dst[1] = src[1]; \
    dst[2] = src[2]; \
    dst[3] = src[3]; \
    dst[4] = src[4]; \
    dst[5] = 0; \
    dst[6] = 0; \
    dst[7] = 0; \
    src += 5; \
    dst += 8; \
  } \
}

#define transform_6_8(type) \
static inline void \
transform_6_8_##type (gpointer psrc, gpointer pdst, guint len) \
{ \
  g##type *src = (g##type *) psrc; \
  g##type *dst = (g##type *) pdst; \
  for (; len > 0; len--) { \
    dst[0] = src[0]; \
    dst[1] = src[1]; \
    dst[2] = src[2]; \
    dst[3] = src[3]; \
    dst[4] = src[4]; \
    dst[5] = src[5]; \
    dst[6] = 0; \
    dst[7] = 0; \
    src += 6; \
    dst += 8; \
  } \
}

#define transform_7_8(type) \
static inline void \
transform_7_8_##type (gpointer psrc, gpointer pdst, guint len) \
{ \
  g##type *src = (g##type *) psrc; \
  g##type *dst = (g##type *) pdst; \
  for (; len > 0; len--) { \
    dst[0] = src[0]; \
    dst[1] = src[1]; \
    dst[2] = src[2]; \
    dst[3] = src[3]; \
    dst[4] = src[4]; \
    dst[5] = src[5]; \
    dst[6] = src[6]; \
    dst[7] = 0; \
    src += 7; \
    dst += 8; \
  } \
}

transform_3_4 (int16);
transform_5_8 (int16);
transform_6_8 (int16);
transform_7_8 (int16);

transform_3_4 (int32);
transform_5_8 (int32);
transform_6_8 (int32);
transform_7_8 (int32);

static void inline
transform (guint in_chan, guint width, gpointer psrc, gpointer pdst, guint len)
{
  guint out_chan = OUT_CHANNELS (in_chan);
  if (width == 16) {
    switch (out_chan) {
      case 4:
        if (in_chan == 3) {
          transform_3_4_int16 (psrc, pdst, len);
        } else {
          g_assert (FALSE);
        }
        break;
      case 8:
        switch (in_chan) {
          case 5:
            transform_5_8_int16 (psrc, pdst, len);
            break;
          case 6:
            transform_6_8_int16 (psrc, pdst, len);
            break;
          case 7:
            transform_7_8_int16 (psrc, pdst, len);
            break;
          default:
            g_assert (FALSE);
        }
        break;
      default:
        g_assert (FALSE);
    }
  } else if (width == 32) {
    switch (out_chan) {
      case 4:
        if (in_chan == 3) {
          transform_3_4_int32 (psrc, pdst, len);
        } else {
          g_assert (FALSE);
        }
        break;
      case 8:
        switch (in_chan) {
          case 5:
            transform_5_8_int32 (psrc, pdst, len);
            break;
          case 6:
            transform_6_8_int32 (psrc, pdst, len);
            break;
          case 7:
            transform_7_8_int32 (psrc, pdst, len);
            break;
          default:
            g_assert (FALSE);
        }
        break;
      default:
        g_assert (FALSE);
    }
  } else {
    g_assert (FALSE);
  }
}

static void
gst_omx_audio_sink_mute_set (GstOMXAudioSink * self, gboolean mute)
{
  if (self->comp) {
    OMX_ERRORTYPE err;
    OMX_AUDIO_CONFIG_MUTETYPE param;

    GST_OMX_INIT_STRUCT (&param);
    param.nPortIndex = self->in_port->index;
    param.bMute = (mute ? OMX_TRUE : OMX_FALSE);
    err = gst_omx_component_set_config (self->comp,
        OMX_IndexConfigAudioMute, &param);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to set mute to %d: %s (0x%08x)",
          param.bMute, gst_omx_error_to_string (err), err);
    }
  }
  self->mute = mute;
}

static void
gst_omx_audio_sink_volume_set (GstOMXAudioSink * self, gdouble volume)
{
  if (self->comp) {
    OMX_ERRORTYPE err;
    OMX_AUDIO_CONFIG_VOLUMETYPE param;
    GST_OMX_INIT_STRUCT (&param);
    param.nPortIndex = self->in_port->index;
    param.bLinear = OMX_TRUE;
    param.sVolume.nValue = volume * 100;
    err = gst_omx_component_set_config (self->comp,
        OMX_IndexConfigAudioVolume, &param);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to set volume to %d: %s (0x%08x)",
          (gint) param.sVolume.nValue, gst_omx_error_to_string (err), err);
    }
  }
  self->volume = volume;
}

static gboolean
gst_omx_audio_sink_open (GstAudioSink * audiosink)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  GstOMXAudioSinkClass *klass = GST_OMX_AUDIO_SINK_GET_CLASS (self);
  gint port_index;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (self, "Opening audio sink");

  self->comp =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->cdata.core_name,
      klass->cdata.component_name, klass->cdata.component_role,
      klass->cdata.hacks);

  if (!self->comp)
    return FALSE;

  if (gst_omx_component_get_state (self->comp,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  port_index = klass->cdata.in_port_index;

  if (port_index == -1) {
    OMX_PORT_PARAM_TYPE param;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->comp, OMX_IndexParamAudioInit,
        &param);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Couldn't get port information: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* Fallback */
      port_index = 0;
    } else {
      GST_DEBUG_OBJECT (self, "Detected %u ports, starting at %u",
          (guint) param.nPorts, (guint) param.nStartPortNumber);
      port_index = param.nStartPortNumber + 0;
    }
  }
  self->in_port = gst_omx_component_add_port (self->comp, port_index);

  port_index = klass->cdata.out_port_index;

  if (port_index == -1) {
    OMX_PORT_PARAM_TYPE param;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->comp, OMX_IndexParamAudioInit,
        &param);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Couldn't get port information: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* Fallback */
      port_index = 0;
    } else {
      GST_DEBUG_OBJECT (self, "Detected %u ports, starting at %u",
          (guint) param.nPorts, (guint) param.nStartPortNumber);
      port_index = param.nStartPortNumber + 1;
    }
  }
  self->out_port = gst_omx_component_add_port (self->comp, port_index);

  if (!self->in_port || !self->out_port)
    return FALSE;

  err = gst_omx_port_set_enabled (self->in_port, FALSE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to disable port: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  err = gst_omx_port_set_enabled (self->out_port, FALSE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to disable port: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  GST_DEBUG_OBJECT (self, "Opened audio sink");

  return TRUE;
}

static gboolean
gst_omx_audio_sink_close (GstAudioSink * audiosink)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Closing audio sink");

  state = gst_omx_component_get_state (self->comp, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->comp, OMX_StateIdle);
      gst_omx_component_get_state (self->comp, 5 * GST_SECOND);
    }
    gst_omx_component_set_state (self->comp, OMX_StateLoaded);
    gst_omx_port_deallocate_buffers (self->in_port);
    if (state > OMX_StateLoaded)
      gst_omx_component_get_state (self->comp, 5 * GST_SECOND);
  }

  self->in_port = NULL;
  self->out_port = NULL;
  if (self->comp)
    gst_omx_component_unref (self->comp);
  self->comp = NULL;

  GST_DEBUG_OBJECT (self, "Closed audio sink");

  return TRUE;
}

static gboolean
gst_omx_audio_sink_parse_spec (GstOMXAudioSink * self,
    GstAudioRingBufferSpec * spec)
{
  self->iec61937 = FALSE;
  self->endianness = GST_AUDIO_INFO_ENDIANNESS (&spec->info);
  self->rate = GST_AUDIO_INFO_RATE (&spec->info);
  self->channels = GST_AUDIO_INFO_CHANNELS (&spec->info);
  self->width = GST_AUDIO_INFO_WIDTH (&spec->info);
  self->is_signed = GST_AUDIO_INFO_IS_SIGNED (&spec->info);
  self->is_float = GST_AUDIO_INFO_IS_FLOAT (&spec->info);

  switch (spec->type) {
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_RAW:
    {
      guint out_channels = OUT_CHANNELS (self->channels);

      self->samples = spec->segsize / self->channels / (self->width >> 3);
      if (self->channels == out_channels) {
        self->buffer_size = spec->segsize;
      } else {
        self->buffer_size = (spec->segsize / self->channels) * out_channels;
      }
      break;
    }
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_AC3:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_EAC3:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_DTS:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_MPEG:
      self->iec61937 = TRUE;
      self->endianness = G_LITTLE_ENDIAN;
      self->channels = 2;
      self->width = 16;
      self->is_signed = TRUE;
      self->is_float = FALSE;
      self->buffer_size = spec->segsize;
      break;
    default:
      return FALSE;
  }

  return TRUE;
}

static inline void
channel_mapping (GstAudioRingBufferSpec * spec,
    OMX_AUDIO_CHANNELTYPE * eChannelMapping)
{
  gint i, nchan = GST_AUDIO_INFO_CHANNELS (&spec->info);

  for (i = 0; i < nchan; i++) {
    OMX_AUDIO_CHANNELTYPE pos;

    switch (GST_AUDIO_INFO_POSITION (&spec->info, i)) {
      case GST_AUDIO_CHANNEL_POSITION_MONO:
      case GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER:
        pos = OMX_AUDIO_ChannelCF;
        break;
      case GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT:
        pos = OMX_AUDIO_ChannelLF;
        break;
      case GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT:
        pos = OMX_AUDIO_ChannelRF;
        break;
      case GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT:
        pos = OMX_AUDIO_ChannelLS;
        break;
      case GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT:
        pos = OMX_AUDIO_ChannelRS;
        break;
      case GST_AUDIO_CHANNEL_POSITION_LFE1:
        pos = OMX_AUDIO_ChannelLFE;
        break;
      case GST_AUDIO_CHANNEL_POSITION_REAR_CENTER:
        pos = OMX_AUDIO_ChannelCS;
        break;
      case GST_AUDIO_CHANNEL_POSITION_REAR_LEFT:
        pos = OMX_AUDIO_ChannelLR;
        break;
      case GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT:
        pos = OMX_AUDIO_ChannelRR;
        break;
      default:
        pos = OMX_AUDIO_ChannelNone;
        break;
    }
    eChannelMapping[i] = pos;
  }
}

static inline const gchar *
ch2str (OMX_AUDIO_CHANNELTYPE ch)
{
  switch (ch) {
    case OMX_AUDIO_ChannelNone:
      return "OMX_AUDIO_ChannelNone";
    case OMX_AUDIO_ChannelLF:
      return "OMX_AUDIO_ChannelLF";
    case OMX_AUDIO_ChannelRF:
      return "OMX_AUDIO_ChannelRF";
    case OMX_AUDIO_ChannelCF:
      return "OMX_AUDIO_ChannelCF";
    case OMX_AUDIO_ChannelLS:
      return "OMX_AUDIO_ChannelLS";
    case OMX_AUDIO_ChannelRS:
      return "OMX_AUDIO_ChannelRS";
    case OMX_AUDIO_ChannelLFE:
      return "OMX_AUDIO_ChannelLFE";
    case OMX_AUDIO_ChannelCS:
      return "OMX_AUDIO_ChannelCS";
    case OMX_AUDIO_ChannelLR:
      return "OMX_AUDIO_ChannelLR";
    case OMX_AUDIO_ChannelRR:
      return "OMX_AUDIO_ChannelRR";
    default:
      return "Invalid value";
  }
}

static inline gboolean
gst_omx_audio_sink_configure_pcm (GstOMXAudioSink * self,
    GstAudioRingBufferSpec * spec)
{
  OMX_AUDIO_PARAM_PCMMODETYPE param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = self->in_port->index;
  param.nChannels = OUT_CHANNELS (self->channels);
  param.eNumData =
      (self->is_signed ? OMX_NumericalDataSigned : OMX_NumericalDataUnsigned);
  param.eEndian =
      ((self->endianness ==
          G_LITTLE_ENDIAN) ? OMX_EndianLittle : OMX_EndianBig);
  param.bInterleaved = OMX_TRUE;
  param.nBitPerSample = self->width;
  param.nSamplingRate = self->rate;

  if (self->is_float) {
    /* This is cherrypicked from xbmc but it doesn't seems to be valid on my RPI.
     * https://github.com/xbmc/xbmc/blob/master/xbmc/cores/AudioEngine/Sinks/AESinkPi.cpp
     */
    param.ePCMMode = (OMX_AUDIO_PCMMODETYPE) 0x8000;
  } else {
    param.ePCMMode = OMX_AUDIO_PCMModeLinear;
  }

  if (spec->type == GST_AUDIO_RING_BUFFER_FORMAT_TYPE_RAW) {
    channel_mapping (spec, &param.eChannelMapping[0]);
  }

  GST_DEBUG_OBJECT (self, "Setting PCM parameters");
  GST_DEBUG_OBJECT (self, "  nChannels: %u", (guint) param.nChannels);
  GST_DEBUG_OBJECT (self, "  eNumData: %s",
      (param.eNumData == OMX_NumericalDataSigned ? "signed" : "unsigned"));
  GST_DEBUG_OBJECT (self, "  eEndian: %s",
      (param.eEndian == OMX_EndianLittle ? "little endian" : "big endian"));
  GST_DEBUG_OBJECT (self, "  bInterleaved: %d", param.bInterleaved);
  GST_DEBUG_OBJECT (self, "  nBitPerSample: %u", (guint) param.nBitPerSample);
  GST_DEBUG_OBJECT (self, "  nSamplingRate: %u", (guint) param.nSamplingRate);
  GST_DEBUG_OBJECT (self, "  ePCMMode: %04x", param.ePCMMode);
  GST_DEBUG_OBJECT (self, "  eChannelMapping: {%s, %s, %s, %s, %s, %s, %s, %s}",
      ch2str (param.eChannelMapping[0]), ch2str (param.eChannelMapping[1]),
      ch2str (param.eChannelMapping[2]), ch2str (param.eChannelMapping[3]),
      ch2str (param.eChannelMapping[4]), ch2str (param.eChannelMapping[5]),
      ch2str (param.eChannelMapping[6]), ch2str (param.eChannelMapping[7]));

  err =
      gst_omx_component_set_parameter (self->comp, OMX_IndexParamAudioPcm,
      &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set PCM parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_omx_audio_sink_prepare (GstAudioSink * audiosink,
    GstAudioRingBufferSpec * spec)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_ERRORTYPE err;

  if (!gst_omx_audio_sink_parse_spec (self, spec))
    goto spec_parse;

  gst_omx_port_get_port_definition (self->in_port, &port_def);

  port_def.nBufferSize = self->buffer_size;
  /* Only allocate a min number of buffers for transfers from our ringbuffer to
   * the hw ringbuffer as we want to keep our small */
  port_def.nBufferCountActual = MAX (port_def.nBufferCountMin, 2);
  port_def.format.audio.eEncoding = OMX_AUDIO_CodingPCM;

  GST_DEBUG_OBJECT (self, "Updating outport port definition");
  GST_DEBUG_OBJECT (self, "  nBufferSize: %u", (guint) port_def.nBufferSize);
  GST_DEBUG_OBJECT (self, "  nBufferCountActual: %u", (guint)
      port_def.nBufferCountActual);
  GST_DEBUG_OBJECT (self, "  audio.eEncoding: 0x%08x",
      port_def.format.audio.eEncoding);

  err = gst_omx_port_update_port_definition (self->in_port, &port_def);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to configure port: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto configuration;
  }

  if (!gst_omx_audio_sink_configure_pcm (self, spec)) {
    goto configuration;
  }

  err = gst_omx_component_set_state (self->comp, OMX_StateIdle);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set state idle: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  err = gst_omx_port_set_flushing (self->in_port, 5 * GST_SECOND, FALSE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set port not flushing: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  err = gst_omx_port_set_enabled (self->in_port, TRUE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to enable port: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  GST_DEBUG_OBJECT (self, "Allocate buffers");
  err = gst_omx_port_allocate_buffers (self->in_port);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed on buffer allocation: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  err = gst_omx_port_wait_enabled (self->in_port, 5 * GST_SECOND);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "port not enabled: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  err = gst_omx_port_mark_reconfigured (self->in_port);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Couln't mark port as reconfigured: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  err = gst_omx_component_set_state (self->comp, OMX_StatePause);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set state paused: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto activation;
  }

  if (gst_omx_component_get_state (self->comp,
          GST_CLOCK_TIME_NONE) != OMX_StatePause)
    goto activation;

  /* Configure some parameters */
  GST_OBJECT_LOCK (self);
  gst_omx_audio_sink_mute_set (self, self->mute);
  gst_omx_audio_sink_volume_set (self, self->volume);
  GST_OBJECT_UNLOCK (self);

#if defined (USE_OMX_TARGET_RPI)
  {
    GstOMXAudioSinkClass *klass = GST_OMX_AUDIO_SINK_GET_CLASS (self);
    OMX_ERRORTYPE err;
    OMX_CONFIG_BRCMAUDIODESTINATIONTYPE param;

    if (klass->destination
        && strlen (klass->destination) < sizeof (param.sName)) {
      GST_DEBUG_OBJECT (self, "Setting destination: %s", klass->destination);
      GST_OMX_INIT_STRUCT (&param);
      strcpy ((char *) param.sName, klass->destination);
      err = gst_omx_component_set_config (self->comp,
          OMX_IndexConfigBrcmAudioDestination, &param);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self,
            "Failed to configuring destination: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto activation;
      }
    }
  }
#endif

  return TRUE;

  /* ERRORS */
spec_parse:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS, (NULL),
        ("Error parsing spec"));
    return FALSE;
  }

configuration:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS, (NULL),
        ("Configuration failed"));
    return FALSE;
  }
activation:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS, (NULL),
        ("Component activation failed"));
    return FALSE;
  }
}

static gboolean
gst_omx_audio_sink_unprepare (GstAudioSink * audiosink)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  OMX_ERRORTYPE err;

  if (gst_omx_component_get_state (self->comp, 0) == OMX_StateIdle)
    return TRUE;

  err = gst_omx_port_set_flushing (self->in_port, 5 * GST_SECOND, TRUE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set port flushing: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto failed;
  }

  err = gst_omx_component_set_state (self->comp, OMX_StateIdle);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set state idle: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto failed;
  }

  err = gst_omx_port_set_enabled (self->in_port, FALSE);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set port disabled: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto failed;
  }

  err = gst_omx_port_wait_buffers_released (self->in_port, 5 * GST_SECOND);
  if (err != OMX_ErrorNone) {
    goto failed;
  }

  err = gst_omx_port_deallocate_buffers (self->in_port);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Couldn't deallocate buffers: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    goto failed;
  }

  err = gst_omx_port_wait_enabled (self->in_port, 1 * GST_SECOND);
  if (err != OMX_ErrorNone) {
    goto failed;
  }

  gst_omx_component_get_state (self->comp, GST_CLOCK_TIME_NONE);

  return TRUE;

  /* ERRORS */
failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->comp),
            gst_omx_component_get_last_error (self->comp)));
    return FALSE;
  }
}

static GstOMXBuffer *
gst_omx_audio_sink_acquire_buffer (GstOMXAudioSink * self)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXPort *port = self->in_port;
  OMX_ERRORTYPE err;
  GstOMXBuffer *buf = NULL;

  while (!buf) {
    acq_ret = gst_omx_port_acquire_buffer (port, &buf, GST_OMX_WAIT);
    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_ERROR) {
      goto component_error;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      GST_DEBUG_OBJECT (self, "Flushing...");
      goto flushing;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      GST_DEBUG_OBJECT (self, "Reconfigure...");
      /* Reallocate all buffers */
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self, "Failed to set port disabled: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self, "Couldn't deallocate buffers: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }

      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }

      err = gst_omx_port_allocate_buffers (port);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone) {
        goto reconfigure_error;
      }
      continue;
    }
  }

  return buf;

  /* ERRORS */
component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->comp),
            gst_omx_component_get_last_error (self->comp)));
    return NULL;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure input port"));
    return NULL;
  }
flushing:
  {
    return NULL;
  }
}

static gint
gst_omx_audio_sink_write (GstAudioSink * audiosink, gpointer data, guint length)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  GstOMXBuffer *buf;
  OMX_ERRORTYPE err;

  GST_LOG_OBJECT (self, "received audio samples buffer of %u bytes", length);

  GST_OMX_AUDIO_SINK_LOCK (self);

  if (!(buf = gst_omx_audio_sink_acquire_buffer (self))) {
    goto beach;
  }

  if (buf->omx_buf->nAllocLen == length) {
    memcpy (buf->omx_buf->pBuffer + buf->omx_buf->nOffset, data, length);
  } else {
    transform (self->channels, self->width, data,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset, self->samples);
  }
  buf->omx_buf->nFilledLen = buf->omx_buf->nAllocLen;

  err = gst_omx_port_release_buffer (self->in_port, buf);
  if (err != OMX_ErrorNone)
    goto release_error;

beach:

  GST_OMX_AUDIO_SINK_UNLOCK (self);

  return length;

  /* ERRORS */
release_error:
  {
    GST_OMX_AUDIO_SINK_UNLOCK (self);
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase input buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    return 0;
  }
}

static guint
gst_omx_audio_sink_delay (GstAudioSink * audiosink)
{
#if defined (USE_OMX_TARGET_RPI)
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  OMX_PARAM_U32TYPE param;
  OMX_ERRORTYPE err;

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = self->in_port->index;
  param.nU32 = 0;
  err = gst_omx_component_get_config (self->comp,
      OMX_IndexConfigAudioRenderingLatency, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to get rendering latency: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    param.nU32 = 0;
  }

  GST_DEBUG_OBJECT (self, "reported delay %u samples", (guint) param.nU32);
  return param.nU32;
#else
  return 0;
#endif
}

static void
gst_omx_audio_sink_reset (GstAudioSink * audiosink)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiosink);
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Flushing sink");

  gst_omx_port_set_flushing (self->in_port, 5 * GST_SECOND, TRUE);

  GST_OMX_AUDIO_SINK_LOCK (self);
  if ((state = gst_omx_component_get_state (self->comp, 0)) > OMX_StatePause) {
    gst_omx_component_set_state (self->comp, OMX_StatePause);
    gst_omx_component_get_state (self->comp, GST_CLOCK_TIME_NONE);
  }

  gst_omx_component_set_state (self->comp, state);
  gst_omx_component_get_state (self->comp, GST_CLOCK_TIME_NONE);

  gst_omx_port_set_flushing (self->in_port, 5 * GST_SECOND, FALSE);

  GST_OMX_AUDIO_SINK_UNLOCK (self);
}

static GstBuffer *
gst_omx_audio_sink_payload (GstAudioBaseSink * audiobasesink, GstBuffer * buf)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (audiobasesink);

  if (self->iec61937) {
    GstBuffer *out;
    gint framesize;
    GstMapInfo iinfo, oinfo;
    GstAudioRingBufferSpec *spec = &audiobasesink->ringbuffer->spec;

    framesize = gst_audio_iec61937_frame_size (spec);
    if (framesize <= 0)
      return NULL;

    out = gst_buffer_new_and_alloc (framesize);

    gst_buffer_map (buf, &iinfo, GST_MAP_READ);
    gst_buffer_map (out, &oinfo, GST_MAP_WRITE);

    if (!gst_audio_iec61937_payload (iinfo.data, iinfo.size,
            oinfo.data, oinfo.size, spec, G_BIG_ENDIAN)) {
      gst_buffer_unref (out);
      return NULL;
    }

    gst_buffer_unmap (buf, &iinfo);
    gst_buffer_unmap (out, &oinfo);

    gst_buffer_copy_into (out, buf, GST_BUFFER_COPY_METADATA, 0, -1);
    return out;
  }

  return gst_buffer_ref (buf);
}

static gboolean
gst_omx_audio_sink_accept_caps (GstOMXAudioSink * self, GstCaps * caps)
{
  GstPad *pad = GST_BASE_SINK (self)->sinkpad;
  GstCaps *pad_caps;
  GstStructure *st;
  gboolean ret = FALSE;
  GstAudioRingBufferSpec spec = { 0 };

  pad_caps = gst_pad_query_caps (pad, caps);
  if (!pad_caps || gst_caps_is_empty (pad_caps)) {
    if (pad_caps)
      gst_caps_unref (pad_caps);
    ret = FALSE;
    goto done;
  }
  gst_caps_unref (pad_caps);

  /* If we've not got fixed caps, creating a stream might fail, so let's just
   * return from here with default acceptcaps behaviour */
  if (!gst_caps_is_fixed (caps))
    goto done;

  /* parse helper expects this set, so avoid nasty warning
   * will be set properly later on anyway  */
  spec.latency_time = GST_SECOND;
  if (!gst_audio_ring_buffer_parse_caps (&spec, caps))
    goto done;

  /* Make sure input is framed (one frame per buffer) and can be payloaded */
  switch (spec.type) {
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_AC3:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_EAC3:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_DTS:
    case GST_AUDIO_RING_BUFFER_FORMAT_TYPE_MPEG:
    {
      gboolean framed = FALSE, parsed = FALSE;
      st = gst_caps_get_structure (caps, 0);

      gst_structure_get_boolean (st, "framed", &framed);
      gst_structure_get_boolean (st, "parsed", &parsed);
      if ((!framed && !parsed) || gst_audio_iec61937_frame_size (&spec) <= 0)
        goto done;
    }
    default:{
    }
  }
  ret = TRUE;

done:
  gst_caps_replace (&spec.caps, NULL);
  return ret;
}

static gboolean
gst_omx_audio_sink_query (GstBaseSink * basesink, GstQuery * query)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (basesink);
  gboolean ret;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_ACCEPT_CAPS:
    {
      GstCaps *caps;

      gst_query_parse_accept_caps (query, &caps);
      ret = gst_omx_audio_sink_accept_caps (self, caps);
      gst_query_set_accept_caps_result (query, ret);
      ret = TRUE;
      break;
    }
    default:
      ret = GST_BASE_SINK_CLASS (parent_class)->query (basesink, query);
      break;
  }
  return ret;
}

static void
gst_omx_audio_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_MUTE:
    {
      gboolean mute = g_value_get_boolean (value);
      GST_OBJECT_LOCK (self);
      if (self->mute != mute) {
        gst_omx_audio_sink_mute_set (self, mute);
      }
      GST_OBJECT_UNLOCK (self);
      break;
    }
    case PROP_VOLUME:
    {
      gdouble volume = g_value_get_double (value);
      GST_OBJECT_LOCK (self);
      if (volume != self->volume) {
        gst_omx_audio_sink_volume_set (self, volume);
      }
      GST_OBJECT_UNLOCK (self);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_audio_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (object);

  switch (prop_id) {
    case PROP_MUTE:
      GST_OBJECT_LOCK (self);
      g_value_set_boolean (value, self->mute);
      GST_OBJECT_UNLOCK (self);
      break;
    case PROP_VOLUME:
      GST_OBJECT_LOCK (self);
      g_value_set_double (value, self->volume);
      GST_OBJECT_UNLOCK (self);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_omx_audio_sink_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (element);
  OMX_ERRORTYPE err;

  switch (transition) {
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
    {
      GST_DEBUG_OBJECT (self, "going to PLAYING state");
      err = gst_omx_component_set_state (self->comp, OMX_StateExecuting);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self, "Failed to set state executing: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        return GST_STATE_CHANGE_FAILURE;
      }

      if (gst_omx_component_get_state (self->comp,
              GST_CLOCK_TIME_NONE) != OMX_StateExecuting) {
        return GST_STATE_CHANGE_FAILURE;
      }
      GST_DEBUG_OBJECT (self, "in PLAYING state");
      break;
    }
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
    {
      GST_DEBUG_OBJECT (self, "going to PAUSED state");
      err = gst_omx_component_set_state (self->comp, OMX_StatePause);
      if (err != OMX_ErrorNone) {
        GST_ERROR_OBJECT (self, "Failed to set state paused: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
        return GST_STATE_CHANGE_FAILURE;
      }

      if (gst_omx_component_get_state (self->comp,
              GST_CLOCK_TIME_NONE) != OMX_StatePause) {
        return GST_STATE_CHANGE_FAILURE;
      }
      GST_DEBUG_OBJECT (self, "in PAUSED state");
      break;
    }
    default:
      break;
  }

  return ret;
}

static void
gst_omx_audio_sink_finalize (GObject * object)
{
  GstOMXAudioSink *self = GST_OMX_AUDIO_SINK (object);

  g_mutex_clear (&self->lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_omx_audio_sink_class_init (GstOMXAudioSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *basesink_class = GST_BASE_SINK_CLASS (klass);
  GstAudioBaseSinkClass *baudiosink_class = GST_AUDIO_BASE_SINK_CLASS (klass);
  GstAudioSinkClass *audiosink_class = GST_AUDIO_SINK_CLASS (klass);

  gobject_class->set_property = gst_omx_audio_sink_set_property;
  gobject_class->get_property = gst_omx_audio_sink_get_property;
  gobject_class->finalize = gst_omx_audio_sink_finalize;

  g_object_class_install_property (gobject_class, PROP_MUTE,
      g_param_spec_boolean ("mute", "Mute", "mute channel",
          DEFAULT_PROP_MUTE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VOLUME,
      g_param_spec_double ("volume", "Volume", "volume factor, 1.0=100%",
          0.0, VOLUME_MAX_DOUBLE, DEFAULT_PROP_VOLUME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_audio_sink_change_state);

  basesink_class->query = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_query);

  baudiosink_class->payload = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_payload);

  audiosink_class->open = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_open);
  audiosink_class->close = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_close);
  audiosink_class->prepare = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_prepare);
  audiosink_class->unprepare = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_unprepare);
  audiosink_class->write = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_write);
  audiosink_class->delay = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_delay);
  audiosink_class->reset = GST_DEBUG_FUNCPTR (gst_omx_audio_sink_reset);


  klass->cdata.type = GST_OMX_COMPONENT_TYPE_SINK;
}

static void
gst_omx_audio_sink_init (GstOMXAudioSink * self)
{
  g_mutex_init (&self->lock);

  self->mute = DEFAULT_PROP_MUTE;
  self->volume = DEFAULT_PROP_VOLUME;

  /* For the Raspberry PI there's a big hw buffer and 400 ms seems a good
   * size for our ringbuffer. OpenSL ES Sink also allocates a buffer of 400 ms
   * in Android so I guess that this should be a sane value for OpenMax in
   * general. */
  GST_AUDIO_BASE_SINK (self)->buffer_time = 400000;
  gst_audio_base_sink_set_provide_clock (GST_AUDIO_BASE_SINK (self), TRUE);
}
