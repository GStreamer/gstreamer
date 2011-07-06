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

#include "gstomxvideodec.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_video_dec_debug_category);
#define GST_CAT_DEFAULT gst_omx_video_dec_debug_category

typedef struct _BufferIdentification BufferIdentification;
struct _BufferIdentification
{
  guint64 timestamp;
};

/* prototypes */
static void gst_omx_video_dec_finalize (GObject * object);

static GstStateChangeReturn
gst_omx_video_dec_change_state (GstElement * element,
    GstStateChange transition);

static gboolean gst_omx_video_dec_start (GstBaseVideoDecoder * decoder);
static gboolean gst_omx_video_dec_stop (GstBaseVideoDecoder * decoder);
static gboolean gst_omx_video_dec_set_format (GstBaseVideoDecoder * decoder,
    GstVideoState * state);
static gboolean gst_omx_video_dec_reset (GstBaseVideoDecoder * decoder);
static GstFlowReturn gst_omx_video_dec_parse_data (GstBaseVideoDecoder *
    decoder, gboolean at_eos);
static GstFlowReturn gst_omx_video_dec_handle_frame (GstBaseVideoDecoder *
    decoder, GstVideoFrame * frame);
static GstFlowReturn gst_omx_video_dec_finish (GstBaseVideoDecoder * decoder);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_omx_video_dec_debug_category, "omxvideodec", 0, \
      "debug category for gst-omx video decoder base class");

GST_BOILERPLATE_FULL (GstOMXVideoDec, gst_omx_video_dec, GstBaseVideoDecoder,
    GST_TYPE_BASE_VIDEO_DECODER, DEBUG_INIT);

static void
gst_omx_video_dec_base_init (gpointer g_class)
{
}

static void
gst_omx_video_dec_class_init (GstOMXVideoDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstBaseVideoDecoderClass *base_video_decoder_class =
      GST_BASE_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_omx_video_dec_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_change_state);

  base_video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_video_dec_start);
  base_video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_video_dec_stop);
  base_video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_omx_video_dec_reset);
  base_video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_set_format);
  base_video_decoder_class->parse_data =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_parse_data);
  base_video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_handle_frame);
  base_video_decoder_class->finish =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_finish);

  klass->in_port_index = 0;
  klass->out_port_index = 1;
}

static void
gst_omx_video_dec_init (GstOMXVideoDec * self, GstOMXVideoDecClass * klass)
{
  GST_BASE_VIDEO_DECODER (self)->packetized = TRUE;
}

static gboolean
gst_omx_video_dec_open (GstOMXVideoDec * self)
{
  GstOMXVideoDecClass *klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

  self->component =
      gst_omx_component_new (GST_OBJECT_CAST (self), klass->core_name,
      klass->component_name);

  if (!self->component)
    return FALSE;

  if (gst_omx_component_get_state (self->component,
          GST_CLOCK_TIME_NONE) != OMX_StateLoaded)
    return FALSE;

  /* FIXME: Always 0 == input, 1 == output? Make configurable? Let subclass decide? */
  self->in_port =
      gst_omx_component_add_port (self->component, klass->in_port_index);
  self->out_port =
      gst_omx_component_add_port (self->component, klass->out_port_index);

  if (!self->in_port || !self->out_port)
    return FALSE;

  return TRUE;
}

static gboolean
gst_omx_video_dec_close (GstOMXVideoDec * self)
{
  OMX_STATETYPE state;

  gst_omx_component_set_state (self->component, OMX_StateLoaded);
  gst_omx_port_deallocate_buffers (self->in_port);
  gst_omx_port_deallocate_buffers (self->out_port);
  state = gst_omx_component_get_state (self->component, 5 * GST_SECOND);

  self->in_port = NULL;
  self->out_port = NULL;
  if (self->component)
    gst_omx_component_free (self->component);
  self->component = NULL;

  return (state == OMX_StateLoaded);
}

