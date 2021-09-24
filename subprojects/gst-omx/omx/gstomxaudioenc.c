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

static gboolean gst_omx_audio_enc_open (GstAudioEncoder * encoder);
static gboolean gst_omx_audio_enc_close (GstAudioEncoder * encoder);
static gboolean gst_omx_audio_enc_start (GstAudioEncoder * encoder);
static gboolean gst_omx_audio_enc_stop (GstAudioEncoder * encoder);
static gboolean gst_omx_audio_enc_set_format (GstAudioEncoder * encoder,
    GstAudioInfo * info);
static GstFlowReturn gst_omx_audio_enc_handle_frame (GstAudioEncoder *
    encoder, GstBuffer * buffer);
static void gst_omx_audio_enc_flush (GstAudioEncoder * encoder);

static GstFlowReturn gst_omx_audio_enc_drain (GstOMXAudioEnc * self);

enum
{
  PROP_0
};

/* class initialization */
#define do_init \
{ \
  GST_DEBUG_CATEGORY_INIT (gst_omx_audio_enc_debug_category, "omxaudioenc", 0, \
      "debug category for gst-omx audio encoder base class"); \
  G_IMPLEMENT_INTERFACE (GST_TYPE_PRESET, NULL); \
}

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXAudioEnc, gst_omx_audio_enc,
    GST_TYPE_AUDIO_ENCODER, do_init);

static void
gst_omx_audio_enc_class_init (GstOMXAudioEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstAudioEncoderClass *audio_encoder_class = GST_AUDIO_ENCODER_CLASS (klass);

  gobject_class->finalize = gst_omx_audio_enc_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_audio_enc_change_state);

  audio_encoder_class->open = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_open);
  audio_encoder_class->close = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_close);
  audio_encoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_start);
  audio_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_stop);
  audio_encoder_class->flush = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_flush);
  audio_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_audio_enc_set_format);
  audio_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_audio_enc_handle_frame);

  klass->cdata.type = GST_OMX_COMPONENT_TYPE_FILTER;
  klass->cdata.default_sink_template_caps = "audio/x-raw, "
      "rate = (int) [ 1, MAX ], "
      "channels = (int) [ 1, " G_STRINGIFY (OMX_AUDIO_MAXCHANNELS) " ], "
      "format = (string) { S8, U8, S16LE, S16BE, U16LE, U16BE, "
      "S24LE, S24BE, U24LE, U24BE, S32LE, S32BE, U32LE, U32BE }";
}

static void
gst_omx_audio_enc_init (GstOMXAudioEnc * self)
{
  g_mutex_init (&self->drain_lock);
  g_cond_init (&self->drain_cond);
}

