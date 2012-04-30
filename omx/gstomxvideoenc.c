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
      {0xffffffff, "Component Default", "default"},
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
    GstVideoInfo * info);
static gboolean gst_omx_video_enc_reset (GstBaseVideoEncoder * encoder);
static GstFlowReturn gst_omx_video_enc_handle_frame (GstBaseVideoEncoder *
    encoder, GstVideoFrameState * frame);
static gboolean gst_omx_video_enc_finish (GstBaseVideoEncoder * encoder);

static GstFlowReturn gst_omx_video_enc_drain (GstOMXVideoEnc * self);

static GstFlowReturn gst_omx_video_enc_handle_output_frame (GstOMXVideoEnc *
    self, GstOMXPort * port, GstOMXBuffer * buf, GstVideoFrameState * frame);

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
#define GST_OMX_VIDEO_ENC_CONTROL_RATE_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_TARGET_BITRATE_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT (0xffffffff)
#define GST_OMX_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT (0xffffffff)

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_video_enc_debug_category, "omxvideoenc", 0, \
      "debug category for gst-omx video encoder base class");

G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXVideoEnc, gst_omx_video_enc,
    GST_TYPE_BASE_VIDEO_ENCODER, DEBUG_INIT);

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
          "Target bitrate (0xffffffff=component default)",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_TARGET_BITRATE_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_PLAYING));

  g_object_class_install_property (gobject_class, PROP_QUANT_I_FRAMES,
      g_param_spec_uint ("quant-i-frames", "I-Frame Quantization",
          "Quantization parameter for I-frames (0xffffffff=component default)",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_QUANT_P_FRAMES,
      g_param_spec_uint ("quant-p-frames", "P-Frame Quantization",
          "Quantization parameter for P-frames (0xffffffff=component default)",
          0, G_MAXUINT, GST_OMX_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

  g_object_class_install_property (gobject_class, PROP_QUANT_B_FRAMES,
      g_param_spec_uint ("quant-b-frames", "B-Frame Quantization",
          "Quantization parameter for B-frames (0xffffffff=component default)",
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

  klass->cdata.default_sink_template_caps = "video/x-raw, "
      "width = " GST_VIDEO_SIZE_RANGE ", "
      "height = " GST_VIDEO_SIZE_RANGE ", " "framerate = " GST_VIDEO_FPS_RANGE;

  klass->handle_output_frame =
      GST_DEBUG_FUNCPTR (gst_omx_video_enc_handle_output_frame);
}

static void
gst_omx_video_enc_init (GstOMXVideoEnc * self)
{
  self->control_rate = GST_OMX_VIDEO_ENC_CONTROL_RATE_DEFAULT;
  self->target_bitrate = GST_OMX_VIDEO_ENC_TARGET_BITRATE_DEFAULT;
  self->quant_i_frames = GST_OMX_VIDEO_ENC_QUANT_I_FRAMES_DEFAULT;
  self->quant_p_frames = GST_OMX_VIDEO_ENC_QUANT_P_FRAMES_DEFAULT;
  self->quant_b_frames = GST_OMX_VIDEO_ENC_QUANT_B_FRAMES_DEFAULT;

  self->drain_lock = g_mutex_new ();
  self->drain_cond = g_cond_new ();
}

static gboolean
gst_omx_video_enc_open (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

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

  /* Set properties */
  {
    OMX_ERRORTYPE err;

    if (self->control_rate != 0xffffffff || self->target_bitrate != 0xffffffff) {
      OMX_VIDEO_PARAM_BITRATETYPE bitrate_param;

      GST_OMX_INIT_STRUCT (&bitrate_param);
      bitrate_param.nPortIndex = self->out_port->index;

      err = gst_omx_component_get_parameter (self->component,
          OMX_IndexParamVideoBitrate, &bitrate_param);

      if (err == OMX_ErrorNone) {
        if (self->control_rate != 0xffffffff)
          bitrate_param.eControlRate = self->control_rate;
        if (self->target_bitrate != 0xffffffff)
          bitrate_param.nTargetBitrate = self->target_bitrate;

        err =
            gst_omx_component_set_parameter (self->component,
            OMX_IndexParamVideoBitrate, &bitrate_param);
        if (err == OMX_ErrorUnsupportedIndex) {
          GST_WARNING_OBJECT (self,
              "Setting a bitrate not supported by the component");
        } else if (err == OMX_ErrorUnsupportedSetting) {
          GST_WARNING_OBJECT (self,
              "Setting bitrate settings %u %u not supported by the component",
              self->control_rate, self->target_bitrate);
        } else if (err != OMX_ErrorNone) {
          GST_ERROR_OBJECT (self,
              "Failed to set bitrate parameters: %s (0x%08x)",
              gst_omx_error_to_string (err), err);
          return FALSE;
        }
      } else {
        GST_ERROR_OBJECT (self, "Failed to get bitrate parameters: %s (0x%08x)",
            gst_omx_error_to_string (err), err);
      }
    }

    if (self->quant_i_frames != 0xffffffff ||
        self->quant_p_frames != 0xffffffff ||
        self->quant_b_frames != 0xffffffff) {
      OMX_VIDEO_PARAM_QUANTIZATIONTYPE quant_param;

      GST_OMX_INIT_STRUCT (&quant_param);
      quant_param.nPortIndex = self->out_port->index;

      err = gst_omx_component_get_parameter (self->component,
          OMX_IndexParamVideoQuantization, &quant_param);

      if (err == OMX_ErrorNone) {

        if (self->quant_i_frames != 0xffffffff)
          quant_param.nQpI = self->quant_i_frames;
        if (self->quant_p_frames != 0xffffffff)
          quant_param.nQpP = self->quant_p_frames;
        if (self->quant_b_frames != 0xffffffff)
          quant_param.nQpB = self->quant_b_frames;

        err =
            gst_omx_component_set_parameter (self->component,
            OMX_IndexParamVideoQuantization, &quant_param);
        if (err == OMX_ErrorUnsupportedIndex) {
          GST_WARNING_OBJECT (self,
              "Setting quantization parameters not supported by the component");
        } else if (err == OMX_ErrorUnsupportedSetting) {
          GST_WARNING_OBJECT (self,
              "Setting quantization parameters %u %u %u not supported by the component",
              self->quant_i_frames, self->quant_p_frames, self->quant_b_frames);
        } else if (err != OMX_ErrorNone) {
          GST_ERROR_OBJECT (self,
              "Failed to set quantization parameters: %s (0x%08x)",
              gst_omx_error_to_string (err), err);
          return FALSE;
        }
      } else {
        GST_ERROR_OBJECT (self,
            "Failed to get quantization parameters: %s (0x%08x)",
            gst_omx_error_to_string (err), err);

      }
    }
  }

  return TRUE;
}

static gboolean
gst_omx_video_enc_shutdown (GstOMXVideoEnc * self)
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
gst_omx_video_enc_close (GstOMXVideoEnc * self)
{
  GST_DEBUG_OBJECT (self, "Closing encoder");

  if (!gst_omx_video_enc_shutdown (self))
    return FALSE;

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
  GstOMXVideoEnc *self = GST_OMX_VIDEO_ENC (object);

  g_mutex_free (self->drain_lock);
  g_cond_free (self->drain_cond);

  G_OBJECT_CLASS (gst_omx_video_enc_parent_class)->finalize (object);
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
      GST_ELEMENT_CLASS (gst_omx_video_enc_parent_class)->change_state (element,
      transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->downstream_flow_ret = GST_FLOW_FLUSHING;
      self->started = FALSE;

      if (!gst_omx_video_enc_shutdown (self))
        ret = GST_STATE_CHANGE_FAILURE;
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

static GstVideoFrameState *
_find_nearest_frame (GstOMXVideoEnc * self, GstOMXBuffer * buf)
{
  GList *l, *best_l = NULL;
  GList *finish_frames = NULL;
  GstVideoFrameState *best = NULL;
  guint64 best_timestamp = 0;
  guint64 best_diff = G_MAXUINT64;
  BufferIdentification *best_id = NULL;

  for (l = GST_BASE_VIDEO_CODEC (self)->frames; l; l = l->next) {
    GstVideoFrameState *tmp = l->data;
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
      GstVideoFrameState *tmp = l->data;
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

  if (finish_frames) {
    g_warning ("Too old frames, bug in encoder -- please file a bug");
    for (l = finish_frames; l; l = l->next) {
      gst_base_video_encoder_finish_frame (GST_BASE_VIDEO_ENCODER (self),
          l->data);
    }
  }

  return best;
}

static GstFlowReturn
gst_omx_video_enc_handle_output_frame (GstOMXVideoEnc * self, GstOMXPort * port,
    GstOMXBuffer * buf, GstVideoFrameState * frame)
{
  GstOMXVideoEncClass *klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);
  GstFlowReturn flow_ret = GST_FLOW_OK;

  if ((buf->omx_buf->nFlags & OMX_BUFFERFLAG_CODECCONFIG)
      && buf->omx_buf->nFilledLen > 0) {
    GstCaps *caps;
    GstBuffer *codec_data;
    GstMapInfo map = GST_MAP_INFO_INIT;

    caps =
        gst_caps_copy (gst_pad_get_current_caps (GST_BASE_VIDEO_CODEC_SRC_PAD
            (self)));
    codec_data = gst_buffer_new_and_alloc (buf->omx_buf->nFilledLen);

    gst_buffer_map (codec_data, &map, GST_MAP_WRITE);
    memcpy (map.data,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);
    gst_buffer_unmap (codec_data, &map);

    gst_caps_set_simple (caps, "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
    if (!gst_pad_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (self), caps)) {
      gst_caps_unref (caps);
      return GST_FLOW_NOT_NEGOTIATED;
    }
    gst_caps_unref (caps);
    flow_ret = GST_FLOW_OK;
  } else if (buf->omx_buf->nFilledLen > 0) {
    GstBuffer *outbuf;
    GstMapInfo map = GST_MAP_INFO_INIT;

    if (buf->omx_buf->nFilledLen > 0) {
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

    if ((klass->cdata.hacks & GST_OMX_HACK_SYNCFRAME_FLAG_NOT_USED)
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

  return flow_ret;
}

static void
gst_omx_video_enc_loop (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass;
  GstOMXPort *port = self->out_port;
  GstOMXBuffer *buf = NULL;
  GstVideoFrameState *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;
  gboolean is_eos;

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

  if (!gst_pad_has_current_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (self))
      || acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURED) {
    GstVideoState *state = &GST_BASE_VIDEO_CODEC (self)->state;
    GstCaps *caps;

    GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    caps = klass->get_caps (self, self->out_port, state);
    if (!caps) {
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
      goto caps_failed;
    }

    if (!gst_pad_set_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (self), caps)) {
      gst_caps_unref (caps);
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
      goto caps_failed;
    }
    gst_caps_unref (caps);

    GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);

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

    GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);
    frame = _find_nearest_frame (self, buf);

    is_eos = ! !(buf->omx_buf->nFlags & OMX_BUFFERFLAG_EOS);

    g_assert (klass->handle_output_frame);
    flow_ret = klass->handle_output_frame (self, self->out_port, buf, frame);

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
    GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);
    flow_ret = GST_FLOW_EOS;
  }

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);

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
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }
flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_FLUSHING;
    self->started = FALSE;
    return;
  }
flow_error:
  {
    if (flow_ret == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    } else if (flow_ret == GST_FLOW_NOT_LINKED || flow_ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Internal data stream error."),
          ("stream stopped, reason %s", gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    }
    self->started = FALSE;
    GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
    return;
  }
reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    return;
  }
caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    return;
  }
}