static void
gst_omx_video_dec_finalize (GObject * object)
{
  /* GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (object); */

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static GstStateChangeReturn
gst_omx_video_dec_change_state (GstElement * element, GstStateChange transition)
{
  GstOMXVideoDec *self;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  g_return_val_if_fail (GST_IS_OMX_VIDEO_DEC (element),
      GST_STATE_CHANGE_FAILURE);
  self = GST_OMX_VIDEO_DEC (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_omx_video_dec_open (self))
        ret = GST_STATE_CHANGE_FAILURE;
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_omx_port_set_flushing (self->out_port, FALSE);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
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
      if (!gst_omx_video_dec_close (self))
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
_find_nearest_frame (GstOMXVideoDec * self, GstOMXBuffer * buf)
{
  GList *l, *best_l = NULL;
  GstVideoFrame *best = NULL;
  guint64 best_timestamp = 0;
  guint64 best_diff = G_MAXUINT64;
  BufferIdentification *best_id = NULL;

  GST_OBJECT_LOCK (self);
  for (l = self->pending_frames; l; l = l->next) {
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
    for (l = self->pending_frames; l && l != best_l;) {
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
        g_warning ("Too old frame, bug in decoder -- please file a bug");
        gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
            tmp);
        self->pending_frames = g_list_delete_link (self->pending_frames, l);
        l = self->pending_frames;
      } else {
        l = l->next;
      }
    }
  }

  if (best_l)
    self->pending_frames = g_list_delete_link (self->pending_frames, best_l);
  GST_OBJECT_UNLOCK (self);

  return best;
}

static void
gst_omx_video_dec_loop (GstOMXVideoDec * self)
{
  GstOMXPort *port = self->out_port;
  GstOMXBuffer *buf = NULL;
  GstVideoFrame *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;

  buf = gst_omx_port_acquire_buffer (port);
  if (!buf) {
    if (gst_omx_component_get_last_error (self->component) != OMX_ErrorNone) {
      goto component_error;
    } else if (!gst_omx_port_is_settings_changed (self->out_port)) {
      goto flushing;
    }
  }

  if (!GST_PAD_CAPS (GST_BASE_VIDEO_CODEC_SRC_PAD (self))
      || gst_omx_port_is_settings_changed (self->out_port)) {
    GstVideoState *state = &GST_BASE_VIDEO_CODEC (self)->state;
    OMX_PARAM_PORTDEFINITIONTYPE port_def;

    gst_omx_port_get_port_definition (port, &port_def);
    g_assert (port_def.format.video.eCompressionFormat ==
        OMX_VIDEO_CodingUnused);

    switch (port_def.format.video.eColorFormat) {
      case OMX_COLOR_FormatYUV420Planar:
        state->format = GST_VIDEO_FORMAT_I420;
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    state->width = port_def.format.video.nFrameWidth;
    state->height = port_def.format.video.nFrameHeight;
    /* FIXME XXX: Bellagio does not set this to something useful... */
    /* gst_util_double_to_fraction (port_def.format.video.xFramerate / ((gdouble) 0xffff), &state->fps_n, &state->fps_d); */
    gst_base_video_decoder_set_src_caps (GST_BASE_VIDEO_DECODER (self));

    if (gst_omx_port_is_settings_changed (self->out_port)) {
      if (gst_omx_port_reconfigure (self->out_port) != OMX_ErrorNone)
        goto reconfigure_error;
    }

    /* Get a new buffer */
    if (!buf)
      return;
  }

  GST_DEBUG_OBJECT (self, "Handling buffer: 0x%08x %lu", buf->omx_buf->nFlags,
      buf->omx_buf->nTimeStamp);

  frame = _find_nearest_frame (self, buf);
  if (!frame) {
    GST_ERROR_OBJECT (self, "No corresponding frame found");
  } else if (buf->omx_buf->nFilledLen > 0) {
    if (gst_base_video_decoder_alloc_src_frame (GST_BASE_VIDEO_DECODER (self),
            frame) == GST_FLOW_OK) {
      memcpy (GST_BUFFER_DATA (frame->src_buffer),
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);
    }
    g_slice_free (BufferIdentification, frame->coder_hook);
    flow_ret =
        gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
        frame);
  } else if (frame != NULL) {
    g_slice_free (BufferIdentification, frame->coder_hook);
    flow_ret =
        gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
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
        ("OpenMAX component in error state %d",
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
}

static gboolean
gst_omx_video_dec_start (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;
  gboolean ret;

  self = GST_OMX_VIDEO_DEC (decoder);

  ret =
      gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, self);

  return ret;
}

static gboolean
gst_omx_video_dec_stop (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;
  gboolean ret;

  self = GST_OMX_VIDEO_DEC (decoder);

  ret = gst_pad_stop_task (GST_BASE_VIDEO_CODEC_SRC_PAD (decoder));

  gst_omx_component_set_state (self->component, OMX_StateIdle);

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  gst_omx_component_get_state (self->component, 5 * GST_SECOND);

  g_list_free (self->pending_frames);
  self->pending_frames = NULL;

  gst_buffer_replace (&self->codec_data, NULL);

  return ret;
}

static gboolean
gst_omx_video_dec_set_format (GstBaseVideoDecoder * decoder,
    GstVideoState * state)
{
  GstOMXVideoDec *self;
  GstOMXVideoDecClass *klass;
  gboolean is_format_change = FALSE;
  gboolean needs_disable = FALSE;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;

  self = GST_OMX_VIDEO_DEC (decoder);
  klass = GST_OMX_VIDEO_DEC_GET_CLASS (decoder);

  /* FIXME: If called again later, properly set states and reinitialize
   *        only possible in Loaded state or if port is disabled =>
   *        delay if state>loaded into the port-disabled callback */

  gst_omx_port_get_port_definition (self->in_port, &port_def);

  /* Check if the caps change is a real format change or if only irrelevant
   * parts of the caps have changed or nothing at all.
   */
  is_format_change |= port_def.format.video.nFrameWidth != state->width;
  is_format_change |= port_def.format.video.nFrameHeight != state->height;
  is_format_change |= (port_def.format.video.xFramerate == 0
      && state->fps_n != 0)
      || (port_def.format.video.xFramerate !=
      (state->fps_n << 16) / (state->fps_d));
  is_format_change |= (self->codec_data != state->codec_data);
  if (klass->is_format_change)
    is_format_change |= klass->is_format_change (self, self->in_port, state);

  needs_disable =
      gst_omx_component_get_state (self->component,
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
    if (gst_omx_port_set_enabled (self->in_port, FALSE) != OMX_ErrorNone)
      return FALSE;
  }

  port_def.format.video.nFrameWidth = state->width - 100;
  port_def.format.video.nFrameHeight = state->height;
  if (state->fps_n == 0)
    port_def.format.video.xFramerate = 0;
  else
    port_def.format.video.xFramerate = (state->fps_n << 16) / (state->fps_d);

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

  gst_buffer_replace (&self->codec_data, state->codec_data);

  if (needs_disable) {
    if (gst_omx_port_set_enabled (self->in_port, TRUE) != OMX_ErrorNone)
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
    GST_ERROR_OBJECT (self, "Component in error state: %d",
        gst_omx_component_get_last_error (self->component));
    return FALSE;
  }

  /* Start the srcpad loop again */
  gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, decoder);

  return (gst_omx_component_get_state (self->component,
          GST_CLOCK_TIME_NONE) == OMX_StateExecuting);
}

