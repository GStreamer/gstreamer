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

#include "gstomxvideoenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_video_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_video_enc_debug_category

#define GST_TYPE_OMX_VIDEO_ENC_CONTROL_RATE (gst_omx_video_enc_control_rate_get_type ())
static GType
gst_omx_video_enc_control_rate_get_type (void)
{
  static GType qtype = 0;

  if (qtype == 0) {
    static const GEnumValue values[] = {
      {OMX_Video_ControlRateDisable, "Disable", "disable"},
      {OMX_Video_ControlRateVariable, "Variable", "variable"},
      {OMX_Video_ControlRateConstant, "Constant", "constant"},
      {OMX_Video_ControlRateVariableSkipFrames, "Variable Skip Frames",
          "variable-skip-frames"},
      {OMX_Video_ControlRateConstantSkipFrames, "Constant Skip Frames",
          "constant-skip-frames"},
      {0, NULL, NULL}
    };

    qtype = g_enum_register_static ("GstOMXVideoEncControlRate", values);
  }
  return qtype;
}

typedef struct _BufferIdentification BufferIdentification;
struct _BufferIdentification
{
  guint64 timestamp;
};

static void
buffer_identification_free (BufferIdentification * id)
{
  g_slice_free (BufferIdentification, id);
}

/* prototypes */
static void gst_omx_video_enc_finalize (GObject * object);
static void gst_omx_video_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_video_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);


static GstStateChangeReturn
gst_omx_video_enc_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_omx_video_enc_start (GstBaseVideoEncoder * encoder);
static gboolean gst_omx_video_enc_stop (GstBaseVideoEncoder * encoder);
static gboolean gst_omx_video_enc_set_format (GstBaseVideoEncoder * encoder,
    GstVideoState * state);
static gboolean gst_omx_video_enc_reset (GstBaseVideoEncoder * encoder);
static GstFlowReturn gst_omx_video_enc_handle_frame (GstBaseVideoEncoder *
    encoder, GstVideoFrame * frame);
static gboolean gst_omx_video_enc_finish (GstBaseVideoEncoder * encoder);

enum
{
  PROP_0,
  PROP_CONTROL_RATE,
  PROP_TARGET_BITRATE,
  PROP_QUANT_I_FRAMES,
  PROP_QUANT_P_FRAMES,
  PROP_QUANT_B_FRAMES
};

/* FIXME: Better defaults */
#define GST_OMX_VIDEO_ENC_CONTROL_RATE_DEFAULT (OMX_Video_ControlRateConstant)
#define GST_OMX_VIDEO_ENC_TARGET_BITRATE_DEFAULT (64000)
#define GST_OMX_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT (9)
#define GST_OMX_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT (6)
#define GST_OMX_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT (2)

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_omx_video_enc_debug_category, "omxvideoenc", 0, \
      "debug category for gst-omx video encoder base class");

GST_BOILERPLATE_FULL (GstOMXVideoEnc, gst_omx_video_enc, GstBaseVideoEncoder,
    GST_TYPE_BASE_VIDEO_ENCODER, DEBUG_INIT);

