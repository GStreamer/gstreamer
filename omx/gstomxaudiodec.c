/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 * Copyright (C) 2013, Collabora Ltd.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include <string.h>

#include "gstomxaudiodec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_audio_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_audio_dec_debug_category

/* prototypes */
static void gst_omx_audio_dec_finalize (GObject * object);

static GstStateChangeReturn
gst_omx_audio_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_omx_audio_dec_open (GstAudioDecoder * decoder);
static gboolean gst_omx_audio_dec_close (GstAudioDecoder * decoder);
static gboolean gst_omx_audio_dec_start (GstAudioDecoder * decoder);
static gboolean gst_omx_audio_dec_stop (GstAudioDecoder * decoder);
static gboolean gst_omx_audio_dec_set_format (GstAudioDecoder * decoder,
    GstCaps * caps);
static void gst_omx_audio_dec_flush (GstAudioDecoder * decoder, gboolean hard);
static GstFlowReturn gst_omx_audio_dec_handle_frame (GstAudioDecoder * decoder,
    GstBuffer * buffer);
static GstFlowReturn gst_omx_audio_dec_drain (GstOMXAudioDec * self);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_audio_dec_debug_category, "omxaudiodec", 0, \
      "debug category for gst-omx audio decoder base class");


G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXAudioDec, gst_omx_audio_dec,
    GST_TYPE_AUDIO_DECODER, DEBUG_INIT);

static void
gst_omx_audio_dec_class_init (GstOMXAudioDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioDecoderClass *audio_decoder_class = GST_AUDIO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_omx_audio_dec_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_change_state);

  audio_decoder_class->open = GST_DEBUG_FUNCPTR (gst_omx_audio_dec_open);
  audio_decoder_class->close = GST_DEBUG_FUNCPTR (gst_omx_audio_dec_close);
  audio_decoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_audio_dec_start);
  audio_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_audio_dec_stop);
  audio_decoder_class->flush = GST_DEBUG_FUNCPTR (gst_omx_audio_dec_flush);
  audio_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_set_format);
  audio_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_audio_dec_handle_frame);

  klass->cdata.type = GST_OMX_COMPONENT_TYPE_FILTER;
  klass->cdata.default_src_template_caps =
      "audio/x-raw, "
      "rate = (int) [ 1, MAX ], "
      "channels = (int) [ 1, " G_STRINGIFY (OMX_AUDIO_MAXCHANNELS) " ], "
      "format = (string) " GST_AUDIO_FORMATS_ALL;
}

static void
gst_omx_audio_dec_init (GstOMXAudioDec * self)
{
  gst_audio_decoder_set_needs_format (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_drainable (GST_AUDIO_DECODER (self), TRUE);
  gst_audio_decoder_set_use_default_pad_acceptcaps (GST_AUDIO_DECODER_CAST
      (self), TRUE);
  GST_PAD_SET_ACCEPT_TEMPLATE (GST_AUDIO_DECODER_SINK_PAD (self));

  g_mutex_init (&self->drain_lock);
  g_cond_init (&self->drain_cond);

  self->output_adapter = gst_adapter_new ();
}

static gboolean
gst_omx_audio_dec_open (GstAudioDecoder * decoder)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (decoder);
  GstOMXAudioDecClass *klass = GST_OMX_AUDIO_DEC_GET_CLASS (self);
  gint in_port_index, out_port_index;

  GST_DEBUG_OBJECT (self, "Opening decoder");

  self->dec =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->cdata.core_name,
      klass->cdata.component_name, klass->cdata.component_role,
      klass->cdata.hacks);
  self->started = FALSE;

  if (!self->dec)
    return FALSE;

  if (gst_omx_component_get_state (self->dec,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  in_port_index = klass->cdata.in_port_index;
  out_port_index = klass->cdata.out_port_index;

  if (in_port_index == -1 || out_port_index == -1) {
    OMX_PORT_PARAM_TYPE param;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->dec, OMX_IndexParamAudioInit,
        &param);
    if (err != OMX_ErrorNone) {
      GST_WARNING_OBJECT (self, "Couldn't get port information: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      /* Fallback */
      in_port_index = 0;
      out_port_index = 1;
    } else {
      GST_DEBUG_OBJECT (self, "Detected %u ports, starting at %u",
          (guint) param.nPorts, (guint) param.nStartPortNumber);
      in_port_index = param.nStartPortNumber + 0;
      out_port_index = param.nStartPortNumber + 1;
    }
  }
  self->dec_in_port = gst_omx_component_add_port (self->dec, in_port_index);
  self->dec_out_port = gst_omx_component_add_port (self->dec, out_port_index);

  if (!self->dec_in_port || !self->dec_out_port)
    return FALSE;

  GST_DEBUG_OBJECT (self, "Opened decoder");

  return TRUE;
}

static gboolean
gst_omx_audio_dec_shutdown (GstOMXAudioDec * self)
{
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Shutting down decoder");

  state = gst_omx_component_get_state (self->dec, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->dec, OMX_StateIdle);
      gst_omx_component_get_state (self->dec, 5 * GST_SECOND);
    }
    gst_omx_component_set_state (self->dec, OMX_StateLoaded);
    gst_omx_port_deallocate_buffers (self->dec_in_port);
    gst_omx_port_deallocate_buffers (self->dec_out_port);
    if (state > OMX_StateLoaded)
      gst_omx_component_get_state (self->dec, 5 * GST_SECOND);
  }

  return TRUE;
}

