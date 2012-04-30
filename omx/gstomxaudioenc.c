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
#include <string.h>

#include "gstomxaudioenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_audio_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_audio_enc_debug_category

/* prototypes */
static void gst_omx_audio_enc_finalize (GObject * object);

static GstStateChangeReturn
gst_omx_audio_enc_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_omx_audio_enc_start (GstAudioEncoder * encoder);
static gboolean gst_omx_audio_enc_stop (GstAudioEncoder * encoder);
static gboolean gst_omx_audio_enc_set_format (GstAudioEncoder * encoder,
    GstAudioInfo * info);
static gboolean gst_omx_audio_enc_sink_event (GstAudioEncoder * encoder,
    GstEvent * event);
static GstFlowReturn gst_omx_audio_enc_handle_frame (GstAudioEncoder *
    encoder, GstBuffer * buffer);
static void gst_omx_audio_enc_flush (GstAudioEncoder * encoder);

static GstFlowReturn gst_omx_audio_enc_drain (GstOMXAudioEnc * self);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_audio_enc_debug_category, "omxaudioenc", 0, \
      "debug category for gst-omx audio encoder base class");

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXAudioEnc, gst_omx_audio_enc,
    GST_TYPE_AUDIO_ENCODER, DEBUG_INIT);

static void
gst_omx_audio_enc_class_init (GstOMXAudioEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioEncoderClass *audio_encoder_class = GST_AUDIO_ENCODER_CLASS (klass);

  gobject_class->finalize = gst_omx_audio_enc_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_audio_enc_change_state);

  audio_encoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_start);
  audio_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_stop);
  audio_encoder_class->flush = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_flush);
  audio_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_audio_enc_set_format);
  audio_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_audio_enc_handle_frame);
  audio_encoder_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_omx_audio_enc_sink_event);

  klass->cdata.default_sink_template_caps = "audio/x-raw, "
      "rate = (int) [ 1, MAX ], "
      "channels = (int) [ 1, " G_STRINGIFY (OMX_AUDIO_MAXCHANNELS) " ], "
      "format = (string) { S8, U8, S16LE, S16BE, U16LE, U16BE, "
      "S24LE, S24BE, U24LE, U24BE, S32LE, S32BE, U32LE, U32BE }";
}

static void
gst_omx_audio_enc_init (GstOMXAudioEnc * self)
{
  self->drain_lock = g_mutex_new ();
  self->drain_cond = g_cond_new ();
}

static gboolean
gst_omx_audio_enc_open (GstOMXAudioEnc * self)
{
  GstOMXAudioEncClass *klass = GST_OMX_AUDIO_ENC_GET_CLASS (self);

  self->component =
      gst_omx_component_new (GST_OBJECT_CAST (self), &klass->cdata);
  self->started = FALSE;

  if (!self->component)
    return FALSE;

  if (gst_omx_component_get_state (self->component,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  self->in_port =
      gst_omx_component_add_port (self->component, klass->cdata.in_port_index);
  self->out_port =
      gst_omx_component_add_port (self->component, klass->cdata.out_port_index);

  if (!self->in_port || !self->out_port)
    return FALSE;

  return TRUE;
}


static gboolean
gst_omx_audio_enc_shutdown (GstOMXAudioEnc * self)
{
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Shutting down encoder");

  state = gst_omx_component_get_state (self->component, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->component, OMX_StateIdle);
      gst_omx_component_get_state (self->component, 5 * GST_SECOND);
    }
    gst_omx_component_set_state (self->component, OMX_StateLoaded);
    gst_omx_port_deallocate_buffers (self->in_port);
    gst_omx_port_deallocate_buffers (self->out_port);
    if (state > OMX_StateLoaded)
      gst_omx_component_get_state (self->component, 5 * GST_SECOND);
  }

  return TRUE;
}

static gboolean
gst_omx_audio_enc_close (GstOMXAudioEnc * self)
{
  GST_DEBUG_OBJECT (self, "Closing encoder");

  if (!gst_omx_audio_enc_shutdown (self))
    return FALSE;

  self->in_port = NULL;
  self->out_port = NULL;
  if (self->component)
    gst_omx_component_free (self->component);
  self->component = NULL;

  return TRUE;
}