static void
gst_omx_video_enc_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (g_class);
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
  videoenc_class->core_name = core_name;
  component_name =
      g_key_file_get_string (config, element_name, "component-name", NULL);
  g_assert (component_name != NULL);
  videoenc_class->component_name = component_name;

  /* If this fails we simply don't set a role */
  if ((component_role =
          g_key_file_get_string (config, element_name, "component-role",
              NULL))) {
    GST_DEBUG ("Using component-role '%s' for element '%s'", component_role,
        element_name);
    videoenc_class->component_role = component_role;
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
  videoenc_class->in_port_index = in_port_index;

  err = NULL;
  out_port_index =
      g_key_file_get_integer (config, element_name, "out-port-index", &err);
  if (err != NULL) {
    GST_DEBUG ("No 'out-port-index' set for element '%s', assuming 1: %s",
        element_name, err->message);
    out_port_index = 1;
    g_error_free (err);
  }
  videoenc_class->out_port_index = out_port_index;

  /* Add pad templates */
  err = NULL;
  if (!(template_caps =
          g_key_file_get_string (config, element_name, "sink-template-caps",
              &err))) {
    GST_DEBUG
        ("No sink template caps specified for element '%s', using default '%s'",
        element_name, videoenc_class->default_sink_template_caps);
    caps = gst_caps_from_string (videoenc_class->default_sink_template_caps);
    g_assert (caps != NULL);
    g_error_free (err);
  } else {
    caps = gst_caps_from_string (template_caps);
    if (!caps) {
      GST_DEBUG
          ("Could not parse sink template caps '%s' for element '%s', using default '%s'",
          template_caps, element_name,
          videoenc_class->default_sink_template_caps);
      caps = gst_caps_from_string (videoenc_class->default_sink_template_caps);
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
        element_name, videoenc_class->default_src_template_caps);
    caps = gst_caps_from_string (videoenc_class->default_src_template_caps);
    g_assert (caps != NULL);
    g_error_free (err);
  } else {
    caps = gst_caps_from_string (template_caps);
    if (!caps) {
      GST_DEBUG
          ("Could not parse src template caps '%s' for element '%s', using default '%s'",
          template_caps, element_name,
          videoenc_class->default_src_template_caps);
      caps = gst_caps_from_string (videoenc_class->default_src_template_caps);
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

    videoenc_class->hacks = gst_omx_parse_hacks (hacks);
  }
}

static void
gst_omx_video_enc_class_init (GstOMXVideoEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseVideoEncoderClass *base_video_encoder_class =
      GST_BASE_VIDEO_ENCODER_CLASS (klass);

  gobject_class->finalize = gst_omx_video_enc_finalize;
  gobject_class->set_property = gst_omx_video_enc_set_property;
  gobject_class->get_property = gst_omx_video_enc_get_property;

  g_object_class_install_property (gobject_class, PROP_CONTROL_RATE,
      g_param_spec_enum ("control-rate", "Control Rate",
          "Bitrate control method",
          GST_TYPE_OMX_VIDEO_ENC_CONTROL_RATE,
          GST_OMX_VIDEO_ENC_CONTROL_RATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_TARGET_BITRATE,
      g_param_spec_uint ("target-bitrate", "Target Bitrate",
          "Target bitrate",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_TARGET_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class, PROP_QUANT_I_FRAMES,
      g_param_spec_uint ("quant-i-frames", "I-Frame Quantization",
          "Quantization parameter for I-frames",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_QUANT_P_FRAMES,
      g_param_spec_uint ("quant-p-frames", "P-Frame Quantization",
          "Quantization parameter for P-frames",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_QUANT_B_FRAMES,
      g_param_spec_uint ("quant-b-frames", "B-Frame Quantization",
          "Quantization parameter for B-frames",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_change_state);

  base_video_encoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_video_enc_start);
  base_video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_video_enc_stop);
  base_video_encoder_class->reset = GST_DEBUG_FUNCPTR (gst_omx_video_enc_reset);
  base_video_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_set_format);
  base_video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_handle_frame);
  base_video_encoder_class->finish =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_finish);
}

static void
gst_omx_video_enc_init (GstOMXVideoEnc * self, GstOMXVideoEncClass * klass)
{
  self->control_rate = GST_OMX_VIDEO_ENC_CONTROL_RATE_DEFAULT;
  self->target_bitrate = GST_OMX_VIDEO_ENC_TARGET_BITRATE_DEFAULT;
  self->quant_i_frames = GST_OMX_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT;
  self->quant_p_frames = GST_OMX_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT;
  self->quant_b_frames = GST_OMX_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT;
}

static gboolean
gst_omx_video_enc_open (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

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

  /* Set properties */
  {
    OMX_VIDEO_PARAM_BITRATETYPE bitrate_param;
    OMX_VIDEO_PARAM_QUANTIZATIONTYPE quant_param;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&bitrate_param);
    bitrate_param.nPortIndex = self->out_port->index;
    bitrate_param.eControlRate = self->control_rate;
    bitrate_param.nTargetBitrate = self->target_bitrate;

    err =
        gst_omx_component_set_parameter (self->component,
        OMX_IndexParamVideoBitrate, &bitrate_param);
    if (err == OMX_ErrorUnsupportedIndex) {
      GST_WARNING_OBJECT (self,
          "Setting a bitrate not supported by the component");
      goto done;
    } else if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to set bitrate parameters: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      return FALSE;
    }

    GST_OMX_INIT_STRUCT (&quant_param);
    quant_param.nPortIndex = self->out_port->index;
    quant_param.nQpI = self->quant_i_frames;
    quant_param.nQpP = self->quant_p_frames;
    quant_param.nQpB = self->quant_b_frames;

    err =
        gst_omx_component_set_parameter (self->component,
        OMX_IndexParamVideoQuantization, &quant_param);
    if (err == OMX_ErrorUnsupportedIndex) {
      GST_WARNING_OBJECT (self,
          "Setting quantization parameters not supported by the component");
      goto done;
    } else if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self,
          "Failed to set quantization parameters: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      return FALSE;
    }
  }