static gboolean
gst_omx_audio_dec_close (GstAudioDecoder * decoder)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Closing decoder");

  if (!gst_omx_audio_dec_shutdown (self))
    return FALSE;

  self->dec_in_port = NULL;
  self->dec_out_port = NULL;
  if (self->dec)
    gst_omx_component_unref (self->dec);
  self->dec = NULL;

  self->started = FALSE;

  GST_DEBUG_OBJECT (self, "Closed decoder");

  return TRUE;
}

static void
gst_omx_audio_dec_finalize (GObject * object)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (object);

  g_mutex_clear (&self->drain_lock);
  g_cond_clear (&self->drain_cond);

  if (self->output_adapter)
    gst_object_unref (self->output_adapter);
  self->output_adapter = NULL;

  G_OBJECT_CLASS (gst_omx_audio_dec_parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_omx_audio_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstOMXAudioDec *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_OMX_AUDIO_DEC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_OMX_AUDIO_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->downstream_flow_ret = GST_FLOW_OK;
      self->draining = FALSE;
      self->started = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->dec_in_port)
        gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
      if (self->dec_out_port)
        gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

      g_mutex_lock (&self->drain_lock);
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      g_mutex_unlock (&self->drain_lock);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_omx_audio_dec_parent_class)->change_state
      (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->downstream_flow_ret = GST_FLOW_FLUSHING;
      self->started = FALSE;

      if (!gst_omx_audio_dec_shutdown (self))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_omx_audio_dec_loop (GstOMXAudioDec * self)
{
  GstOMXAudioDecClass *klass = GST_OMX_AUDIO_DEC_GET_CLASS (self);
  GstOMXPort *port = self->dec_out_port;
  GstOMXBuffer *buf = NULL;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;
  OMX_ERRORTYPE err;
  gint spf;

  acq_return = gst_omx_port_acquire_buffer (port, &buf, GST_OMX_WAIT);
  if (acq_return == GST_OMX_ACQUIRE_BUFFER_ERROR) {
    goto component_error;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
    goto flushing;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_EOS) {
    goto eos;
  }

  if (!gst_pad_has_current_caps (GST_AUDIO_DECODER_SRC_PAD (self)) ||
      acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    OMX_AUDIO_PARAM_PCMMODETYPE pcm_param;
    GstAudioChannelPosition omx_position[OMX_AUDIO_MAXCHANNELS];
    GstOMXAudioDecClass *klass = GST_OMX_AUDIO_DEC_GET_CLASS (self);
    gint i;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    /* Reallocate all buffers */
    if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE
        && gst_omx_port_is_enabled (port)) {
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;
    }

    /* Just update caps */
    GST_AUDIO_DECODER_STREAM_LOCK (self);

    gst_omx_port_get_port_definition (port, &port_def);
    g_assert (port_def.format.audio.eEncoding == OMX_AUDIO_CodingPCM);

    GST_OMX_INIT_STRUCT (&pcm_param);
    pcm_param.nPortIndex = self->dec_out_port->index;
    err =
        gst_omx_component_get_parameter (self->dec, OMX_IndexParamAudioPcm,
        &pcm_param);
    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to get PCM parameters: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      goto caps_failed;
    }

    g_assert (pcm_param.ePCMMode == OMX_AUDIO_PCMModeLinear);
    g_assert (pcm_param.bInterleaved == OMX_TRUE);

    gst_audio_info_init (&self->info);

    for (i = 0; i < pcm_param.nChannels; i++) {
      switch (pcm_param.eChannelMapping[i]) {
        case OMX_AUDIO_ChannelLF:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_LEFT;
          break;
        case OMX_AUDIO_ChannelRF:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_RIGHT;
          break;
        case OMX_AUDIO_ChannelCF:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER;
          break;
        case OMX_AUDIO_ChannelLS:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_SIDE_LEFT;
          break;
        case OMX_AUDIO_ChannelRS:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_SIDE_RIGHT;
          break;
        case OMX_AUDIO_ChannelLFE:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_LFE1;
          break;
        case OMX_AUDIO_ChannelCS:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_REAR_CENTER;
          break;
        case OMX_AUDIO_ChannelLR:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_REAR_LEFT;
          break;
        case OMX_AUDIO_ChannelRR:
          omx_position[i] = GST_AUDIO_CHANNEL_POSITION_REAR_RIGHT;
          break;
        case OMX_AUDIO_ChannelNone:
        default:
          /* This will break the outer loop too as the
           * i == pcm_param.nChannels afterwards */
          for (i = 0; i < pcm_param.nChannels; i++)
            omx_position[i] = GST_AUDIO_CHANNEL_POSITION_NONE;
          break;
      }
    }
    if (pcm_param.nChannels == 1
        && omx_position[0] == GST_AUDIO_CHANNEL_POSITION_FRONT_CENTER)
      omx_position[0] = GST_AUDIO_CHANNEL_POSITION_MONO;

    if (omx_position[0] == GST_AUDIO_CHANNEL_POSITION_NONE
        && klass->get_channel_positions) {
      GST_WARNING_OBJECT (self,
          "Failed to get a valid channel layout, trying fallback");
      klass->get_channel_positions (self, self->dec_out_port, omx_position);
    }

    memcpy (self->position, omx_position, sizeof (omx_position));
    gst_audio_channel_positions_to_valid_order (self->position,
        pcm_param.nChannels);
    self->needs_reorder =
        (memcmp (self->position, omx_position,
            sizeof (GstAudioChannelPosition) * pcm_param.nChannels) != 0);
    if (self->needs_reorder)
      gst_audio_get_channel_reorder_map (pcm_param.nChannels, self->position,
          omx_position, self->reorder_map);

    gst_audio_info_set_format (&self->info,
        gst_audio_format_build_integer (pcm_param.eNumData ==
            OMX_NumericalDataSigned,
            pcm_param.eEndian ==
            OMX_EndianLittle ? G_LITTLE_ENDIAN : G_BIG_ENDIAN,
            pcm_param.nBitPerSample, pcm_param.nBitPerSample),
        pcm_param.nSamplingRate, pcm_param.nChannels, self->position);

    GST_DEBUG_OBJECT (self,
        "Setting output state: format %s, rate %u, channels %u",
        gst_audio_format_to_string (self->info.finfo->format),
        (guint) pcm_param.nSamplingRate, (guint) pcm_param.nChannels);

    if (!gst_audio_decoder_set_output_format (GST_AUDIO_DECODER (self),
            &self->info)
        || !gst_audio_decoder_negotiate (GST_AUDIO_DECODER (self))) {
      if (buf)
        gst_omx_port_release_buffer (port, buf);
      goto caps_failed;
    }

    GST_AUDIO_DECODER_STREAM_UNLOCK (self);

    if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_allocate_buffers (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_populate (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone)
        goto reconfigure_error;
    }

    /* Now get a buffer */
    if (acq_return != GST_OMX_ACQUIRE_BUFFER_OK) {
      return;
    }
  }

  g_assert (acq_return == GST_OMX_ACQUIRE_BUFFER_OK);
  if (!buf) {
    g_assert ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER));
    GST_AUDIO_DECODER_STREAM_LOCK (self);
    goto eos;
  }

  /* This prevents a deadlock between the srcpad stream
   * lock and the audiocodec stream lock, if ::reset()
   * is called at the wrong time
   */
  if (gst_omx_port_is_flushing (port)) {
    GST_DEBUG_OBJECT (self, "Flushing");
    gst_omx_port_release_buffer (port, buf);
    goto flushing;
  }

  GST_DEBUG_OBJECT (self, "Handling buffer: 0x%08x %" G_GUINT64_FORMAT,
      (guint) buf->omx_buf->nFlags,
      (guint64) GST_OMX_GET_TICKS (buf->omx_buf->nTimeStamp));

  GST_AUDIO_DECODER_STREAM_LOCK (self);

  spf = klass->get_samples_per_frame (self, self->dec_out_port);

  if (buf->omx_buf->nFilledLen > 0) {
    GstBuffer *outbuf;
    GstMapInfo minfo;

    GST_DEBUG_OBJECT (self, "Handling output data");

    if (buf->omx_buf->nFilledLen % self->info.bpf != 0) {
      gst_omx_port_release_buffer (port, buf);
      goto invalid_buffer;
    }

    outbuf =
        gst_audio_decoder_allocate_output_buffer (GST_AUDIO_DECODER (self),
        buf->omx_buf->nFilledLen);

    gst_buffer_map (outbuf, &minfo, GST_MAP_WRITE);
    if (self->needs_reorder) {
      gint i, n_samples, c, n_channels;
      gint *reorder_map = self->reorder_map;
      gint16 *dest, *source;

      dest = (gint16 *) minfo.data;
      source = (gint16 *) (buf->omx_buf->pBuffer + buf->omx_buf->nOffset);
      n_samples = buf->omx_buf->nFilledLen / self->info.bpf;
      n_channels = self->info.channels;

      for (i = 0; i < n_samples; i++) {
        for (c = 0; c < n_channels; c++) {
          dest[i * n_channels + reorder_map[c]] = source[i * n_channels + c];
        }
      }
    } else {
      memcpy (minfo.data, buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);
    }
    gst_buffer_unmap (outbuf, &minfo);

    if (spf != -1) {
      gst_adapter_push (self->output_adapter, outbuf);
    } else {
      flow_ret =
          gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (self), outbuf, 1);
    }
  }

  GST_DEBUG_OBJECT (self, "Read frame from component");

  if (spf != -1) {
    GstBuffer *outbuf;
    guint avail = gst_adapter_available (self->output_adapter);
    guint nframes;

    /* We take a multiple of codec frames and push
     * them downstream
     */
    avail /= self->info.bpf;
    nframes = avail / spf;
    avail = nframes * spf;
    avail *= self->info.bpf;

    if (avail > 0) {
      outbuf = gst_adapter_take_buffer (self->output_adapter, avail);
      flow_ret =
          gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (self), outbuf,
          nframes);
    }
  }

  GST_DEBUG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));

  if (buf) {
    err = gst_omx_port_release_buffer (port, buf);
    if (err != OMX_ErrorNone)
      goto release_error;
  }

  self->downstream_flow_ret = flow_ret;

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  GST_AUDIO_DECODER_STREAM_UNLOCK (self);

  return;

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->dec),
            gst_omx_component_get_last_error (self->dec)));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }

flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    g_mutex_lock (&self->drain_lock);
    if (self->draining) {
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
    }
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_FLUSHING;
    self->started = FALSE;
    g_mutex_unlock (&self->drain_lock);
    return;
  }

eos:
  {
    spf = klass->get_samples_per_frame (self, self->dec_out_port);
    if (spf != -1) {
      GstBuffer *outbuf;
      guint avail = gst_adapter_available (self->output_adapter);
      guint nframes;

      /* On EOS we take the complete adapter content, no matter
       * if it is a multiple of the codec frame size or not.
       */
      avail /= self->info.bpf;
      nframes = (avail + spf - 1) / spf;
      avail *= self->info.bpf;

      if (avail > 0) {
        outbuf = gst_adapter_take_buffer (self->output_adapter, avail);
        flow_ret =
            gst_audio_decoder_finish_frame (GST_AUDIO_DECODER (self), outbuf,
            nframes);
      }
    }

    g_mutex_lock (&self->drain_lock);
    if (self->draining) {
      GST_DEBUG_OBJECT (self, "Drained");
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      flow_ret = GST_FLOW_OK;
      gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    } else {
      GST_DEBUG_OBJECT (self, "Component signalled EOS");
      flow_ret = GST_FLOW_EOS;
    }
    g_mutex_unlock (&self->drain_lock);

    GST_AUDIO_DECODER_STREAM_LOCK (self);
    self->downstream_flow_ret = flow_ret;

    /* Here we fallback and pause the task for the EOS case */
    if (flow_ret != GST_FLOW_OK)
      goto flow_error;

    GST_AUDIO_DECODER_STREAM_UNLOCK (self);

    return;
  }