static gboolean
gst_omx_video_enc_start (GstBaseVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;
  gboolean ret;

  self = GST_OMX_VIDEO_ENC (encoder);

  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
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

  GST_DEBUG_OBJECT (self, "Stopping encoder");

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  gst_pad_stop_task (GST_BASE_VIDEO_CODEC_SRC_PAD (encoder));

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
gst_omx_video_enc_set_format (GstBaseVideoEncoder * encoder,
    GstVideoInfo * info)
{
  GstOMXVideoEnc *self;
  GstOMXVideoEncClass *klass;
  gboolean needs_disable = FALSE;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;

  self = GST_OMX_VIDEO_ENC (encoder);
  klass = GST_OMX_VIDEO_ENC_GET_CLASS (encoder);

  GST_DEBUG_OBJECT (self, "Setting new format %s",
      gst_video_format_to_string (info->finfo->format));

  gst_omx_port_get_port_definition (self->in_port, &port_def);

  needs_disable =
      gst_omx_component_get_state (self->component,
      GST_CLOCK_TIME_NONE) != OMX_StateLoaded;
  /* If the component is not in Loaded state and a real format change happens
   * we have to disable the port and re-allocate all buffers. If no real
   * format change happened we can just exit here.
   */
  if (needs_disable) {
    gst_omx_video_enc_drain (self);

    if (gst_omx_port_manual_reconfigure (self->in_port, TRUE) != OMX_ErrorNone)
      return FALSE;
    if (gst_omx_port_set_enabled (self->in_port, FALSE) != OMX_ErrorNone)
      return FALSE;
  }

  switch (info->finfo->format) {
    case GST_VIDEO_FORMAT_I420:
      port_def.format.video.eColorFormat = OMX_COLOR_FormatYUV420Planar;
      break;
    case GST_VIDEO_FORMAT_NV12:
      port_def.format.video.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
      break;
    default:
      GST_ERROR_OBJECT (self, "Unsupported format %s",
          gst_video_format_to_string (info->finfo->format));
      return FALSE;
      break;
  }
  port_def.format.video.nFrameWidth = info->width;
  port_def.format.video.nFrameHeight = info->height;
  if (info->fps_n == 0) {
    port_def.format.video.xFramerate = 0;
  } else {
    if (!(klass->cdata.hacks & GST_OMX_HACK_VIDEO_FRAMERATE_INTEGER))
      port_def.format.video.xFramerate = (info->fps_n << 16) / (info->fps_d);
    else
      port_def.format.video.xFramerate = (info->fps_n) / (info->fps_d);
  }

  if (!gst_omx_port_update_port_definition (self->in_port, &port_def))
    return FALSE;
  if (!gst_omx_port_update_port_definition (self->out_port, NULL))
    return FALSE;

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
  gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_enc_loop, encoder);

  return TRUE;
}

