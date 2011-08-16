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

static gboolean gst_omx_audio_enc_start (GstBaseAudioEncoder * encoder);
static gboolean gst_omx_audio_enc_stop (GstBaseAudioEncoder * encoder);
static gboolean gst_omx_audio_enc_set_format (GstBaseAudioEncoder * encoder,
    GstAudioState * state);
static gboolean gst_omx_audio_enc_event (GstBaseAudioEncoder * encoder,
    GstEvent * event);
static GstFlowReturn gst_omx_audio_enc_handle_frame (GstBaseAudioEncoder *
    encoder, GstBuffer * buffer);
static void gst_omx_audio_enc_flush (GstBaseAudioEncoder * encoder);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_omx_audio_enc_debug_category, "omxaudioenc", 0, \
      "debug category for gst-omx audio encoder base class");

GST_BOILERPLATE_FULL (GstOMXAudioEnc, gst_omx_audio_enc, GstBaseAudioEncoder,
    GST_TYPE_BASE_AUDIO_ENCODER, DEBUG_INIT);

static void
gst_omx_audio_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstOMXAudioEncClass *audioenc_class = GST_OMX_AUDIO_ENC_CLASS (g_class);
  GKeyFile *config;
  const gchar *element_name;
  GError *err;
  gchar *core_name, *component_name, *component_role;
  gint in_port_index, out_port_index;
  gchar *template_caps;
  GstPadTemplate *templ;
  GstCaps *caps;
  gchar **hacks;

  element_name =
      g_type_get_qdata (G_TYPE_FROM_CLASS (g_class),
      gst_omx_element_name_quark);
  /* This happens for the base class and abstract subclasses */
  if (!element_name)
    return;

  config = gst_omx_get_configuration ();

  /* This will always succeed, see check in plugin_init */
  core_name = g_key_file_get_string (config, element_name, "core-name", NULL);
  g_assert (core_name != NULL);
  audioenc_class->core_name = core_name;
  component_name =
      g_key_file_get_string (config, element_name, "component-name", NULL);
  g_assert (component_name != NULL);
  audioenc_class->component_name = component_name;

  /* If this fails we simply don't set a role */
  if ((component_role =
          g_key_file_get_string (config, element_name, "component-role",
              NULL))) {
    GST_DEBUG ("Using component-role '%s' for element '%s'", component_role,
        element_name);
    audioenc_class->component_role = component_role;
  }


  /* Now set the inport/outport indizes and assume sane defaults */
  err = NULL;
  in_port_index =
      g_key_file_get_integer (config, element_name, "in-port-index", &err);
  if (err != NULL) {
    GST_DEBUG ("No 'in-port-index' set for element '%s', assuming 0: %s",
        element_name, err->message);
    in_port_index = 0;
    g_error_free (err);
  }
  audioenc_class->in_port_index = in_port_index;

  err = NULL;
  out_port_index =
      g_key_file_get_integer (config, element_name, "out-port-index", &err);
  if (err != NULL) {
    GST_DEBUG ("No 'out-port-index' set for element '%s', assuming 1: %s",
        element_name, err->message);
    out_port_index = 1;
    g_error_free (err);
  }
  audioenc_class->out_port_index = out_port_index;

  /* Add pad templates */
  err = NULL;
  if (!(template_caps =
          g_key_file_get_string (config, element_name, "sink-template-caps",
              &err))) {
    GST_DEBUG
        ("No sink template caps specified for element '%s', using default '%s'",
        element_name, audioenc_class->default_sink_template_caps);
    caps = gst_caps_from_string (audioenc_class->default_sink_template_caps);
    g_assert (caps != NULL);
    g_error_free (err);
  } else {
    caps = gst_caps_from_string (template_caps);
    if (!caps) {
      GST_DEBUG
          ("Could not parse sink template caps '%s' for element '%s', using default '%s'",
          template_caps, element_name,
          audioenc_class->default_sink_template_caps);
      caps = gst_caps_from_string (audioenc_class->default_sink_template_caps);
      g_assert (caps != NULL);
    }
  }
  templ = gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS, caps);
  g_free (template_caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_object_unref (templ);

  err = NULL;
  if (!(template_caps =
          g_key_file_get_string (config, element_name, "src-template-caps",
              &err))) {
    GST_DEBUG
        ("No src template caps specified for element '%s', using default '%s'",
        element_name, audioenc_class->default_src_template_caps);
    caps = gst_caps_from_string (audioenc_class->default_src_template_caps);
    g_assert (caps != NULL);
    g_error_free (err);
  } else {
    caps = gst_caps_from_string (template_caps);
    if (!caps) {
      GST_DEBUG
          ("Could not parse src template caps '%s' for element '%s', using default '%s'",
          template_caps, element_name,
          audioenc_class->default_src_template_caps);
      caps = gst_caps_from_string (audioenc_class->default_src_template_caps);
      g_assert (caps != NULL);
    }
  }
  templ = gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS, caps);
  g_free (template_caps);
  gst_element_class_add_pad_template (element_class, templ);
  gst_object_unref (templ);

  if ((hacks =
          g_key_file_get_string_list (config, element_name, "hacks", NULL,
              NULL))) {
#ifndef GST_DISABLE_GST_DEBUG
    gchar **walk = hacks;

    while (*walk) {
      GST_DEBUG ("Using hack: %s", *walk);
      walk++;
    }
#endif

    audioenc_class->hacks = gst_omx_parse_hacks (hacks);
  }
}