done:

  return TRUE;
}

static gboolean
gst_omx_video_enc_close (GstOMXVideoEnc * self)
{
  OMX_STATETYPE state;

  state = gst_omx_component_get_state (self->component, 0);
  if (state > OMX_StateLoaded || state == OMX_StateInvalid) {
    if (state > OMX_StateLoaded)
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
gst_omx_video_enc_finalize (GObject * object)
{
  /* GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (object); */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_omx_video_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_CONTROL_RATE:
      self->control_rate = g_value_get_enum (value);
      break;
    case PROP_TARGET_BITRATE:
      self->target_bitrate = g_value_get_uint (value);
      if (self->component) {
        OMX_VIDEO_CONFIG_BITRATETYPE config;
        OMX_ERRORTYPE err;

        GST_OMX_INIT_STRUCT (&config);
        config.nPortIndex = self->out_port->index;
        config.nEncodeBitrate = self->target_bitrate;
        err =
            gst_omx_component_set_config (self->component,
            OMX_IndexConfigVideoBitrate, &config);
        if (err != OMX_ErrorNone)
          GST_ERROR_OBJECT (self,
              "Failed to set bitrate parameter: %s (0x%08x)",
              gst_omx_error_to_string (err), err);
      }
      break;
    case PROP_QUANT_I_FRAMES:
      self->quant_i_frames = g_value_get_uint (value);
      break;
    case PROP_QUANT_P_FRAMES:
      self->quant_p_frames = g_value_get_uint (value);
      break;
    case PROP_QUANT_B_FRAMES:
      self->quant_b_frames = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_video_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_CONTROL_RATE:
      g_value_set_enum (value, self->control_rate);
      break;
    case PROP_TARGET_BITRATE:
      g_value_set_uint (value, self->target_bitrate);
      break;
    case PROP_QUANT_I_FRAMES:
      g_value_set_uint (value, self->quant_i_frames);
      break;
    case PROP_QUANT_P_FRAMES:
      g_value_set_uint (value, self->quant_p_frames);
      break;
    case PROP_QUANT_B_FRAMES:
      g_value_set_uint (value, self->quant_b_frames);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static GstStateChangeReturn
gst_omx_video_enc_change_state (GstElement * element, GstStateChange transition)
{
  GstOMXVideoEnc *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_OMX_VIDEO_ENC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_OMX_VIDEO_ENC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_omx_video_enc_open (self))
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
      if (!gst_omx_video_enc_close (self))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    default:
      break;
  }

  return ret;
}

#define MAX_FRAME_DIST_TICKS  (5 * OMX_TICKS_PER_SECOND)
#define MAX_FRAME_DIST_FRAMES (100)

static GstVideoFrame *
_find_nearest_frame (GstOMXVideoEnc * self, GstOMXBuffer * buf)
{
  GList *l, *best_l = NULL;
  GList *finish_frames = NULL;
  GstVideoFrame *best = NULL;
  guint64 best_timestamp = 0;
  guint64 best_diff = G_MAXUINT64;
  BufferIdentification *best_id = NULL;

  GST_OBJECT_LOCK (self);
  for (l = GST_BASE_VIDEO_CODEC (self)->frames; l; l = l->next) {
    GstVideoFrame *tmp = l->data;
    BufferIdentification *id = tmp->coder_hook;
    guint64 timestamp, diff;

    /* This happens for frames that were just added but
     * which were not passed to the component yet. Ignore
     * them here!
     */
    if (!id)
      continue;

    timestamp = id->timestamp;

    if (timestamp > buf->omx_buf->nTimeStamp)
      diff = timestamp - buf->omx_buf->nTimeStamp;
    else
      diff = buf->omx_buf->nTimeStamp - timestamp;

    if (best == NULL || diff < best_diff) {
      best = tmp;
      best_timestamp = timestamp;
      best_diff = diff;
      best_l = l;
      best_id = id;

      /* For frames without timestamp we simply take the first frame */
      if ((buf->omx_buf->nTimeStamp == 0 && timestamp == 0) || diff == 0)
        break;
    }
  }

  if (best_id) {
    for (l = GST_BASE_VIDEO_CODEC (self)->frames; l && l != best_l; l = l->next) {
      GstVideoFrame *tmp = l->data;
      BufferIdentification *id = tmp->coder_hook;
      guint64 diff_ticks, diff_frames;

      if (id->timestamp > best_timestamp)
        break;

      if (id->timestamp == 0 || best_timestamp == 0)
        diff_ticks = 0;
      else
        diff_ticks = best_timestamp - id->timestamp;
      diff_frames = best->system_frame_number - tmp->system_frame_number;

      if (diff_ticks > MAX_FRAME_DIST_TICKS
          || diff_frames > MAX_FRAME_DIST_FRAMES) {
        finish_frames = g_list_prepend (finish_frames, tmp);
      }
    }
  }

  GST_OBJECT_UNLOCK (self);

  if (finish_frames) {
    g_warning ("Too old frames, bug in encoder -- please file a bug");
    for (l = finish_frames; l; l = l->next) {
      gst_base_video_encoder_finish_frame (GST_BASE_VIDEO_ENCODER (self),
          l->data);
    }
  }

  return best;
}

static void
gst_omx_video_enc_loop (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass;
  GstOMXPort *port = self->out_port;
  GstOMXBuffer *buf = NULL;
  GstVideoFrame *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;

  klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

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

  if (!GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (self))
      || acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURED) {
    GstVideoState *state = &GST_BASE_VIDEO_CODEC (self)->state;
    GstCaps *caps;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    caps = klass->get_caps (self, self->out_port, state);
    if (!caps) {
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      goto caps_failed;
    }

    if (!gst_pad_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (self), caps)) {
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

  frame = _find_nearest_frame (self, buf);
  if ((buf->omx_buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG)
      && buf->omx_buf->nFilledLen > 0) {
    GstCaps *caps;
    GstBuffer *codec_data;

    caps = gst_caps_copy (GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (self)));
    codec_data = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);
    memcpy (GST_BUFFER_DATA (codec_data),
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);

    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
    if (!gst_pad_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (self), caps)) {
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
        GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (self)));

    GST_BUFFER_TIMESTAMP (outbuf) =
        gst_util_uint64_scale (buf->omx_buf->nTimeStamp, GST_SECOND,
        OMX_TICKS_PER_SECOND);
    if (buf->omx_buf->nTickCount != 0)
      GST_BUFFER_DURATION (outbuf) =
          gst_util_uint64_scale (buf->omx_buf->nTickCount, GST_SECOND,
          OMX_TICKS_PER_SECOND);

    if ((klass->hacks & GST_OMX_HACK_SYNCFRAME_FLAG_NOT_USED)
        || (buf->omx_buf->nFlags & OMX_BUFFERFLAG_SYNCFRAME)) {
      if (frame)
        frame->is_sync_point = TRUE;
      else
        GST_BUFFER_FLAG_UNSET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    } else {
      if (frame)
        frame->is_sync_point = FALSE;
      else
        GST_BUFFER_FLAG_SET (outbuf, GST_BUFFER_FLAG_DELTA_UNIT);
    }

    if (frame) {
      frame->src_buffer = outbuf;
      flow_ret =
          gst_base_video_encoder_finish_frame (GST_BASE_VIDEO_ENCODER (self),
          frame);
    } else {
      GST_ERROR_OBJECT (self, "No corresponding frame found");
      flow_ret = gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (self), outbuf);
    }
  } else if (frame != NULL) {
    flow_ret =
        gst_base_video_encoder_finish_frame (GST_BASE_VIDEO_ENCODER (self),
        frame);
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
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    return;
  }
flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    return;
  }
flow_error:
  {
    if (flow_ret == GST_FLOW_UNEXPECTED) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    } else if (flow_ret == GST_FLOW_NOT_LINKED
        || flow_ret < GST_FLOW_UNEXPECTED) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Internal data stream error."),
          ("stream stopped, reason %s", gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    }
    return;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    return;
  }
caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    return;
  }
}

static gboolean
gst_omx_video_enc_start (GstBaseVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;
  gboolean ret;

  self = GST_OMX_VIDEO_ENC (encoder);

  ret =
      gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_enc_loop, self);

  return ret;
}

static gboolean
gst_omx_video_enc_stop (GstBaseVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;

  self = GST_OMX_VIDEO_ENC (encoder);

  gst_pad_stop_task (GST_BASE_VIDEO_CODEC_SRC_PAD (encoder));

  if (gst_omx_component_get_state (self->component, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->component, OMX_StateIdle);

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  gst_omx_component_get_state (self->component, 5 * GST_SECOND);

  return TRUE;
}

static gboolean
gst_omx_video_enc_set_format (GstBaseVideoEncoder * encoder,
    GstVideoState * state)
{
  GstOMXVideoEnc *self;
  GstOMXVideoEncClass *klass;
  gboolean needs_disable = FALSE;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;

  self = GST_OMX_VIDEO_ENC (encoder);
  klass = GST_OMX_VIDEO_ENC_GET_CLASS (encoder);

  GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, state->caps);

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

  switch (state->format) {
    case GST_VIDEO_FORMAT_I420:
      port_def.format.video.eColorFormat = OMX_COLOR_FormatYUV420Planar;
      break;
    case GST_VIDEO_FORMAT_NV12:
      port_def.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
      break;
    default:
      GST_ERROR_OBJECT (self, "Unsupported caps %" GST_PTR_FORMAT, state->caps);
      return FALSE;
      break;
  }
  port_def.format.video.nFrameWidth = state->width;
  port_def.format.video.nFrameHeight = state->height;
  if (state->fps_n == 0) {
    port_def.format.video.xFramerate = 0;
  } else {
    if (!(klass->hacks & GST_OMX_HACK_VIDEO_FRAMERATE_INTEGER))
      port_def.format.video.xFramerate = (state->fps_n << 16) / (state->fps_d);
    else
      port_def.format.video.xFramerate = (state->fps_n) / (state->fps_d);
  }

  if (!gst_omx_port_update_port_definition (self->in_port, &port_def))
    return FALSE;
  if (!gst_omx_port_update_port_definition (self->out_port, NULL))
    return FALSE;

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
  gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_enc_loop, encoder);

  return (gst_omx_component_get_state (self->component,
          GST_CLOCK_TIME_NONE) == OMX_StateExecuting);
}