static gboolean
gst_omx_video_dec_reset (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Resetting decoder");

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  /* Wait until the srcpad loop is finished */
  GST_PAD_STREAM_LOCK (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
  GST_PAD_STREAM_UNLOCK (GST_BASE_VIDEO_CODEC_SRC_PAD (self));

  g_list_free (self->pending_frames);
  self->pending_frames = NULL;

  gst_omx_port_set_flushing (self->in_port, FALSE);
  gst_omx_port_set_flushing (self->out_port, FALSE);

  /* Start the srcpad loop again */
  gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, decoder);

  return TRUE;
}

static GstFlowReturn
gst_omx_video_dec_parse_data (GstBaseVideoDecoder * decoder, gboolean at_eos)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_omx_video_dec_handle_frame (GstBaseVideoDecoder * decoder,
    GstVideoFrame * frame)
{
  GstOMXVideoDec *self;
  GstOMXBuffer *buf;
  GstBuffer *codec_data = NULL;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Handling frame");

  if (gst_omx_port_is_flushing (self->in_port))
    goto flushing;
  if (gst_omx_component_get_last_error (self->component) != OMX_ErrorNone)
    goto component_error;

  if (gst_omx_port_is_settings_changed (self->in_port)) {
    if (gst_omx_port_reconfigure (self->in_port) != OMX_ErrorNone)
      goto reconfigure_error;
  }

  if (self->codec_data) {
    codec_data = self->codec_data;

  retry_codec_data:
    buf = gst_omx_port_acquire_buffer (self->in_port);
    if (!buf) {
      if (gst_omx_component_get_last_error (self->component) != OMX_ErrorNone) {
        goto component_error;
      } else if (gst_omx_port_is_settings_changed (self->in_port)) {
        if (gst_omx_port_reconfigure (self->in_port) != OMX_ErrorNone)
          goto reconfigure_error;
        goto retry_codec_data;
      } else {
        goto flushing;
      }
    }

    if (buf->omx_buf->nAllocLen < GST_BUFFER_SIZE (codec_data)) {
      gst_omx_port_release_buffer (self->in_port, buf);
      goto too_large_codec_data;
    }

    buf->omx_buf->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;
    buf->omx_buf->nFilledLen = GST_BUFFER_SIZE (codec_data);
    memcpy (buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        GST_BUFFER_DATA (codec_data), GST_BUFFER_SIZE (codec_data));

    gst_omx_port_release_buffer (self->in_port, buf);
    self->codec_data = NULL;
  }

  {
    guint offset = 0;
    GstClockTime timestamp, duration, timestamp_offset = 0;

    GST_OBJECT_LOCK (self);
    self->pending_frames = g_list_append (self->pending_frames, frame);
    GST_OBJECT_UNLOCK (self);

    timestamp = frame->presentation_timestamp;
    duration = frame->presentation_duration;

    while (offset < GST_BUFFER_SIZE (frame->sink_buffer)) {
      buf = gst_omx_port_acquire_buffer (self->in_port);

      if (!buf) {
        if (gst_omx_component_get_last_error (self->component) != OMX_ErrorNone) {
          goto component_error;
        } else if (gst_omx_port_is_settings_changed (self->in_port)) {
          if (gst_omx_port_reconfigure (self->in_port) != OMX_ErrorNone)
            goto reconfigure_error;
          continue;
        } else {
          goto flushing;
        }
      }

      /* Copy the buffer content in chunks of size as requested
       * by the port */
      buf->omx_buf->nFilledLen =
          MIN (GST_BUFFER_SIZE (frame->sink_buffer) - offset,
          buf->omx_buf->nAllocLen - buf->omx_buf->nOffset);
      memcpy (buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          GST_BUFFER_DATA (frame->sink_buffer) + offset,
          buf->omx_buf->nFilledLen);

      /* Interpolate timestamps if we're passing the buffer
       * in multiple chunks */
      if (offset != 0 && duration != GST_CLOCK_TIME_NONE) {
        timestamp_offset =
            gst_util_uint64_scale (offset, duration,
            GST_BUFFER_SIZE (frame->sink_buffer));
      }

      if (timestamp != GST_CLOCK_TIME_NONE) {
        buf->omx_buf->nTimeStamp =
            gst_util_uint64_scale (timestamp + timestamp_offset,
            OMX_TICKS_PER_SECOND, GST_SECOND);
      }
      if (duration != GST_CLOCK_TIME_NONE) {
        buf->omx_buf->nTickCount =
            gst_util_uint64_scale (buf->omx_buf->nFilledLen, duration,
            GST_BUFFER_SIZE (frame->sink_buffer));
      }

      if (offset == 0) {
        BufferIdentification *id = g_slice_new0 (BufferIdentification);

        id->timestamp = buf->omx_buf->nTimeStamp;
        frame->coder_hook = id;
      }

      /* TODO: Set flags
       *   - OMX_BUFFERFLAG_DECODEONLY for buffers that are outside
       *     the segment
       *   - OMX_BUFFERFLAG_SYNCFRAME for non-delta frames
       *   - OMX_BUFFERFLAG_ENDOFFRAME for parsed input
       */

      offset += buf->omx_buf->nFilledLen;
      gst_omx_port_release_buffer (self->in_port, buf);
    }
  }

  return GST_FLOW_OK;

too_large_codec_data:
  {
    GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
        ("codec_data larger than supported by OpenMAX port (%u > %u)",
            GST_BUFFER_SIZE (codec_data), self->in_port->port_def.nBufferSize));
    return GST_FLOW_ERROR;
  }

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %d",
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

static GstFlowReturn
gst_omx_video_dec_finish (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;
  GstOMXBuffer *buf;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Sending EOS to the component");

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  buf = gst_omx_port_acquire_buffer (self->in_port);
  if (buf) {
    buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
    gst_omx_port_release_buffer (self->in_port, buf);
  }

  return GST_BASE_VIDEO_DECODER_FLOW_DROPPED;
}