static void
gst_omx_audio_enc_class_init (GstOMXAudioEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseAudioEncoderClass *base_audio_encoder_class =
      GST_BASE_AUDIO_ENCODER_CLASS (klass);

  gobject_class->finalize = gst_omx_audio_enc_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_audio_enc_change_state);

  base_audio_encoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_start);
  base_audio_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_stop);
  base_audio_encoder_class->flush = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_flush);
  base_audio_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_audio_enc_set_format);
  base_audio_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_audio_enc_handle_frame);
  base_audio_encoder_class->event = GST_DEBUG_FUNCPTR (gst_omx_audio_enc_event);

  klass->default_sink_template_caps = "audio/x-raw-int, "
      "rate = (int) [ 1, MAX ], "
      "channels = (int) [ 1, " G_STRINGIFY (OMX_AUDIO_MAXCHANNELS) " ], "
      "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
      "width = (int) 8, "
      "depth = (int) 8, "
      "signed = (boolean) { true, false }; "
      "audio/x-raw-int, "
      "rate = (int) [ 1, MAX ], "
      "channels = (int) [ 1, " G_STRINGIFY (OMX_AUDIO_MAXCHANNELS) " ], "
      "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
      "width = (int) 16, "
      "depth = (int) 16, "
      "signed = (boolean) { true, false }; "
      "audio/x-raw-int, "
      "rate = (int) [ 1, MAX ], "
      "channels = (int) [ 1, " G_STRINGIFY (OMX_AUDIO_MAXCHANNELS) " ], "
      "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
      "width = (int) 24, "
      "depth = (int) 24, "
      "signed = (boolean) { true, false }; "
      "audio/x-raw-int, "
      "rate = (int) [ 1, MAX ], "
      "channels = (int) [ 1, " G_STRINGIFY (OMX_AUDIO_MAXCHANNELS) " ], "
      "endianness = (int) { LITTLE_ENDIAN, BIG_ENDIAN }, "
      "width = (int) 32, "
      "depth = (int) 32, " "signed = (boolean) { true, false }";

}

static void
gst_omx_audio_enc_init (GstOMXAudioEnc * self, GstOMXAudioEncClass * klass)
{
}