static gboolean
gst_omx_video_enc_reset (GstBaseVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;

  self = GST_OMX_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Resetting encoder");

  /* FIXME: Workaround for 
   * https://bugzilla.gnome.org/show_bug.cgi?id=654529
   */
  GST_OBJECT_LOCK (self);
  g_list_foreach (GST_BASE_VIDEO_CODEC (self)->frames,
      (GFunc) gst_base_video_codec_free_frame, NULL);
  g_list_free (GST_BASE_VIDEO_CODEC (self)->frames);
  GST_BASE_VIDEO_CODEC (self)->frames = NULL;
  GST_OBJECT_UNLOCK (self);

  if (self->started) {
    gst_omx_port_set_flushing (self->in_port, TRUE);
    gst_omx_port_set_flushing (self->out_port, TRUE);

    /* Wait until the srcpad loop is finished */
    GST_PAD_STREAM_LOCK (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    GST_PAD_STREAM_UNLOCK (GST_BASE_VIDEO_CODEC_SRC_PAD (self));

    gst_omx_port_set_flushing (self->in_port, FALSE);
    gst_omx_port_set_flushing (self->out_port, FALSE);
  }

  /* Start the srcpad loop again */
  gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_enc_loop, encoder);

  return TRUE;
}

static gboolean
gst_omx_video_enc_fill_buffer (GstOMXVideoEnc * self, GstBuffer * inbuf,
    GstOMXBuffer * outbuf)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (self)->state;
  OMX_PARAM_PORTDEFINITIONTYPE *port_def = &self->in_port->port_def;
  gboolean ret = FALSE;

  if (state->width != port_def->format.video.nFrameWidth ||
      state->height != port_def->format.video.nFrameHeight) {
    GST_ERROR_OBJECT (self, "Width or height do not match");
    goto done;
  }

  /* Same strides and everything */
  if (GST_BUFFER_SIZE (inbuf) == outbuf->omx_buf->nAllocLen) {
    outbuf->omx_buf->nFilledLen = outbuf->omx_buf->nAllocLen;
    memcpy (outbuf->omx_buf->pBuffer + outbuf->omx_buf->nOffset,
        GST_BUFFER_DATA (inbuf), outbuf->omx_buf->nFilledLen);
    ret = TRUE;
    goto done;
  }

  /* Different strides */
  switch (state->format) {
    case GST_VIDEO_FORMAT_I420:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride;

      outbuf->omx_buf->nFilledLen = 0;

      for (i = 0; i < 3; i++) {
        if (i == 0) {
          dest_stride = port_def->format.video.nStride;
          src_stride =
              gst_video_format_get_row_stride (state->format, 0, state->width);
        } else {
          dest_stride = port_def->format.video.nStride / 2;
          src_stride =
              gst_video_format_get_row_stride (state->format, 1, state->width);
        }

        dest = outbuf->omx_buf->pBuffer + outbuf->omx_buf->nOffset;
        if (i > 0)
          dest +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;
        if (i == 2)
          dest +=
              (port_def->format.video.nSliceHeight / 2) *
              (port_def->format.video.nStride / 2);

        src =
            GST_BUFFER_DATA (inbuf) +
            gst_video_format_get_component_offset (state->format, i,
            state->width, state->height);

        height =
            gst_video_format_get_component_height (state->format, i,
            state->height);

        for (j = 0; j < height; j++) {
          memcpy (dest, src, MIN (src_stride, dest_stride));
          outbuf->omx_buf->nFilledLen += dest_stride;
          src += src_stride;
          dest += dest_stride;
        }
      }
      ret = TRUE;
      break;
    }
    case GST_VIDEO_FORMAT_NV12:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride;

      outbuf->omx_buf->nFilledLen = 0;

      for (i = 0; i < 2; i++) {
        if (i == 0) {
          dest_stride = port_def->format.video.nStride;
          src_stride =
              gst_video_format_get_row_stride (state->format, 0, state->width);
        } else {
          dest_stride = port_def->format.video.nStride;
          src_stride =
              gst_video_format_get_row_stride (state->format, 1, state->width);
        }

        dest = outbuf->omx_buf->pBuffer + outbuf->omx_buf->nOffset;
        if (i == 1)
          dest +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;

        src =
            GST_BUFFER_DATA (inbuf) +
            gst_video_format_get_component_offset (state->format, i,
            state->width, state->height);

        height =
            gst_video_format_get_component_height (state->format, i,
            state->height);
        for (j = 0; j < height; j++) {
          memcpy (dest, src, MIN (src_stride, dest_stride));
          outbuf->omx_buf->nFilledLen += dest_stride;
          src += src_stride;
          dest += dest_stride;
        }
      }
      ret = TRUE;
      break;
    }
    default:
      GST_ERROR_OBJECT (self, "Unsupported format");
      goto done;
      break;
  }