flow_error:
  {
    if (flow_ret == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
      self->started = FALSE;
    } else if (flow_ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED,
          ("Internal data stream error."), ("stream stopped, reason %s",
              gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
      self->started = FALSE;
    } else if (flow_ret == GST_FLOW_FLUSHING) {
      GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
      g_mutex_lock (&self->drain_lock);
      if (self->draining) {
        self->draining = FALSE;
        g_cond_broadcast (&self->drain_cond);
      }
      gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
      self->started = FALSE;
      g_mutex_unlock (&self->drain_lock);
    }
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }

reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }

invalid_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Invalid sized input buffer"));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }

caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    return;
  }
release_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase output buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    gst_pad_push_event (GST_AUDIO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    return;
  }
}

static gboolean
gst_omx_audio_dec_start (GstAudioDecoder * decoder)
{
  GstOMXAudioDec *self;

  self = GST_OMX_AUDIO_DEC (decoder);

  self->last_upstream_ts = 0;
  self->downstream_flow_ret = GST_FLOW_OK;

  return TRUE;
}

static gboolean
gst_omx_audio_dec_stop (GstAudioDecoder * decoder)
{
  GstOMXAudioDec *self;

  self = GST_OMX_AUDIO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Stopping decoder");

  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

  gst_pad_stop_task (GST_AUDIO_DECODER_SRC_PAD (decoder));

  if (gst_omx_component_get_state (self->dec, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->dec, OMX_StateIdle);

  self->downstream_flow_ret = GST_FLOW_FLUSHING;
  self->started = FALSE;

  g_mutex_lock (&self->drain_lock);
  self->draining = FALSE;
  g_cond_broadcast (&self->drain_cond);
  g_mutex_unlock (&self->drain_lock);

  gst_adapter_flush (self->output_adapter,
      gst_adapter_available (self->output_adapter));

  gst_omx_component_get_state (self->dec, 5 * GST_SECOND);

  gst_buffer_replace (&self->codec_data, NULL);

  GST_DEBUG_OBJECT (self, "Stopped decoder");

  return TRUE;
}

static gboolean
gst_omx_audio_dec_set_format (GstAudioDecoder * decoder, GstCaps * caps)
{
  GstOMXAudioDec *self;
  GstOMXAudioDecClass *klass;
  GstStructure *s;
  const GValue *codec_data;
  gboolean is_format_change = FALSE;
  gboolean needs_disable = FALSE;

  self = GST_OMX_AUDIO_DEC (decoder);
  klass = GST_OMX_AUDIO_DEC_GET_CLASS (decoder);

  GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, caps);

  /* Check if the caps change is a real format change or if only irrelevant
   * parts of the caps have changed or nothing at all.
   */
  if (klass->is_format_change)
    is_format_change = klass->is_format_change (self, self->dec_in_port, caps);

  needs_disable =
      gst_omx_component_get_state (self->dec,
      GST_CLOCK_TIME_NONE) != OMX_StateLoaded;
  /* If the component is not in Loaded state and a real format change happens
   * we have to disable the port and re-allocate all buffers. If no real
   * format change happened we can just exit here.
   */
  if (needs_disable && !is_format_change) {
    GST_DEBUG_OBJECT (self,
        "Already running and caps did not change the format");
    return TRUE;
  }

  if (needs_disable && is_format_change) {
    GstOMXPort *out_port = self->dec_out_port;

    GST_DEBUG_OBJECT (self, "Need to disable and drain decoder");

    gst_omx_audio_dec_drain (self);
    gst_omx_audio_dec_flush (decoder, FALSE);
    gst_omx_port_set_flushing (out_port, 5 * GST_SECOND, TRUE);

    if (klass->cdata.hacks & GST_OMX_HACK_NO_COMPONENT_RECONFIGURE) {
      GST_AUDIO_DECODER_STREAM_UNLOCK (self);
      gst_omx_audio_dec_stop (GST_AUDIO_DECODER (self));
      gst_omx_audio_dec_close (GST_AUDIO_DECODER (self));
      GST_AUDIO_DECODER_STREAM_LOCK (self);

      if (!gst_omx_audio_dec_open (GST_AUDIO_DECODER (self)))
        return FALSE;
      needs_disable = FALSE;
    } else {
      /* Disabling at the same time input port and output port is only
       * required when a buffer is shared between the ports. This cannot
       * be the case for a decoder because its input and output buffers
       * are of different nature. So let's disable ports sequencially.
       * Starting from IL 1.2.0, this point has been clarified.
       * OMX_SendCommand will return an error if the IL client attempts to
       * call it when there is already an on-going command being processed.
       * The exception is for buffer sharing above and the event
       * OMX_EventPortNeedsDisable will be sent to request disabling the
       * other port at the same time. */
      if (gst_omx_port_set_enabled (self->dec_in_port, FALSE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_buffers_released (self->dec_in_port,
              5 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_deallocate_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_enabled (self->dec_in_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_set_enabled (out_port, FALSE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_buffers_released (out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_deallocate_buffers (out_port) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_enabled (out_port, 1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
    }

    GST_DEBUG_OBJECT (self, "Decoder drained and disabled");
  }

  if (klass->set_format) {
    if (!klass->set_format (self, self->dec_in_port, caps)) {
      GST_ERROR_OBJECT (self, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (self, "Updating outport port definition");
  if (gst_omx_port_update_port_definition (self->dec_out_port,
          NULL) != OMX_ErrorNone)
    return FALSE;

  /* Get codec data from caps */
  gst_buffer_replace (&self->codec_data, NULL);
  s = gst_caps_get_structure (caps, 0);
  codec_data = gst_structure_get_value (s, "codec_data");
  if (codec_data) {
    /* Vorbis and some other codecs have multiple buffers in
     * the stream-header field */
    self->codec_data = gst_value_get_buffer (codec_data);
    if (self->codec_data)
      gst_buffer_ref (self->codec_data);
  }

  GST_DEBUG_OBJECT (self, "Enabling component");

  if (needs_disable) {
    if (gst_omx_port_set_enabled (self->dec_in_port, TRUE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_allocate_buffers (self->dec_in_port) != OMX_ErrorNone)
      return FALSE;

    if ((klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      if (gst_omx_port_set_enabled (self->dec_out_port, TRUE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_allocate_buffers (self->dec_out_port) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->dec_out_port,
              5 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_port_wait_enabled (self->dec_in_port,
            5 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_mark_reconfigured (self->dec_in_port) != OMX_ErrorNone)
      return FALSE;
  } else {
    if (!(klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      /* Disable output port */
      if (gst_omx_port_set_enabled (self->dec_out_port, FALSE) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->dec_out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_component_set_state (self->dec,
              OMX_StateIdle) != OMX_ErrorNone)
        return FALSE;

      /* Need to allocate buffers to reach Idle state */
      if (gst_omx_port_allocate_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;
    } else {
      if (gst_omx_component_set_state (self->dec,
              OMX_StateIdle) != OMX_ErrorNone)
        return FALSE;

      /* Need to allocate buffers to reach Idle state */
      if (gst_omx_port_allocate_buffers (self->dec_in_port) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_allocate_buffers (self->dec_out_port) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_component_get_state (self->dec,
            GST_CLOCK_TIME_NONE) != OMX_StateIdle)
      return FALSE;

    if (gst_omx_component_set_state (self->dec,
            OMX_StateExecuting) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_component_get_state (self->dec,
            GST_CLOCK_TIME_NONE) != OMX_StateExecuting)
      return FALSE;
  }

  /* Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, FALSE);

  if (gst_omx_component_get_last_error (self->dec) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Component in error state: %s (0x%08x)",
        gst_omx_component_get_last_error_string (self->dec),
        gst_omx_component_get_last_error (self->dec));
    return FALSE;
  }

  self->downstream_flow_ret = GST_FLOW_OK;

  return TRUE;
}

static void
gst_omx_audio_dec_flush (GstAudioDecoder * decoder, gboolean hard)
{
  GstOMXAudioDec *self = GST_OMX_AUDIO_DEC (decoder);
  OMX_ERRORTYPE err = OMX_ErrorNone;

  GST_DEBUG_OBJECT (self, "Flushing decoder");

  if (gst_omx_component_get_state (self->dec, 0) == OMX_StateLoaded)
    return;

  /* 0) Pause the components */
  if (gst_omx_component_get_state (self->dec, 0) == OMX_StateExecuting) {
    gst_omx_component_set_state (self->dec, OMX_StatePause);
    gst_omx_component_get_state (self->dec, GST_CLOCK_TIME_NONE);
  }

  /* 1) Flush the ports */
  GST_DEBUG_OBJECT (self, "flushing ports");
  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, TRUE);

  /* 2) Wait until the srcpad loop is stopped,
   * unlock GST_AUDIO_DECODER_STREAM_LOCK to prevent deadlocks
   * caused by using this lock from inside the loop function */
  GST_AUDIO_DECODER_STREAM_UNLOCK (self);
  gst_pad_stop_task (GST_AUDIO_DECODER_SRC_PAD (decoder));
  GST_DEBUG_OBJECT (self, "Flushing -- task stopped");
  GST_AUDIO_DECODER_STREAM_LOCK (self);

  /* 3) Resume components */
  gst_omx_component_set_state (self->dec, OMX_StateExecuting);
  gst_omx_component_get_state (self->dec, GST_CLOCK_TIME_NONE);

  /* 4) Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->dec_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->dec_out_port, 5 * GST_SECOND, FALSE);

  err = gst_omx_port_populate (self->dec_out_port);

  if (err != OMX_ErrorNone) {
    GST_WARNING_OBJECT (self, "Failed to populate output port: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  /* Reset our state */
  gst_adapter_flush (self->output_adapter,
      gst_adapter_available (self->output_adapter));
  self->last_upstream_ts = 0;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->started = FALSE;
  GST_DEBUG_OBJECT (self, "Flush finished");
}

static GstFlowReturn
gst_omx_audio_dec_handle_frame (GstAudioDecoder * decoder, GstBuffer * inbuf)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXAudioDec *self;
  GstOMXPort *port;
  GstOMXBuffer *buf;
  GstBuffer *codec_data = NULL;
  guint offset = 0;
  GstClockTime timestamp, duration;
  OMX_ERRORTYPE err;
  GstMapInfo minfo;

  self = GST_OMX_AUDIO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Handling frame");

  if (self->downstream_flow_ret != GST_FLOW_OK) {
    return self->downstream_flow_ret;
  }

  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Starting task");
    gst_pad_start_task (GST_AUDIO_DECODER_SRC_PAD (self),
        (GstTaskFunction) gst_omx_audio_dec_loop, decoder, NULL);
  }

  if (inbuf == NULL)
    return gst_omx_audio_dec_drain (self);

  /* Make sure to keep a reference to the input here,
   * it can be unreffed from the other thread if
   * finish_frame() is called */
  gst_buffer_ref (inbuf);

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  duration = GST_BUFFER_DURATION (inbuf);

  port = self->dec_in_port;

  gst_buffer_map (inbuf, &minfo, GST_MAP_READ);

  while (offset < minfo.size) {
    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_AUDIO_DECODER_STREAM_UNLOCK (self);
    acq_ret = gst_omx_port_acquire_buffer (port, &buf, GST_OMX_WAIT);

    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_ERROR) {
      GST_AUDIO_DECODER_STREAM_LOCK (self);
      goto component_error;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      GST_AUDIO_DECODER_STREAM_LOCK (self);
      goto flushing;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      /* Reallocate all buffers */
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_allocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_DECODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      /* Now get a new buffer and fill it */
      GST_AUDIO_DECODER_STREAM_LOCK (self);
      continue;
    }
    GST_AUDIO_DECODER_STREAM_LOCK (self);

    g_assert (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

    if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <= 0) {
      gst_omx_port_release_buffer (port, buf);
      goto full_buffer;
    }

    if (self->downstream_flow_ret != GST_FLOW_OK) {
      gst_omx_port_release_buffer (port, buf);
      goto flow_error;
    }

    if (self->codec_data) {
      GST_DEBUG_OBJECT (self, "Passing codec data to the component");

      codec_data = self->codec_data;

      if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <
          gst_buffer_get_size (codec_data)) {
        gst_omx_port_release_buffer (port, buf);
        goto too_large_codec_data;
      }

      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;
      buf->omx_buf->nFilledLen = gst_buffer_get_size (codec_data);
      gst_buffer_extract (codec_data, 0,
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);

      if (GST_CLOCK_TIME_IS_VALID (timestamp))
        GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp,
            gst_util_uint64_scale (timestamp, OMX_TICKS_PER_SECOND,
                GST_SECOND));
      else
        GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp, G_GUINT64_CONSTANT (0));
      buf->omx_buf->nTickCount = 0;

      self->started = TRUE;
      err = gst_omx_port_release_buffer (port, buf);
      gst_buffer_replace (&self->codec_data, NULL);
      if (err != OMX_ErrorNone)
        goto release_error;
      /* Acquire new buffer for the actual frame */
      continue;
    }

    /* Now handle the frame */
    GST_DEBUG_OBJECT (self, "Passing frame offset %d to the component", offset);

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    buf->omx_buf->nFilledLen =
        MIN (minfo.size - offset,
        buf->omx_buf->nAllocLen - buf->omx_buf->nOffset);
    gst_buffer_extract (inbuf, offset,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);

    if (timestamp != GST_CLOCK_TIME_NONE) {
      GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp,
          gst_util_uint64_scale (timestamp, OMX_TICKS_PER_SECOND, GST_SECOND));
      self->last_upstream_ts = timestamp;
    } else {
      GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp, G_GUINT64_CONSTANT (0));
    }

    if (duration != GST_CLOCK_TIME_NONE && offset == 0) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (duration, OMX_TICKS_PER_SECOND, GST_SECOND);
      self->last_upstream_ts += duration;
    } else {
      buf->omx_buf->nTickCount = 0;
    }

    if (offset == 0)
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;

    /* TODO: Set flags
     *   - OMX_BUFFERFLAG_DECODEONLY for buffers that are outside
     *     the segment
     */

    offset += buf->omx_buf->nFilledLen;

    if (offset == minfo.size)
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

    self->started = TRUE;
    err = gst_omx_port_release_buffer (port, buf);
    if (err != OMX_ErrorNone)
      goto release_error;
  }
  gst_buffer_unmap (inbuf, &minfo);
  gst_buffer_unref (inbuf);

  GST_DEBUG_OBJECT (self, "Passed frame to component");

  return self->downstream_flow_ret;

full_buffer:
  {
    gst_buffer_unmap (inbuf, &minfo);
    gst_buffer_unref (inbuf);

    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Got OpenMAX buffer with no free space (%p, %u/%u)", buf,
            (guint) buf->omx_buf->nOffset, (guint) buf->omx_buf->nAllocLen));
    return GST_FLOW_ERROR;
  }

flow_error:
  {
    gst_buffer_unmap (inbuf, &minfo);
    gst_buffer_unref (inbuf);

    return self->downstream_flow_ret;
  }

too_large_codec_data:
  {
    gst_buffer_unmap (inbuf, &minfo);
    gst_buffer_unref (inbuf);

    GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
        ("codec_data larger than supported by OpenMAX port "
            "(%" G_GSIZE_FORMAT " > %u)", gst_buffer_get_size (codec_data),
            (guint) self->dec_in_port->port_def.nBufferSize));
    return GST_FLOW_ERROR;
  }

component_error:
  {
    gst_buffer_unmap (inbuf, &minfo);
    gst_buffer_unref (inbuf);

    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->dec),
            gst_omx_component_get_last_error (self->dec)));
    return GST_FLOW_ERROR;
  }

flushing:
  {
    gst_buffer_unmap (inbuf, &minfo);
    gst_buffer_unref (inbuf);

    GST_DEBUG_OBJECT (self, "Flushing -- returning FLUSHING");
    return GST_FLOW_FLUSHING;
  }
reconfigure_error:
  {
    gst_buffer_unmap (inbuf, &minfo);
    gst_buffer_unref (inbuf);

    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure input port"));
    return GST_FLOW_ERROR;
  }
release_error:
  {
    gst_buffer_unmap (inbuf, &minfo);
    gst_buffer_unref (inbuf);

    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase input buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));

    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_omx_audio_dec_drain (GstOMXAudioDec * self)
{
  GstOMXAudioDecClass *klass;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (self, "Draining component");

  klass = GST_OMX_AUDIO_DEC_GET_CLASS (self);

  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Component not started yet");
    return GST_FLOW_OK;
  }
  self->started = FALSE;

  if ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER)) {
    GST_WARNING_OBJECT (self, "Component does not support empty EOS buffers");
    return GST_FLOW_OK;
  }

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_AUDIO_DECODER_STREAM_UNLOCK (self);

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->dec_in_port, &buf, GST_OMX_WAIT);
  if (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    GST_AUDIO_DECODER_STREAM_LOCK (self);
    GST_ERROR_OBJECT (self, "Failed to acquire buffer for draining: %d",
        acq_ret);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&self->drain_lock);
  self->draining = TRUE;
  buf->omx_buf->nFilledLen = 0;
  GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp,
      gst_util_uint64_scale (self->last_upstream_ts, OMX_TICKS_PER_SECOND,
          GST_SECOND));
  buf->omx_buf->nTickCount = 0;
  buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
  err = gst_omx_port_release_buffer (self->dec_in_port, buf);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to drain component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    g_mutex_unlock (&self->drain_lock);
    GST_AUDIO_DECODER_STREAM_LOCK (self);
    return GST_FLOW_ERROR;
  }

  GST_DEBUG_OBJECT (self, "Waiting until component is drained");

  if (G_UNLIKELY (self->dec->hacks & GST_OMX_HACK_DRAIN_MAY_NOT_RETURN)) {
    gint64 wait_until = g_get_monotonic_time () + G_TIME_SPAN_SECOND / 2;

    if (!g_cond_wait_until (&self->drain_cond, &self->drain_lock, wait_until))
      GST_WARNING_OBJECT (self, "Drain timed out");
    else
      GST_DEBUG_OBJECT (self, "Drained component");

  } else {
    g_cond_wait (&self->drain_cond, &self->drain_lock);
    GST_DEBUG_OBJECT (self, "Drained component");
  }

  g_mutex_unlock (&self->drain_lock);
  GST_AUDIO_DECODER_STREAM_LOCK (self);

  gst_adapter_flush (self->output_adapter,
      gst_adapter_available (self->output_adapter));
  self->started = FALSE;

  return GST_FLOW_OK;
}