static gboolean
gst_omx_audio_enc_open (GstOMXAudioEnc * self)
{
  GstOMXAudioEncClass *klass = GST_OMX_AUDIO_ENC_GET_CLASS (self);

  self->component =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->core_name,
      klass->component_name, klass->component_role, klass->hacks);
  self->started = FALSE;

  if (!self->component)
    return FALSE;

  if (gst_omx_component_get_state (self->component,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  self->in_port =
      gst_omx_component_add_port (self->component, klass->in_port_index);
  self->out_port =
      gst_omx_component_add_port (self->component, klass->out_port_index);

  if (!self->in_port || !self->out_port)
    return FALSE;

  return TRUE;
}

static gboolean
gst_omx_audio_enc_close (GstOMXAudioEnc * self)
{
  OMX_STATETYPE state;

  state = gst_omx_component_get_state (self->component, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    gst_omx_component_set_state (self->component, OMX_StateLoaded);
    gst_omx_port_deallocate_buffers (self->in_port);
    gst_omx_port_deallocate_buffers (self->out_port);
    if (state > OMX_StateLoaded)
      gst_omx_component_get_state (self->component, 5 * GST_SECOND);
  }

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
  /* GstOMXAudioEnc *self = GST_OMX_AUDIO_ENC (object); */

  G_OBJECT_CLASS (parent_class)->finalize (object);
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
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (self->in_port)
        gst_omx_port_set_flushing (self->in_port, TRUE);
      if (self->out_port)
        gst_omx_port_set_flushing (self->out_port, TRUE);
      break;
    default:
      break;
  }

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
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

  if (!GST_PAD_CAPS (GST_BASE_AUDIO_ENCODER_SRC_PAD (self))
      || acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURED) {
    GstAudioState *state = &GST_BASE_AUDIO_ENCODER (self)->ctx->state;
    GstCaps *caps;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    caps = klass->get_caps (self, self->out_port, state);
    if (!caps) {
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      goto caps_failed;
    }

    if (!gst_pad_set_caps (GST_BASE_AUDIO_ENCODER_SRC_PAD (self), caps)) {
      gst_caps_unref (caps);
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      goto caps_failed;
    }
    gst_caps_unref (caps);

    /* Now get a buffer */
    if (acq_return != GST_OMX_ACQUIRE_BUFFER_OK)
      return;
  }

  g_assert (acq_return == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

  GST_DEBUG_OBJECT (self, "Handling buffer: 0x%08x %lu", buf->omx_buf->nFlags,
      buf->omx_buf->nTimeStamp);

  if ((buf->omx_buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG)
      && buf->omx_buf->nFilledLen > 0) {
    GstCaps *caps;
    GstBuffer *codec_data;

    caps = gst_caps_copy (GST_PAD_CAPS (GST_BASE_AUDIO_ENCODER_SRC_PAD (self)));
    codec_data = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);
    memcpy (GST_BUFFER_DATA (codec_data),
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);

    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
    if (!gst_pad_set_caps (GST_BASE_AUDIO_ENCODER_SRC_PAD (self), caps)) {
      gst_caps_unref (caps);
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      goto caps_failed;
    }
    gst_caps_unref (caps);
    flow_ret = GST_FLOW_OK;
  } else if (buf->omx_buf->nFilledLen > 0) {
    GstBuffer *outbuf;

    if (buf->omx_buf->nFilledLen > 0) {
      outbuf = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);

      memcpy (GST_BUFFER_DATA (outbuf),
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);
    } else {
      outbuf = gst_buffer_new ();
    }

    gst_buffer_set_caps (outbuf,
        GST_PAD_CAPS (GST_BASE_AUDIO_ENCODER_SRC_PAD (self)));

    GST_BUFFER_TIMESTAMP (outbuf) =
        gst_util_uint64_scale (buf->omx_buf->nTimeStamp, GST_SECOND,
        OMX_TICKS_PER_SECOND);
    if (buf->omx_buf->nTickCount != 0)
      GST_BUFFER_DURATION (outbuf) =
          gst_util_uint64_scale (buf->omx_buf->nTickCount, GST_SECOND,
          OMX_TICKS_PER_SECOND);

    flow_ret =
        gst_base_audio_encoder_finish_frame (GST_BASE_AUDIO_ENCODER (self),
        outbuf, -1);
  }

  if (flow_ret == GST_FLOW_OK && (buf->omx_buf->nFlags & OMX_BUFFERFLAG_EOS))
    flow_ret = GST_FLOW_UNEXPECTED;

  gst_omx_port_release_buffer (port, buf);

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  return;

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->component),
            gst_omx_component_get_last_error (self->component)));
    gst_pad_push_event (GST_BASE_AUDIO_ENCODER_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_AUDIO_ENCODER_SRC_PAD (self));
    return;
  }
flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_BASE_AUDIO_ENCODER_SRC_PAD (self));
    return;
  }
flow_error:
  {
    if (flow_ret == GST_FLOW_UNEXPECTED) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_BASE_AUDIO_ENCODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_BASE_AUDIO_ENCODER_SRC_PAD (self));
    } else if (flow_ret == GST_FLOW_NOT_LINKED
        || flow_ret < GST_FLOW_UNEXPECTED) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Internal data stream error."),
          ("stream stopped, reason %s", gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_BASE_AUDIO_ENCODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_BASE_AUDIO_ENCODER_SRC_PAD (self));
    }
    return;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_BASE_AUDIO_ENCODER_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_AUDIO_ENCODER_SRC_PAD (self));
    return;
  }
caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_BASE_AUDIO_ENCODER_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_AUDIO_ENCODER_SRC_PAD (self));
    return;
  }
}

static gboolean
gst_omx_audio_enc_start (GstBaseAudioEncoder * encoder)
{
  GstOMXAudioEnc *self;
  gboolean ret;

  self = GST_OMX_AUDIO_ENC (encoder);

  ret =
      gst_pad_start_task (GST_BASE_AUDIO_ENCODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_audio_enc_loop, self);

  return ret;
}

static gboolean
gst_omx_audio_enc_stop (GstBaseAudioEncoder * encoder)
{
  GstOMXAudioEnc *self;

  self = GST_OMX_AUDIO_ENC (encoder);

  gst_pad_stop_task (GST_BASE_AUDIO_ENCODER_SRC_PAD (encoder));

  if (gst_omx_component_get_state (self->component, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->component, OMX_StateIdle);

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  gst_omx_component_get_state (self->component, 5 * GST_SECOND);

  return TRUE;
}

static gboolean
gst_omx_audio_enc_set_format (GstBaseAudioEncoder * encoder,
    GstAudioState * state)
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

  gst_omx_port_get_port_definition (self->in_port, &port_def);

  needs_disable =
      gst_omx_component_get_state (self->component,
      GST_CLOCK_TIME_NONE) != OMX_StateLoaded;
  /* If the component is not in Loaded state and a real format change happens
   * we have to disable the port and re-allocate all buffers. If no real
   * format change happened we can just exit here.
   */
  if (needs_disable) {
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
  pcm_param.nChannels = state->channels;
  pcm_param.eNumData =
      (state->sign ? OMX_NumericalDataSigned : OMX_NumericalDataUnsigned);
  pcm_param.eEndian =
      ((state->endian == G_LITTLE_ENDIAN) ? OMX_EndianLittle : OMX_EndianBig);
  pcm_param.bInterleaved = OMX_TRUE;
  pcm_param.nBitPerSample = state->width;
  pcm_param.nSamplingRate = state->rate;
  pcm_param.ePCMMode = OMX_AUDIO_PCMModeLinear;

  for (i = 0; i < pcm_param.nChannels; i++) {
    OMX_AUDIO_CHANNELTYPE pos;

    switch (state->channel_pos[i]) {
      case GST_AUDIO_CHANNEL_POSITION_FRONT_MONO:
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
      case GST_AUDIO_CHANNEL_POSITION_LFE:
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
    if (!klass->set_format (self, self->in_port, state)) {
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
  gst_pad_start_task (GST_BASE_AUDIO_ENCODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_audio_enc_loop, encoder);

  return (gst_omx_component_get_state (self->component,
          GST_CLOCK_TIME_NONE) == OMX_StateExecuting);
}

static void
gst_omx_audio_enc_flush (GstBaseAudioEncoder * encoder)
{
  GstOMXAudioEnc *self;

  self = GST_OMX_AUDIO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Resetting encoder");

  if (self->started) {
    gst_omx_port_set_flushing (self->in_port, TRUE);
    gst_omx_port_set_flushing (self->out_port, TRUE);

    /* Wait until the srcpad loop is finished */
    GST_PAD_STREAM_LOCK (GST_BASE_AUDIO_ENCODER_SRC_PAD (self));
    GST_PAD_STREAM_UNLOCK (GST_BASE_AUDIO_ENCODER_SRC_PAD (self));

    gst_omx_port_set_flushing (self->in_port, FALSE);
    gst_omx_port_set_flushing (self->out_port, FALSE);
  }

  /* Start the srcpad loop again */
  gst_pad_start_task (GST_BASE_AUDIO_ENCODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_audio_enc_loop, encoder);
}

static GstFlowReturn
gst_omx_audio_enc_handle_frame (GstBaseAudioEncoder * encoder,
    GstBuffer * inbuf)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXAudioEnc *self;
  GstOMXBuffer *buf;
  guint offset = 0;
  GstClockTime timestamp, duration, timestamp_offset = 0;

  self = GST_OMX_AUDIO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Handling frame");

  timestamp = GST_BUFFER_TIMESTAMP (inbuf);
  duration = GST_BUFFER_DURATION (inbuf);

  while (offset < GST_BUFFER_SIZE (inbuf)) {
    acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);

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

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    buf->omx_buf->nFilledLen =
        MIN (GST_BUFFER_SIZE (inbuf) - offset,
        buf->omx_buf->nAllocLen - buf->omx_buf->nOffset);
    memcpy (buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        GST_BUFFER_DATA (inbuf) + offset, buf->omx_buf->nFilledLen);

    /* Interpolate timestamps if we're passing the buffer
     * in multiple chunks */
    if (offset != 0 && duration != GST_CLOCK_TIME_NONE) {
      timestamp_offset =
          gst_util_uint64_scale (offset, duration, GST_BUFFER_SIZE (inbuf));
    }

    if (timestamp != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTimeStamp =
          gst_util_uint64_scale (timestamp + timestamp_offset,
          OMX_TICKS_PER_SECOND, GST_SECOND);
    }
    if (duration != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (buf->omx_buf->nFilledLen, duration,
          GST_BUFFER_SIZE (inbuf));
    }


    offset += buf->omx_buf->nFilledLen;
    self->started = TRUE;
    gst_omx_port_release_buffer (self->in_port, buf);
  }

  return GST_FLOW_OK;

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
    GST_DEBUG_OBJECT (self, "Flushing -- returning WRONG_STATE");
    return GST_FLOW_WRONG_STATE;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure input port"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_omx_audio_enc_event (GstBaseAudioEncoder * encoder, GstEvent * event)
{
  GstOMXAudioEnc *self;

  self = GST_OMX_AUDIO_ENC (encoder);

  if (GST_EVENT_TYPE (event) == GST_EVENT_EOS) {
    GstOMXBuffer *buf;
    GstOMXAcquireBufferReturn acq_ret;

    GST_DEBUG_OBJECT (self, "Sending EOS to the component");

    /* Send an EOS buffer to the component and let the base
     * class drop the EOS event. We will send it later when
     * the EOS buffer arrives on the output port. */
    acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
    if (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK) {
      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
      gst_omx_port_release_buffer (self->in_port, buf);
    }
    return FALSE;
  }

  return TRUE;
}