static gboolean
gst_omx_audio_enc_open (GstAudioEncoder * encoder)
{
  GstOMXAudioEnc *self = GST_OMX_AUDIO_ENC (encoder);
  GstOMXAudioEncClass *klass = GST_OMX_AUDIO_ENC_GET_CLASS (self);
  gint in_port_index, out_port_index;

  self->enc =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->cdata.core_name,
      klass->cdata.component_name, klass->cdata.component_role,
      klass->cdata.hacks);
  self->started = FALSE;

  if (!self->enc)
    return FALSE;

  if (gst_omx_component_get_state (self->enc,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  in_port_index = klass->cdata.in_port_index;
  out_port_index = klass->cdata.out_port_index;

  if (in_port_index == -1 || out_port_index == -1) {
    OMX_PORT_PARAM_TYPE param;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&param);

    err =
        gst_omx_component_get_parameter (self->enc, OMX_IndexParamAudioInit,
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

  self->enc_in_port = gst_omx_component_add_port (self->enc, in_port_index);
  self->enc_out_port = gst_omx_component_add_port (self->enc, out_port_index);

  if (!self->enc_in_port || !self->enc_out_port)
    return FALSE;

  return TRUE;
}


static gboolean
gst_omx_audio_enc_shutdown (GstOMXAudioEnc * self)
{
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Shutting down encoder");

  state = gst_omx_component_get_state (self->enc, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateIdle) {
      gst_omx_component_set_state (self->enc, OMX_StateIdle);
      gst_omx_component_get_state (self->enc, 5 * GST_SECOND);
    }
    gst_omx_component_set_state (self->enc, OMX_StateLoaded);
    gst_omx_port_deallocate_buffers (self->enc_in_port);
    gst_omx_port_deallocate_buffers (self->enc_out_port);
    if (state > OMX_StateLoaded)
      gst_omx_component_get_state (self->enc, 5 * GST_SECOND);
  }

  return TRUE;
}

static gboolean
gst_omx_audio_enc_close (GstAudioEncoder * encoder)
{
  GstOMXAudioEnc *self = GST_OMX_AUDIO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Closing encoder");

  if (!gst_omx_audio_enc_shutdown (self))
    return FALSE;

  self->enc_in_port = NULL;
  self->enc_out_port = NULL;
  if (self->enc)
    gst_omx_component_unref (self->enc);
  self->enc = NULL;

  return TRUE;
}

static void
gst_omx_audio_enc_finalize (GObject * object)
{
  GstOMXAudioEnc *self = GST_OMX_AUDIO_ENC (object);

  g_mutex_clear (&self->drain_lock);
  g_cond_clear (&self->drain_cond);

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
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      self->downstream_flow_ret = GST_FLOW_OK;

      self->draining = FALSE;
      self->started = FALSE;
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->enc_in_port)
        gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, TRUE);
      if (self->enc_out_port)
        gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, TRUE);

      g_mutex_lock (&self->drain_lock);
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      g_mutex_unlock (&self->drain_lock);
      break;
    default:
      break;
  }

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
  GstOMXPort *port = self->enc_out_port;
  GstOMXBuffer *buf = NULL;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;
  OMX_ERRORTYPE err;

  klass = GST_OMX_AUDIO_ENC_GET_CLASS (self);

  acq_return = gst_omx_port_acquire_buffer (port, &buf, GST_OMX_WAIT);
  if (acq_return == GST_OMX_ACQUIRE_BUFFER_ERROR) {
    goto component_error;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
    goto flushing;
  } else if (acq_return == GST_OMX_ACQUIRE_BUFFER_EOS) {
    goto eos;
  }

  if (!gst_pad_has_current_caps (GST_AUDIO_ENCODER_SRC_PAD (self))
      || acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
    GstAudioInfo *info =
        gst_audio_encoder_get_audio_info (GST_AUDIO_ENCODER (self));
    GstCaps *caps;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    /* Reallocate all buffers */
    if (acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
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

    GST_AUDIO_ENCODER_STREAM_LOCK (self);

    caps = klass->get_caps (self, self->enc_out_port, info);
    if (!caps) {
      if (buf)
        gst_omx_port_release_buffer (self->enc_out_port, buf);
      GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
      goto caps_failed;
    }

    GST_DEBUG_OBJECT (self, "Setting output caps: %" GST_PTR_FORMAT, caps);

    if (!gst_audio_encoder_set_output_format (GST_AUDIO_ENCODER (self), caps)) {
      gst_caps_unref (caps);
      if (buf)
        gst_omx_port_release_buffer (self->enc_out_port, buf);
      GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
      goto caps_failed;
    }
    gst_caps_unref (caps);

    GST_AUDIO_ENCODER_STREAM_UNLOCK (self);

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
    GST_AUDIO_ENCODER_STREAM_LOCK (self);
    goto eos;
  }

  GST_DEBUG_OBJECT (self, "Handling buffer: 0x%08x %" G_GUINT64_FORMAT,
      (guint) buf->omx_buf->nFlags,
      (guint64) GST_OMX_GET_TICKS (buf->omx_buf->nTimeStamp));

  /* This prevents a deadlock between the srcpad stream
   * lock and the videocodec stream lock, if ::reset()
   * is called at the wrong time
   */
  if (gst_omx_port_is_flushing (self->enc_out_port)) {
    GST_DEBUG_OBJECT (self, "Flushing");
    gst_omx_port_release_buffer (self->enc_out_port, buf);
    goto flushing;
  }

  GST_AUDIO_ENCODER_STREAM_LOCK (self);

  if ((buf->omx_buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG)
      && buf->omx_buf->nFilledLen > 0) {
    GstCaps *caps;
    GstBuffer *codec_data;
    GstMapInfo map = GST_MAP_INFO_INIT;

    GST_DEBUG_OBJECT (self, "Handling codec data");
    caps =
        gst_caps_copy (gst_pad_get_current_caps (GST_AUDIO_ENCODER_SRC_PAD
            (self)));
    codec_data = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);

    gst_buffer_map (codec_data, &map, GST_MAP_WRITE);
    memcpy (map.data,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);
    gst_buffer_unmap (codec_data, &map);

    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
    if (!gst_pad_set_caps (GST_AUDIO_ENCODER_SRC_PAD (self), caps)) {
      gst_caps_unref (caps);
      if (buf)
        gst_omx_port_release_buffer (self->enc_out_port, buf);
      GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
      goto caps_failed;
    }
    gst_caps_unref (caps);
    flow_ret = GST_FLOW_OK;
  } else if (buf->omx_buf->nFilledLen > 0) {
    GstBuffer *outbuf;
    guint n_samples;

    GST_DEBUG_OBJECT (self, "Handling output data");

    n_samples =
        klass->get_num_samples (self, self->enc_out_port,
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
        gst_util_uint64_scale (GST_OMX_GET_TICKS (buf->omx_buf->nTimeStamp),
        GST_SECOND, OMX_TICKS_PER_SECOND);
    if (buf->omx_buf->nTickCount != 0)
      GST_BUFFER_DURATION (outbuf) =
          gst_util_uint64_scale (buf->omx_buf->nTickCount, GST_SECOND,
          OMX_TICKS_PER_SECOND);

    flow_ret =
        gst_audio_encoder_finish_frame (GST_AUDIO_ENCODER (self),
        outbuf, n_samples);
  }

  GST_DEBUG_OBJECT (self, "Handled output data");

  GST_DEBUG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));

  err = gst_omx_port_release_buffer (port, buf);
  if (err != OMX_ErrorNone)
    goto release_error;

  self->downstream_flow_ret = flow_ret;

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  GST_AUDIO_ENCODER_STREAM_UNLOCK (self);

  return;

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->enc),
            gst_omx_component_get_last_error (self->enc)));
    gst_pad_push_event (GST_AUDIO_ENCODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
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
    gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_FLUSHING;
    self->started = FALSE;
    g_mutex_unlock (&self->drain_lock);
    return;
  }