static void
gst_omx_audio_enc_finalize (GObject * object)
{
  GstOMXAudioEnc *self = GST_OMX_AUDIO_ENC (object);

  g_mutex_free (self->drain_lock);
  g_cond_free (self->drain_cond);

  G_OBJECT_CLASS (gst_omx_audio_enc_parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_omx_audio_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstOMXAudioEnc *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_OMX_AUDIO_ENC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_OMX_AUDIO_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_omx_audio_enc_open (self))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (self->in_port)
        gst_omx_port_set_flushing (self->in_port, FALSE);
      if (self->out_port)
        gst_omx_port_set_flushing (self->out_port, FALSE);
      self->downstream_flow_ret = GST_FLOW_OK;

      self->draining = FALSE;
      self->started = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->in_port)
        gst_omx_port_set_flushing (self->in_port, TRUE);
      if (self->out_port)
        gst_omx_port_set_flushing (self->out_port, TRUE);

      g_mutex_lock (self->drain_lock);
      self->draining = FALSE;
      g_cond_broadcast (self->drain_cond);
      g_mutex_unlock (self->drain_lock);
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret =
      GST_ELEMENT_CLASS (gst_omx_audio_enc_parent_class)->change_state (element,
      transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->downstream_flow_ret = GST_FLOW_FLUSHING;
      self->started = FALSE;

      if (!gst_omx_audio_enc_shutdown (self))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_omx_audio_enc_close (self))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_omx_audio_enc_loop (GstOMXAudioEnc * self)
{
  GstOMXAudioEncClass *klass;
  GstOMXPort *port = self->out_port;
  GstOMXBuffer *buf = NULL;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;
  gboolean is_eos;

  klass = GST_OMX_AUDIO_ENC_GET_CLASS (self);

  acq_return = gst_omx_port_acquire_buffer (port, &buf);
  if (acq_return == GST_OMX_ACQUIRE_BUFFER_ERROR) {
    goto component_error;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
    goto flushing;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
    if (gst_omx_port_reconfigure (self->out_port) != OMX_ErrorNone)
      goto reconfigure_error;
    /* And restart the loop */
    return;
  }

  if (!gst_pad_has_current_caps (GST_AUDIO_ENCODER_SRC_PAD (self))
      || acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURED) {
    GstAudioInfo *info =
        gst_audio_encoder_get_audio_info (GST_AUDIO_ENCODER (self));
    GstCaps *caps;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    GST_AUDIO_ENCODER_STREAM_LOCK (self);
    caps = klass->get_caps (self, self->out_port, info);
    if (!caps) {
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
      goto caps_failed;
    }

    if (!gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (self), caps)) {
      gst_caps_unref (caps);
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
      goto caps_failed;
    }
    gst_caps_unref (caps);
    GST_AUDIO_ENCODER_STREAM_UNLOCK (self);

    /* Now get a buffer */
    if (acq_return != GST_OMX_ACQUIRE_BUFFER_OK)
      return;
  }

  g_assert (acq_return == GST_OMX_ACQUIRE_BUFFER_OK);

  if (buf) {

    GST_DEBUG_OBJECT (self, "Handling buffer: 0x%08x %lu", buf->omx_buf->nFlags,
        buf->omx_buf->nTimeStamp);

    /* This prevents a deadlock between the srcpad stream
     * lock and the videocodec stream lock, if ::reset()
     * is called at the wrong time
     */
    if (gst_omx_port_is_flushing (self->out_port)) {
      GST_DEBUG_OBJECT (self, "Flushing");
      gst_omx_port_release_buffer (self->out_port, buf);
      goto flushing;
    }

    GST_AUDIO_ENCODER_STREAM_LOCK (self);
    is_eos = ! !(buf->omx_buf->nFlags & OMX_BUFFERFLAG_EOS);

    if ((buf->omx_buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG)
        && buf->omx_buf->nFilledLen > 0) {
      GstCaps *caps;
      GstBuffer *codec_data;
      GstMapInfo map = GST_MAP_INFO_INIT;

      caps =
          gst_caps_copy (gst_pad_get_current_caps (GST_AUDIO_ENCODER_SRC_PAD
              (self)));
      codec_data = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);

      gst_buffer_map (codec_data, &map, GST_MAP_WRITE);
      memcpy (map.data,
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);
      gst_buffer_unmap (codec_data, &map);

      gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data,
          NULL);
      if (!gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (self), caps)) {
        gst_caps_unref (caps);
        if (buf)
          gst_omx_port_release_buffer (self->out_port, buf);
        GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
        goto caps_failed;
      }
      gst_caps_unref (caps);
      flow_ret = GST_FLOW_OK;
    } else if (buf->omx_buf->nFilledLen > 0) {
      GstBuffer *outbuf;
      guint n_samples;

      n_samples =
          klass->get_num_samples (self, self->out_port,
          gst_audio_encoder_get_audio_info (GST_AUDIO_ENCODER (self)), buf);

      if (buf->omx_buf->nFilledLen > 0) {
        GstMapInfo map = GST_MAP_INFO_INIT;
        outbuf = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);

        gst_buffer_map (outbuf, &map, GST_MAP_WRITE);

        memcpy (map.data,
            buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
            buf->omx_buf->nFilledLen);
        gst_buffer_unmap (outbuf, &map);

      } else {
        outbuf = gst_buffer_new ();
      }

      GST_BUFFER_TIMESTAMP (outbuf) =
          gst_util_uint64_scale (buf->omx_buf->nTimeStamp, GST_SECOND,
          OMX_TICKS_PER_SECOND);
      if (buf->omx_buf->nTickCount != 0)
        GST_BUFFER_DURATION (outbuf) =
            gst_util_uint64_scale (buf->omx_buf->nTickCount, GST_SECOND,
            OMX_TICKS_PER_SECOND);

      flow_ret =
          gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (self),
          outbuf, n_samples);
    }

    if (is_eos || flow_ret == GST_FLOW_EOS) {
      g_mutex_lock (self->drain_lock);
      if (self->draining) {
        GST_DEBUG_OBJECT (self, "Drained");
        self->draining = FALSE;
        g_cond_broadcast (self->drain_cond);
      } else if (flow_ret == GST_FLOW_OK) {
        GST_DEBUG_OBJECT (self, "Component signalled EOS");
        flow_ret = GST_FLOW_EOS;
      }
      g_mutex_unlock (self->drain_lock);
    } else {
      GST_DEBUG_OBJECT (self, "Finished frame: %s",
          gst_flow_get_name (flow_ret));
    }

    gst_omx_port_release_buffer (port, buf);

    self->downstream_flow_ret = flow_ret;
  } else {
    g_assert ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER));
    GST_AUDIO_ENCODER_STREAM_LOCK (self);
    flow_ret = GST_FLOW_EOS;
  }

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  GST_AUDIO_ENCODER_STREAM_UNLOCK (self);

  return;

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->component),
            gst_omx_component_get_last_error (self->component)));
    gst_pad_push_event (GST_AUDIO_ENCODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }
flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_FLUSHING;
    self->started = FALSE;
    return;
  }
flow_error:
  {
    if (flow_ret == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_AUDIO_ENCODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
    } else if (flow_ret == GST_FLOW_NOT_LINKED || flow_ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Internal data stream error."),
          ("stream stopped, reason %s", gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_AUDIO_ENCODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
    }
    self->started = FALSE;
    GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
    return;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_AUDIO_ENCODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    return;
  }
caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_AUDIO_ENCODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    return;
  }
}

static gboolean
gst_omx_audio_enc_start (GstAudioEncoder * encoder)
{
  GstOMXAudioEnc *self;
  gboolean ret;

  self = GST_OMX_AUDIO_ENC (encoder);

  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  ret =
      gst_pad_start_task (GST_AUDIO_ENCODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_audio_enc_loop, self);

  return ret;
}

static gboolean
gst_omx_audio_enc_stop (GstAudioEncoder * encoder)
{
  GstOMXAudioEnc *self;

  self = GST_OMX_AUDIO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Stopping encoder");

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  gst_pad_stop_task (GST_AUDIO_ENCODER_SRC_PAD (encoder));

  if (gst_omx_component_get_state (self->component, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->component, OMX_StateIdle);

  self->downstream_flow_ret = GST_FLOW_FLUSHING;
  self->started = FALSE;
  self->eos = FALSE;

  g_mutex_lock (self->drain_lock);
  self->draining = FALSE;
  g_cond_broadcast (self->drain_cond);
  g_mutex_unlock (self->drain_lock);

  gst_omx_component_get_state (self->component, 5 * GST_SECOND);

  return TRUE;
}

static gboolean
gst_omx_audio_enc_set_format (GstAudioEncoder * encoder, GstAudioInfo * info)
{
  GstOMXAudioEnc *self;
  GstOMXAudioEncClass *klass;
  gboolean needs_disable = FALSE;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_AUDIO_PARAM_PCMMODETYPE pcm_param;
  gint i;
  OMX_ERRORTYPE err;

  self = GST_OMX_AUDIO_ENC (encoder);
  klass = GST_OMX_AUDIO_ENC_GET_CLASS (encoder);

  GST_DEBUG_OBJECT (self, "Setting new caps");

  /* Set audio encoder base class properties */
  gst_audio_encoder_set_frame_samples_min (encoder,
      gst_util_uint64_scale_ceil (OMX_MIN_PCMPAYLOAD_MSEC,
          GST_MSECOND * info->rate, GST_SECOND));
  gst_audio_encoder_set_frame_samples_max (encoder, 0);

  gst_omx_port_get_port_definition (self->in_port, &port_def);

  needs_disable =
      gst_omx_component_get_state (self->component,
      GST_CLOCK_TIME_NONE) != OMX_StateLoaded;
  /* If the component is not in Loaded state and a real format change happens
   * we have to disable the port and re-allocate all buffers. If no real
   * format change happened we can just exit here.
   */
  if (needs_disable) {
    gst_omx_audio_enc_drain (self);

    if (gst_omx_port_manual_reconfigure (self->in_port, TRUE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_set_enabled (self->in_port, FALSE) != OMX_ErrorNone)
      return FALSE;
  }

  port_def.format.audio.eEncoding = OMX_AUDIO_CodingPCM;
  if (!gst_omx_port_update_port_definition (self->in_port, &port_def))
    return FALSE;
  if (!gst_omx_port_update_port_definition (self->out_port, NULL))
    return FALSE;

  GST_OMX_INIT_STRUCT (&pcm_param);
  pcm_param.nPortIndex = self->in_port->index;
  pcm_param.nChannels = info->channels;
  pcm_param.eNumData =
      ((info->finfo->flags & GST_AUDIO_FORMAT_FLAG_SIGNED) ?
      OMX_NumericalDataSigned : OMX_NumericalDataUnsigned);
  pcm_param.eEndian =
      ((info->finfo->endianness == G_LITTLE_ENDIAN) ?
      OMX_EndianLittle : OMX_EndianBig);
  pcm_param.bInterleaved = OMX_TRUE;
  pcm_param.nBitPerSample = info->finfo->width;
  pcm_param.nSamplingRate = info->rate;
  pcm_param.ePCMMode = OMX_AUDIO_PCMModeLinear;

  for (i = 0; i < pcm_param.nChannels; i++) {
    OMX_AUDIO_CHANNELTYPE pos;

    switch (info->position[i]) {
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
    pcm_param.eChannelMapping[i] = pos;
  }

  err =
      gst_omx_component_set_parameter (self->component, OMX_IndexParamAudioPcm,
      &pcm_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set PCM parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  if (klass->set_format) {
    if (!klass->set_format (self, self->in_port, info)) {
      GST_ERROR_OBJECT (self, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  if (needs_disable) {
    if (gst_omx_port_set_enabled (self->in_port, TRUE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_manual_reconfigure (self->in_port, FALSE) != OMX_ErrorNone)
      return FALSE;
  } else {
    if (gst_omx_component_set_state (self->component,
            OMX_StateIdle) != OMX_ErrorNone)
      return FALSE;

    /* Need to allocate buffers to reach Idle state */
    if (gst_omx_port_allocate_buffers (self->in_port) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_allocate_buffers (self->out_port) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_component_get_state (self->component,
            GST_CLOCK_TIME_NONE) != OMX_StateIdle)
      return FALSE;

    if (gst_omx_component_set_state (self->component,
            OMX_StateExecuting) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_component_get_state (self->component,
            GST_CLOCK_TIME_NONE) != OMX_StateExecuting)
      return FALSE;
  }

  /* Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->in_port, FALSE);
  gst_omx_port_set_flushing (self->out_port, FALSE);

  if (gst_omx_component_get_last_error (self->component) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Component in error state: %s (0x%08x)",
        gst_omx_component_get_last_error_string (self->component),
        gst_omx_component_get_last_error (self->component));
    return FALSE;
  }

  /* Start the srcpad loop again */
  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_AUDIO_ENCODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_audio_enc_loop, encoder);

  return TRUE;
}

static void
gst_omx_audio_enc_flush (GstAudioEncoder * encoder)
{
  GstOMXAudioEnc *self;

  self = GST_OMX_AUDIO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Resetting encoder");

  gst_omx_audio_enc_drain (self);

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  /* Wait until the srcpad loop is finished */
  GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
  GST_PAD_STREAM_LOCK (GST_AUDIO_ENCODER_SRC_PAD (self));
  GST_PAD_STREAM_UNLOCK (GST_AUDIO_ENCODER_SRC_PAD (self));
  GST_AUDIO_ENCODER_STREAM_LOCK (self);

  gst_omx_port_set_flushing (self->in_port, FALSE);
  gst_omx_port_set_flushing (self->out_port, FALSE);

  /* Start the srcpad loop again */
  self->last_upstream_ts = 0;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->eos = FALSE;
  gst_pad_start_task (GST_AUDIO_ENCODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_audio_enc_loop, encoder);
}

static GstFlowReturn
gst_omx_audio_enc_handle_frame (GstAudioEncoder * encoder, GstBuffer * inbuf)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXAudioEnc *self;
  GstOMXBuffer *buf;
  gsize size;
  guint offset = 0;
  GstClockTime timestamp, duration, timestamp_offset = 0;

  self = GST_OMX_AUDIO_ENC (encoder);

  if (self->eos) {
    GST_WARNING_OBJECT (self, "Got frame after EOS");
    return GST_FLOW_EOS;
  }

  if (self->downstream_flow_ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Downstream returned %s",
        gst_flow_get_name (self->downstream_flow_ret));

    return self->downstream_flow_ret;
  }

  if (inbuf == NULL)
    return GST_FLOW_OK;

  GST_DEBUG_OBJECT (self, "Handling frame");

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  duration = GST_BUFFER_DURATION (inbuf);

  size = gst_buffer_get_size (inbuf);
  while (offset < size) {
    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
    acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
    GST_AUDIO_ENCODER_STREAM_LOCK (self);

    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_ERROR) {
      goto component_error;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      goto flushing;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      if (gst_omx_port_reconfigure (self->in_port) != OMX_ErrorNone)
        goto reconfigure_error;
      /* Now get a new buffer and fill it */
      continue;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURED) {
      /* TODO: Anything to do here? Don't think so */
      continue;
    }

    g_assert (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

    if (self->downstream_flow_ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Downstream returned %s",
          gst_flow_get_name (self->downstream_flow_ret));

      gst_omx_port_release_buffer (self->in_port, buf);
      return self->downstream_flow_ret;
    }

    if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <= 0) {
      gst_omx_port_release_buffer (self->in_port, buf);
      goto full_buffer;
    }

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    buf->omx_buf->nFilledLen =
        MIN (size - offset, buf->omx_buf->nAllocLen - buf->omx_buf->nOffset);
    gst_buffer_extract (inbuf, offset,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);

    /* Interpolate timestamps if we're passing the buffer
     * in multiple chunks */
    if (offset != 0 && duration != GST_CLOCK_TIME_NONE) {
      timestamp_offset = gst_util_uint64_scale (offset, duration, size);
    }

    if (timestamp != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTimeStamp =
          gst_util_uint64_scale (timestamp + timestamp_offset,
          OMX_TICKS_PER_SECOND, GST_SECOND);
      self->last_upstream_ts = timestamp + timestamp_offset;
    }
    if (duration != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (buf->omx_buf->nFilledLen, duration, size);
      self->last_upstream_ts += duration;
    }

    offset += buf->omx_buf->nFilledLen;
    self->started = TRUE;
    gst_omx_port_release_buffer (self->in_port, buf);
  }

  return self->downstream_flow_ret;

full_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Got OpenMAX buffer with no free space (%p, %u/%u)", buf,
            buf->omx_buf->nOffset, buf->omx_buf->nAllocLen));
    return GST_FLOW_ERROR;
  }
component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->component),
            gst_omx_component_get_last_error (self->component)));
    return GST_FLOW_ERROR;
  }

flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- returning FLUSHING");
    return GST_FLOW_FLUSHING;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure input port"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_omx_audio_enc_sink_event (GstAudioEncoder * encoder, GstEvent * event)
{
  GstOMXAudioEnc *self;
  GstOMXAudioEncClass *klass;

  self = GST_OMX_AUDIO_ENC (encoder);
  klass = GST_OMX_AUDIO_ENC_GET_CLASS (self);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GstOMXBuffer *buf;
    GstOMXAcquireBufferReturn acq_ret;

    GST_DEBUG_OBJECT (self, "Sending EOS to the component");

    /* Don't send EOS buffer twice, this doesn't work */
    if (self->eos) {
      GST_DEBUG_OBJECT (self, "Component is already EOS");
      return TRUE;
    }
    self->eos = TRUE;

    if ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER)) {
      GST_WARNING_OBJECT (self, "Component does not support empty EOS buffers");

      /* Insert a NULL into the queue to signal EOS */
      gst_omx_rec_mutex_lock (&self->out_port->port_lock);
      g_queue_push_tail (self->out_port->pending_buffers, NULL);
      g_cond_broadcast (self->out_port->port_cond);
      gst_omx_rec_mutex_unlock (&self->out_port->port_lock);
      return TRUE;
    }

    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_AUDIO_ENCODER_STREAM_UNLOCK (self);

    /* Send an EOS buffer to the component and let the base
     * class drop the EOS event. We will send it later when
     * the EOS buffer arrives on the output port. */
    acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK) {
      buf->omx_buf->nFilledLen = 0;
      buf->omx_buf->nTimeStamp =
          gst_util_uint64_scale (self->last_upstream_ts, OMX_TICKS_PER_SECOND,
          GST_SECOND);
      buf->omx_buf->nTickCount = 0;
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
      gst_omx_port_release_buffer (self->in_port, buf);
      GST_DEBUG_OBJECT (self, "Sent EOS to the component");
    } else {
      GST_ERROR_OBJECT (self, "Failed to acquire buffer for EOS: %d", acq_ret);
    }

    GST_AUDIO_ENCODER_STREAM_LOCK (self);

    return TRUE;
  }

  return FALSE;
}