done:
  return ret;
}

static GstFlowReturn
gst_omx_video_enc_handle_frame (GstBaseVideoEncoder * encoder,
    GstVideoFrame * frame)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXVideoEnc *self;
  GstOMXBuffer *buf;

  self = GST_OMX_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Handling frame");

  while (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    BufferIdentification *id;
    GstClockTime timestamp, duration;

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

    /* Now handle the frame */
    if (frame->force_keyframe) {
      OMX_ERRORTYPE err;
      OMX_CONFIG_INTRAREFRESHVOPTYPE config;

      GST_OMX_INIT_STRUCT (&config);
      config.nPortIndex = self->out_port->index;
      config.IntraRefreshVOP = OMX_TRUE;

      err =
          gst_omx_component_set_config (self->component,
          OMX_IndexConfigVideoIntraVOPRefresh, &config);
      if (err != OMX_ErrorNone)
        GST_ERROR_OBJECT (self, "Failed to force a keyframe: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
    }

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    if (!gst_omx_video_enc_fill_buffer (self, frame->sink_buffer, buf)) {
      gst_omx_port_release_buffer (self->in_port, buf);
      goto buffer_fill_error;
    }

    timestamp = frame->presentation_timestamp;
    if (timestamp != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTimeStamp =
          gst_util_uint64_scale (timestamp, OMX_TICKS_PER_SECOND, GST_SECOND);
    }

    duration = frame->presentation_duration;
    if (duration != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (buf->omx_buf->nFilledLen, duration,
          GST_BUFFER_SIZE (frame->sink_buffer));
    }

    id = g_slice_new0 (BufferIdentification);
    id->timestamp = buf->omx_buf->nTimeStamp;
    frame->coder_hook = id;
    frame->coder_hook_destroy_notify =
        (GDestroyNotify) buffer_identification_free;

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
buffer_fill_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE, (NULL),
        ("Failed to write input into the OpenMAX buffer"));
    return GST_FLOW_ERROR;
  }
}

static gboolean
gst_omx_video_enc_finish (GstBaseVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;

  self = GST_OMX_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Sending EOS to the component");

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
  if (acq_ret == GST_OMX_ACQUIRE_BUFFER_OK) {
    buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
    gst_omx_port_release_buffer (self->in_port, buf);
  }

  return TRUE;
}