eos:
  {
    g_mutex_lock (&self->drain_lock);
    if (self->draining) {
      GST_DEBUG_OBJECT (self, "Drained");
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      flow_ret = GST_FLOW_OK;
      gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
    } else {
      GST_DEBUG_OBJECT (self, "Component signalled EOS");
      flow_ret = GST_FLOW_EOS;
    }
    g_mutex_unlock (&self->drain_lock);

    GST_AUDIO_ENCODER_STREAM_LOCK (self);
    self->downstream_flow_ret = flow_ret;

    /* Here we fallback and pause the task for the EOS case */
    if (flow_ret != GST_FLOW_OK)
      goto flow_error;

    GST_AUDIO_ENCODER_STREAM_UNLOCK (self);

    return;
  }
flow_error:
  {
    if (flow_ret == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_AUDIO_ENCODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
      self->started = FALSE;
    } else if (flow_ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Internal data stream error."),
          ("stream stopped, reason %s", gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_AUDIO_ENCODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
      self->started = FALSE;
    } else if (flow_ret == GST_FLOW_FLUSHING) {
      GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
      g_mutex_lock (&self->drain_lock);
      if (self->draining) {
        self->draining = FALSE;
        g_cond_broadcast (&self->drain_cond);
      }
      gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
      self->started = FALSE;
      g_mutex_unlock (&self->drain_lock);
    }
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
release_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase output buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    gst_pad_push_event (GST_AUDIO_ENCODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_AUDIO_ENCODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
    return;
  }
}

static gboolean
gst_omx_audio_enc_start (GstAudioEncoder * encoder)
{
  GstOMXAudioEnc *self;

  self = GST_OMX_AUDIO_ENC (encoder);

  self->last_upstream_ts = 0;
  self->downstream_flow_ret = GST_FLOW_OK;

  return TRUE;
}

static gboolean
gst_omx_audio_enc_stop (GstAudioEncoder * encoder)
{
  GstOMXAudioEnc *self;

  self = GST_OMX_AUDIO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Stopping encoder");

  gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, TRUE);

  gst_pad_stop_task (GST_AUDIO_ENCODER_SRC_PAD (encoder));

  if (gst_omx_component_get_state (self->enc, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->enc, OMX_StateIdle);

  self->downstream_flow_ret = GST_FLOW_FLUSHING;
  self->started = FALSE;

  g_mutex_lock (&self->drain_lock);
  self->draining = FALSE;
  g_cond_broadcast (&self->drain_cond);
  g_mutex_unlock (&self->drain_lock);

  gst_omx_component_get_state (self->enc, 5 * GST_SECOND);

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

  gst_omx_port_get_port_definition (self->enc_in_port, &port_def);

  needs_disable =
      gst_omx_component_get_state (self->enc,
      GST_CLOCK_TIME_NONE) != OMX_StateLoaded;
  /* If the component is not in Loaded state and a real format change happens
   * we have to disable the port and re-allocate all buffers. If no real
   * format change happened we can just exit here.
   */
  if (needs_disable) {
    GST_DEBUG_OBJECT (self, "Need to disable and drain encoder");
    gst_omx_audio_enc_drain (self);
    gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, TRUE);

    /* Wait until the srcpad loop is finished,
     * unlock GST_AUDIO_ENCODER_STREAM_LOCK to prevent deadlocks
     * caused by using this lock from inside the loop function */
    GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
    gst_pad_stop_task (GST_AUDIO_ENCODER_SRC_PAD (encoder));
    GST_AUDIO_ENCODER_STREAM_LOCK (self);

    if (klass->cdata.hacks & GST_OMX_HACK_NO_COMPONENT_RECONFIGURE) {
      GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
      gst_omx_audio_enc_stop (GST_AUDIO_ENCODER (self));
      gst_omx_audio_enc_close (GST_AUDIO_ENCODER (self));
      GST_AUDIO_ENCODER_STREAM_LOCK (self);

      if (!gst_omx_audio_enc_open (GST_AUDIO_ENCODER (self)))
        return FALSE;
      needs_disable = FALSE;

      /* The local port_def is now obsolete so get it again. */
      gst_omx_port_get_port_definition (self->enc_in_port, &port_def);
    } else {
      /* Disabling at the same time input port and output port is only
       * required when a buffer is shared between the ports. This cannot
       * be the case for a encoder because its input and output buffers
       * are of different nature. So let's disable ports sequencially.
       * Starting from IL 1.2.0, this point has been clarified.
       * OMX_SendCommand will return an error if the IL client attempts to
       * call it when there is already an on-going command being processed.
       * The exception is for buffer sharing above and the event
       * OMX_EventPortNeedsDisable will be sent to request disabling the
       * other port at the same time. */
      if (gst_omx_port_set_enabled (self->enc_in_port, FALSE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_buffers_released (self->enc_in_port,
              5 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_deallocate_buffers (self->enc_in_port) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_enabled (self->enc_in_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_set_enabled (self->enc_out_port, FALSE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_buffers_released (self->enc_out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_deallocate_buffers (self->enc_out_port) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_wait_enabled (self->enc_out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
    }

    GST_DEBUG_OBJECT (self, "Encoder drained and disabled");
  }

  port_def.format.audio.eEncoding = OMX_AUDIO_CodingPCM;
  GST_DEBUG_OBJECT (self, "Setting inport port definition");
  if (gst_omx_port_update_port_definition (self->enc_in_port,
          &port_def) != OMX_ErrorNone)
    return FALSE;

  GST_OMX_INIT_STRUCT (&pcm_param);
  pcm_param.nPortIndex = self->enc_in_port->index;
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

  GST_DEBUG_OBJECT (self, "Setting PCM parameters");
  err =
      gst_omx_component_set_parameter (self->enc, OMX_IndexParamAudioPcm,
      &pcm_param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set PCM parameters: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    return FALSE;
  }

  if (klass->set_format) {
    if (!klass->set_format (self, self->enc_in_port, info)) {
      GST_ERROR_OBJECT (self, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  GST_DEBUG_OBJECT (self, "Updating outport port definition");
  if (gst_omx_port_update_port_definition (self->enc_out_port,
          NULL) != OMX_ErrorNone)
    return FALSE;

  GST_DEBUG_OBJECT (self, "Enabling component");
  if (needs_disable) {
    if (gst_omx_port_set_enabled (self->enc_in_port, TRUE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_allocate_buffers (self->enc_in_port) != OMX_ErrorNone)
      return FALSE;

    if ((klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      if (gst_omx_port_set_enabled (self->enc_out_port, TRUE) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_allocate_buffers (self->enc_out_port) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->enc_out_port,
              5 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_port_wait_enabled (self->enc_in_port,
            5 * GST_SECOND) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_mark_reconfigured (self->enc_in_port) != OMX_ErrorNone)
      return FALSE;
  } else {
    if (!(klass->cdata.hacks & GST_OMX_HACK_NO_DISABLE_OUTPORT)) {
      /* Disable output port */
      if (gst_omx_port_set_enabled (self->enc_out_port, FALSE) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_wait_enabled (self->enc_out_port,
              1 * GST_SECOND) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_component_set_state (self->enc,
              OMX_StateIdle) != OMX_ErrorNone)
        return FALSE;

      /* Need to allocate buffers to reach Idle state */
      if (gst_omx_port_allocate_buffers (self->enc_in_port) != OMX_ErrorNone)
        return FALSE;
    } else {
      if (gst_omx_component_set_state (self->enc,
              OMX_StateIdle) != OMX_ErrorNone)
        return FALSE;

      /* Need to allocate buffers to reach Idle state */
      if (gst_omx_port_allocate_buffers (self->enc_in_port) != OMX_ErrorNone)
        return FALSE;
      if (gst_omx_port_allocate_buffers (self->enc_out_port) != OMX_ErrorNone)
        return FALSE;
    }

    if (gst_omx_component_get_state (self->enc,
            GST_CLOCK_TIME_NONE) != OMX_StateIdle)
      return FALSE;

    if (gst_omx_component_set_state (self->enc,
            OMX_StateExecuting) != OMX_ErrorNone)
      return FALSE;

    if (gst_omx_component_get_state (self->enc,
            GST_CLOCK_TIME_NONE) != OMX_StateExecuting)
      return FALSE;
  }

  /* Unset flushing to allow ports to accept data again */
  gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, FALSE);

  if (gst_omx_component_get_last_error (self->enc) != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Component in error state: %s (0x%08x)",
        gst_omx_component_get_last_error_string (self->enc),
        gst_omx_component_get_last_error (self->enc));
    return FALSE;
  }

  /* Start the srcpad loop again */
  GST_DEBUG_OBJECT (self, "Starting task again");
  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_AUDIO_ENCODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_audio_enc_loop, encoder, NULL);

  return TRUE;
}

static void
gst_omx_audio_enc_flush (GstAudioEncoder * encoder)
{
  GstOMXAudioEnc *self;

  self = GST_OMX_AUDIO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Resetting encoder");

  gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, TRUE);
  gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, TRUE);

  /* Wait until the srcpad loop is finished */
  GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
  GST_PAD_STREAM_LOCK (GST_AUDIO_ENCODER_SRC_PAD (self));
  GST_PAD_STREAM_UNLOCK (GST_AUDIO_ENCODER_SRC_PAD (self));
  GST_AUDIO_ENCODER_STREAM_LOCK (self);

  gst_omx_port_set_flushing (self->enc_in_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_set_flushing (self->enc_out_port, 5 * GST_SECOND, FALSE);
  gst_omx_port_populate (self->enc_out_port);

  /* Start the srcpad loop again */
  self->last_upstream_ts = 0;
  self->downstream_flow_ret = GST_FLOW_OK;
  self->started = FALSE;
  gst_pad_start_task (GST_AUDIO_ENCODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_audio_enc_loop, encoder, NULL);
}

static GstFlowReturn
gst_omx_audio_enc_handle_frame (GstAudioEncoder * encoder, GstBuffer * inbuf)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXAudioEnc *self;
  GstOMXPort *port;
  GstOMXBuffer *buf;
  gsize size;
  guint offset = 0;
  GstClockTime timestamp, duration, timestamp_offset = 0;
  OMX_ERRORTYPE err;

  self = GST_OMX_AUDIO_ENC (encoder);

  if (self->downstream_flow_ret != GST_FLOW_OK) {
    return self->downstream_flow_ret;
  }

  if (inbuf == NULL)
    return gst_omx_audio_enc_drain (self);

  GST_DEBUG_OBJECT (self, "Handling frame");

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  duration = GST_BUFFER_DURATION (inbuf);

  port = self->enc_in_port;

  size = gst_buffer_get_size (inbuf);
  while (offset < size) {
    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_AUDIO_ENCODER_STREAM_UNLOCK (self);
    acq_ret = gst_omx_port_acquire_buffer (port, &buf, GST_OMX_WAIT);

    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_ERROR) {
      GST_AUDIO_ENCODER_STREAM_LOCK (self);
      goto component_error;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_FLUSHING) {
      GST_AUDIO_ENCODER_STREAM_LOCK (self);
      goto flushing;
    } else if (acq_ret == GST_OMX_ACQUIRE_BUFFER_RECONFIGURE) {
      /* Reallocate all buffers */
      err = gst_omx_port_set_enabled (port, FALSE);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_buffers_released (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_deallocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 1 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_set_enabled (port, TRUE);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_allocate_buffers (port);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_wait_enabled (port, 5 * GST_SECOND);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      err = gst_omx_port_mark_reconfigured (port);
      if (err != OMX_ErrorNone) {
        GST_AUDIO_ENCODER_STREAM_LOCK (self);
        goto reconfigure_error;
      }

      /* Now get a new buffer and fill it */
      GST_AUDIO_ENCODER_STREAM_LOCK (self);
      continue;
    }
    GST_AUDIO_ENCODER_STREAM_LOCK (self);

    g_assert (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

    if (self->downstream_flow_ret != GST_FLOW_OK) {
      gst_omx_port_release_buffer (port, buf);
      return self->downstream_flow_ret;
    }

    if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <= 0) {
      gst_omx_port_release_buffer (port, buf);
      goto full_buffer;
    }

    GST_DEBUG_OBJECT (self, "Handling frame at offset %d", offset);

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
      GST_OMX_SET_TICKS (buf->omx_buf->nTimeStamp,
          gst_util_uint64_scale (timestamp + timestamp_offset,
              OMX_TICKS_PER_SECOND, GST_SECOND));
      self->last_upstream_ts = timestamp + timestamp_offset;
    }
    if (duration != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (buf->omx_buf->nFilledLen, duration, size);
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (buf->omx_buf->nTickCount,
          OMX_TICKS_PER_SECOND, GST_SECOND);
      self->last_upstream_ts += duration;
    }

    offset += buf->omx_buf->nFilledLen;
    self->started = TRUE;
    err = gst_omx_port_release_buffer (port, buf);
    if (err != OMX_ErrorNone)
      goto release_error;
  }

  GST_DEBUG_OBJECT (self, "Passed frame to component");

  return self->downstream_flow_ret;

full_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("Got OpenMAX buffer with no free space (%p, %u/%u)", buf,
            (guint) buf->omx_buf->nOffset, (guint) buf->omx_buf->nAllocLen));
    return GST_FLOW_ERROR;
  }
component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->enc),
            gst_omx_component_get_last_error (self->enc)));
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
release_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Failed to relase input buffer to component: %s (0x%08x)",
            gst_omx_error_to_string (err), err));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_omx_audio_enc_drain (GstOMXAudioEnc * self)
{
  GstOMXAudioEncClass *klass;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;
  OMX_ERRORTYPE err;

  GST_DEBUG_OBJECT (self, "Draining component");

  klass = GST_OMX_AUDIO_ENC_GET_CLASS (self);

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
  GST_AUDIO_ENCODER_STREAM_UNLOCK (self);

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->enc_in_port, &buf, GST_OMX_WAIT);
  if (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    GST_AUDIO_ENCODER_STREAM_LOCK (self);
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
  err = gst_omx_port_release_buffer (self->enc_in_port, buf);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to drain component: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
    g_mutex_unlock (&self->drain_lock);
    GST_AUDIO_ENCODER_STREAM_LOCK (self);
    return GST_FLOW_ERROR;
  }
  GST_DEBUG_OBJECT (self, "Waiting until component is drained");
  g_cond_wait (&self->drain_cond, &self->drain_lock);
  GST_DEBUG_OBJECT (self, "Drained component");
  g_mutex_unlock (&self->drain_lock);
  GST_AUDIO_ENCODER_STREAM_LOCK (self);

  self->started = FALSE;

  return GST_FLOW_OK;
}