static GstFlowReturn
gst_omx_audio_enc_drain (GstOMXAudioEnc * self)
{
  GstOMXAudioEncClass *klass;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;

  GST_DEBUG_OBJECT (self, "Draining component");

  klass = GST_OMX_AUDIO_ENC_GET_CLASS (self);

  if (!self->started) {
    GST_DEBUG_OBJECT (self, "Component not started yet");
    return GST_FLOW_OK;
  }
  self->started = FALSE;

  /* Don't send EOS buffer twice, this doesn't work */
  if (self->eos) {
    GST_DEBUG_OBJECT (self, "Component is EOS already");
    return GST_FLOW_OK;
  }

  if ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER)) {
    GST_WARNING_OBJECT (self, "Component does not support empty EOS buffers");
    return GST_FLOW_OK;
  }

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_AUDIO_ENCODER_STREAM_UNLOCK (self);

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
  if (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    GST_AUDIO_ENCODER_STREAM_LOCK (self);
    GST_ERROR_OBJECT (self, "Failed to acquire buffer for draining: %d",
        acq_ret);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (self->drain_lock);
  self->draining = TRUE;
  buf->omx_buf->nFilledLen = 0;
  buf->omx_buf->nTimeStamp =
      gst_util_uint64_scale (self->last_upstream_ts, OMX_TICKS_PER_SECOND,
      GST_SECOND);
  buf->omx_buf->nTickCount = 0;
  buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
  gst_omx_port_release_buffer (self->in_port, buf);
  GST_DEBUG_OBJECT (self, "Waiting until component is drained");
  g_cond_wait (self->drain_cond, self->drain_lock);
  GST_DEBUG_OBJECT (self, "Drained component");
  g_mutex_unlock (self->drain_lock);
  GST_AUDIO_ENCODER_STREAM_LOCK (self);

  self->started = FALSE;

  return GST_FLOW_OK;
}