static gboolean
gst_omx_video_enc_reset (GstBaseVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;

  self = GST_OMX_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Resetting encoder");

  gst_omx_video_enc_drain (self);

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  /* Wait until the srcpad loop is finished,
   * unlock GST_BASE_VIDEO_CODEC_STREAM_LOCK to prevent deadlocks
   * caused by using this lock from inside the loop function */
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
  GST_PAD_STREAM_LOCK (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
  GST_PAD_STREAM_UNLOCK (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
  GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);

  gst_omx_port_set_flushing (self->in_port, FALSE);
  gst_omx_port_set_flushing (self->out_port, FALSE);

  /* Start the srcpad loop again */
  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
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
  GstVideoInfo vinfo;
  GstVideoFrame frame;

  if (state->width != port_def->format.video.nFrameWidth ||
      state->height != port_def->format.video.nFrameHeight) {
    GST_ERROR_OBJECT (self, "Width or height do not match");
    goto done;
  }

  /* Same strides and everything */
  if (gst_buffer_get_size (inbuf) ==
      outbuf->omx_buf->nAllocLen - outbuf->omx_buf->nOffset) {
    outbuf->omx_buf->nFilledLen = gst_buffer_get_size (inbuf);

    gst_buffer_extract (inbuf, 0,
        outbuf->omx_buf->pBuffer + outbuf->omx_buf->nOffset,
        outbuf->omx_buf->nFilledLen);
    ret = TRUE;
    goto done;
  }

  /* Different strides */

  gst_video_info_from_caps (&vinfo, state->caps);

  switch (state->format) {
    case GST_VIDEO_FORMAT_I420:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride;

      outbuf->omx_buf->nFilledLen = 0;

      for (i = 0; i < 3; i++) {
        if (i == 0) {
          dest_stride = port_def->format.video.nStride;
          src_stride = vinfo.stride[0];

          /* XXX: Try this if no stride was set */
          if (dest_stride == 0)
            dest_stride = src_stride;
        } else {
          dest_stride = port_def->format.video.nStride / 2;
          src_stride = vinfo.stride[1];

          /* XXX: Try this if no stride was set */
          if (dest_stride == 0)
            dest_stride = src_stride;
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

        if (!gst_video_frame_map (&frame, &vinfo, inbuf, GST_MAP_READ)) {
          GST_ERROR_OBJECT (self, "Invalid input buffer size");
          ret = FALSE;
          break;
        }
        src = GST_VIDEO_FRAME_COMP_DATA (&frame, i);
        height = GST_VIDEO_FRAME_HEIGHT (&frame);

        if (dest + dest_stride * height >
            outbuf->omx_buf->pBuffer + outbuf->omx_buf->nAllocLen) {
          GST_ERROR_OBJECT (self, "Invalid output buffer size");
          ret = FALSE;
          break;
        }

        for (j = 0; j < height; j++) {
          memcpy (dest, src, MIN (src_stride, dest_stride));
          outbuf->omx_buf->nFilledLen += dest_stride;
          src += src_stride;
          dest += dest_stride;
        }

        gst_video_frame_unmap (&frame);
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
          src_stride = vinfo.stride[0];
          /* XXX: Try this if no stride was set */
          if (dest_stride == 0)
            dest_stride = src_stride;
        } else {
          dest_stride = port_def->format.video.nStride;
          src_stride = vinfo.stride[1];

          /* XXX: Try this if no stride was set */
          if (dest_stride == 0)
            dest_stride = src_stride;
        }

        dest = outbuf->omx_buf->pBuffer + outbuf->omx_buf->nOffset;
        if (i == 1)
          dest +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;



        if (!gst_video_frame_map (&frame, &vinfo, inbuf, GST_MAP_READ)) {
          GST_ERROR_OBJECT (self, "Invalid input buffer size");
          ret = FALSE;
          break;
        }
        src = GST_VIDEO_FRAME_COMP_DATA (&frame, i);
        height = GST_VIDEO_FRAME_HEIGHT (&frame);

        if (dest + dest_stride * height >
            outbuf->omx_buf->pBuffer + outbuf->omx_buf->nAllocLen) {
          GST_ERROR_OBJECT (self, "Invalid output buffer size");
          ret = FALSE;
          break;
        }

        for (j = 0; j < height; j++) {
          memcpy (dest, src, MIN (src_stride, dest_stride));
          outbuf->omx_buf->nFilledLen += dest_stride;
          src += src_stride;
          dest += dest_stride;
        }

        gst_video_frame_unmap (&frame);
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
    GstVideoFrameState * frame)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXVideoEnc *self;
  GstOMXBuffer *buf;

  self = GST_OMX_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Handling frame");

  if (self->eos) {
    GST_WARNING_OBJECT (self, "Got frame after EOS");
    return GST_FLOW_EOS;
  }

  if (self->downstream_flow_ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Downstream returned %s",
        gst_flow_get_name (self->downstream_flow_ret));

    return self->downstream_flow_ret;
  }

  while (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    BufferIdentification *id;
    GstClockTime timestamp, duration;

    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
    acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
    GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);

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

    if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <= 0) {
      gst_omx_port_release_buffer (self->in_port, buf);
      goto full_buffer;
    }

    if (self->downstream_flow_ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Downstream returned %s",
          gst_flow_get_name (self->downstream_flow_ret));

      gst_omx_port_release_buffer (self->in_port, buf);
      return self->downstream_flow_ret;
    }

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

      frame->force_keyframe = FALSE;
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
      self->last_upstream_ts = timestamp;
    }

    duration = frame->presentation_duration;
    if (duration != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (buf->omx_buf->nFilledLen, duration,
          gst_buffer_get_size (frame->sink_buffer));
      self->last_upstream_ts += duration;
    }

    id = g_slice_new0 (BufferIdentification);
    id->timestamp = buf->omx_buf->nTimeStamp;
    frame->coder_hook = id;
    frame->coder_hook_destroy_notify =
        (GDestroyNotify) buffer_identification_free;

    self->started = TRUE;
    gst_omx_port_release_buffer (self->in_port, buf);
  }

  return self->downstream_flow_ret;;

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
buffer_fill_error:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, WRITE, (NULL),
        ("Failed to write input into the OpenMAX buffer"));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_omx_video_enc_finish (GstBaseVideoEncoder * encoder)
{
  GstOMXVideoEnc *self;
  GstOMXVideoEncClass *klass;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;

  self = GST_OMX_VIDEO_ENC (encoder);
  klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Sending EOS to the component");

  /* Don't send EOS buffer twice, this doesn't work */
  if (self->eos) {
    GST_DEBUG_OBJECT (self, "Component is already EOS");
    return GST_BASE_VIDEO_ENCODER_FLOW_DROPPED;
  }
  self->eos = TRUE;

  if ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER)) {
    GST_WARNING_OBJECT (self, "Component does not support empty EOS buffers");

    /* Insert a NULL into the queue to signal EOS */
    gst_omx_rec_mutex_lock (&self->out_port->port_lock);
    g_queue_push_tail (self->out_port->pending_buffers, NULL);
    g_cond_broadcast (self->out_port->port_cond);
    gst_omx_rec_mutex_unlock (&self->out_port->port_lock);

    return GST_BASE_VIDEO_ENCODER_FLOW_DROPPED;
  }

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);

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

  GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);

  return GST_BASE_VIDEO_ENCODER_FLOW_DROPPED;
}

static GstFlowReturn
gst_omx_video_enc_drain (GstOMXVideoEnc * self)
{
  GstOMXVideoEncClass *klass;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;

  GST_DEBUG_OBJECT (self, "Draining component");

  klass = GST_OMX_VIDEO_ENC_GET_CLASS (self);

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
  GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
  if (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);
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
  GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);

  self->started = FALSE;

  return GST_FLOW_OK;
}
