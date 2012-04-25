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

static void
buffer_identification_free (BufferIdentification * id)
{
  g_slice_free (BufferIdentification, id);
}

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
    decoder, GstVideoFrameState * frame);
static GstFlowReturn gst_omx_video_dec_finish (GstBaseVideoDecoder * decoder);

static GstFlowReturn gst_omx_video_dec_drain (GstOMXVideoDec * self);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_video_dec_debug_category, "omxvideodec", 0, \
      "debug category for gst-omx video decoder base class");


G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXVideoDec, gst_omx_video_dec,
    GST_TYPE_BASE_VIDEO_DECODER, DEBUG_INIT);

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

  klass->cdata.default_src_template_caps = "video/x-raw, "
      "width = " GST_VIDEO_SIZE_RANGE ", "
      "height = " GST_VIDEO_SIZE_RANGE ", " "framerate = " GST_VIDEO_FPS_RANGE;
}

static void
gst_omx_video_dec_init (GstOMXVideoDec * self)
{
  GST_BASE_VIDEO_DECODER (self)->packetized = TRUE;

  self->drain_lock = g_mutex_new ();
  self->drain_cond = g_cond_new ();
}

static gboolean
gst_omx_video_dec_open (GstOMXVideoDec * self)
{
  GstOMXVideoDecClass *klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Opening decoder");

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

  GST_DEBUG_OBJECT (self, "Opened decoder");

  return TRUE;
}

static gboolean
gst_omx_video_dec_shutdown (GstOMXVideoDec * self)
{
  OMX_STATETYPE state;

  GST_DEBUG_OBJECT (self, "Shutting down decoder");

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
gst_omx_video_dec_close (GstOMXVideoDec * self)
{
  GST_DEBUG_OBJECT (self, "Closing decoder");

  if (!gst_omx_video_dec_shutdown (self))
    return FALSE;

  self->in_port = NULL;
  self->out_port = NULL;
  if (self->component)
    gst_omx_component_free (self->component);
  self->component = NULL;

  self->started = FALSE;

  GST_DEBUG_OBJECT (self, "Closed decoder");

  return TRUE;
}

static void
gst_omx_video_dec_finalize (GObject * object)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (object);

  g_mutex_free (self->drain_lock);
  g_cond_free (self->drain_cond);

  G_OBJECT_CLASS (gst_omx_video_dec_parent_class)->finalize (object);
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
      GST_ELEMENT_CLASS (gst_omx_video_dec_parent_class)->change_state (element,
      transition);

  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      self->downstream_flow_ret = GST_FLOW_FLUSHING;
      self->started = FALSE;

      if (!gst_omx_video_dec_shutdown (self))
        ret = GST_STATE_CHANGE_FAILURE;
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

static GstVideoFrameState *
_find_nearest_frame (GstOMXVideoDec * self, GstOMXBuffer * buf)
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
    g_warning ("Too old frames, bug in decoder -- please file a bug");
    for (l = finish_frames; l; l = l->next) {
      gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
          l->data);
    }
  }

  return best;
}

static gboolean
gst_omx_video_dec_fill_buffer (GstOMXVideoDec * self, GstOMXBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (self)->state;
  OMX_PARAM_PORTDEFINITIONTYPE *port_def = &self->out_port->port_def;
  gboolean ret = FALSE;
  GstVideoInfo vinfo;
  GstVideoFrame frame;

  if (state->width != port_def->format.video.nFrameWidth ||
      state->height != port_def->format.video.nFrameHeight) {
    GST_ERROR_OBJECT (self, "Width or height do not match");
    goto done;
  }

  /* Same strides and everything */
  if (gst_buffer_get_size (outbuf) == inbuf->omx_buf->nFilledLen) {
    GstMapInfo map = GST_MAP_INFO_INIT;

    gst_buffer_map (outbuf, &map, GST_MAP_WRITE);
    memcpy (map.data,
        inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset,
        inbuf->omx_buf->nFilledLen);
    gst_buffer_unmap (outbuf, &map);
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

      for (i = 0; i < 3; i++) {
        if (i == 0) {
          src_stride = port_def->format.video.nStride;
          dest_stride = vinfo.stride[0];

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        } else {
          src_stride = port_def->format.video.nStride / 2;
          dest_stride = vinfo.stride[1];

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        }

        src = inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset;
        if (i > 0)
          src +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;
        if (i == 2)
          src +=
              (port_def->format.video.nSliceHeight / 2) *
              (port_def->format.video.nStride / 2);

        gst_video_frame_map (&frame, &vinfo, outbuf, GST_MAP_WRITE);
        dest = GST_VIDEO_FRAME_COMP_DATA (&frame, i);
        height = GST_VIDEO_FRAME_HEIGHT (&frame);

        for (j = 0; j < height; j++) {
          memcpy (dest, src, MIN (src_stride, dest_stride));
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

      for (i = 0; i < 2; i++) {
        if (i == 0) {
          src_stride = port_def->format.video.nStride;
          dest_stride = vinfo.stride[0];

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        } else {
          src_stride = port_def->format.video.nStride;
          dest_stride = vinfo.stride[1];

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        }

        src = inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset;
        if (i == 1)
          src +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;

        gst_video_frame_map (&frame, &vinfo, outbuf, GST_MAP_WRITE);
        dest = GST_VIDEO_FRAME_COMP_DATA (&frame, i);
        height = GST_VIDEO_FRAME_HEIGHT (&frame);

        for (j = 0; j < height; j++) {
          memcpy (dest, src, MIN (src_stride, dest_stride));
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
  if (ret) {
    GST_BUFFER_PTS (outbuf) =
        gst_util_uint64_scale (inbuf->omx_buf->nTimeStamp, GST_SECOND,
        OMX_TICKS_PER_SECOND);
    if (inbuf->omx_buf->nTickCount != 0)
      GST_BUFFER_DURATION (outbuf) =
          gst_util_uint64_scale (inbuf->omx_buf->nTickCount, GST_SECOND,
          OMX_TICKS_PER_SECOND);
  }

  return ret;
}

static void
gst_omx_video_dec_loop (GstOMXVideoDec * self)
{
  GstOMXPort *port = self->out_port;
  GstOMXBuffer *buf = NULL;
  GstVideoFrameState *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;
  GstClockTimeDiff deadline;
  gboolean is_eos;

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
    OMX_PARAM_PORTDEFINITIONTYPE port_def;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);
    gst_omx_port_get_port_definition (port, &port_def);
    g_assert (port_def.format.video.eCompressionFormat ==
        OMX_VIDEO_CodingUnused);

    switch (port_def.format.video.eColorFormat) {
      case OMX_COLOR_FormatYUV420Planar:
        state->format = GST_VIDEO_FORMAT_I420;
        break;
      case OMX_COLOR_FormatYUV420SemiPlanar:
        state->format = GST_VIDEO_FORMAT_NV12;
        break;
      default:
        GST_ERROR_OBJECT (self, "Unsupported color format: %d",
            port_def.format.video.eColorFormat);
        if (buf)
          gst_omx_port_release_buffer (self->out_port, buf);
        GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
        goto caps_failed;
        break;
    }

    state->width = port_def.format.video.nFrameWidth;
    state->height = port_def.format.video.nFrameHeight;

    /* Take framerate and pixel-aspect-ratio from sinkpad caps */

    if (!gst_base_video_decoder_set_src_caps (GST_BASE_VIDEO_DECODER (self))) {
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
      goto caps_failed;
    }

    GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);

    /* Now get a buffer */
    if (acq_return != GST_OMX_ACQUIRE_BUFFER_OK)
      return;
  }

  g_assert (acq_return == GST_OMX_ACQUIRE_BUFFER_OK && buf != NULL);

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

  if (frame
      && (deadline = gst_base_video_decoder_get_max_decode_time
          (GST_BASE_VIDEO_DECODER (self), frame)) < 0) {
    GST_WARNING_OBJECT (self,
        "Frame is too late, dropping (deadline %" GST_TIME_FORMAT ")",
        GST_TIME_ARGS (-deadline));
    flow_ret =
        gst_base_video_decoder_drop_frame (GST_BASE_VIDEO_DECODER (self),
        frame);
  } else if (!frame && buf->omx_buf->nFilledLen > 0) {
    GstBuffer *outbuf;

    /* This sometimes happens at EOS or if the input is not properly framed,
     * let's handle it gracefully by allocating a new buffer for the current
     * caps and filling it
     */

    GST_ERROR_OBJECT (self, "No corresponding frame found");

    outbuf =
        gst_base_video_decoder_alloc_src_buffer (GST_BASE_VIDEO_DECODER (self));

    if (!gst_omx_video_dec_fill_buffer (self, buf, outbuf)) {
      gst_buffer_unref (outbuf);
      gst_omx_port_release_buffer (self->out_port, buf);
      goto invalid_buffer;
    }

    flow_ret = gst_pad_push (GST_BASE_VIDEO_CODEC_SRC_PAD (self), outbuf);
  } else if (buf->omx_buf->nFilledLen > 0) {
    if (GST_BASE_VIDEO_CODEC (self)->state.bytes_per_picture == 0) {
      /* FIXME: If the sinkpad caps change we have currently no way
       * to allocate new src buffers because basevideodecoder assumes
       * that the caps on both pads are equivalent all the time
       */
      GST_WARNING_OBJECT (self,
          "Caps change pending and still have buffers for old caps -- dropping");
    } else
        if (gst_base_video_decoder_alloc_src_frame (GST_BASE_VIDEO_DECODER
            (self), frame) == GST_FLOW_OK) {
      /* FIXME: This currently happens because of a race condition too.
       * We first need to reconfigure the output port and then the input
       * port if both need reconfiguration.
       */
      if (!gst_omx_video_dec_fill_buffer (self, buf, frame->src_buffer)) {
        gst_buffer_replace (&frame->src_buffer, NULL);
        flow_ret =
            gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
            frame);
        gst_omx_port_release_buffer (self->out_port, buf);
        goto invalid_buffer;
      }
    }
    flow_ret =
        gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
        frame);
  } else if (frame != NULL) {
    flow_ret =
        gst_base_video_decoder_finish_frame (GST_BASE_VIDEO_DECODER (self),
        frame);
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
    GST_DEBUG_OBJECT (self, "Finished frame: %s", gst_flow_get_name (flow_ret));
  }

  gst_omx_port_release_buffer (port, buf);

  self->downstream_flow_ret = flow_ret;

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
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }

invalid_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Invalid sized input buffer"));
    gst_pad_push_event (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
        gst_event_new_eos ());
    gst_pad_pause_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
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
gst_omx_video_dec_start (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;
  gboolean ret;

  self = GST_OMX_VIDEO_DEC (decoder);

  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  ret =
      gst_pad_start_task (GST_BASE_VIDEO_CODEC_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, self);

  return ret;
}

static gboolean
gst_omx_video_dec_stop (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Stopping decoder");

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  gst_pad_stop_task (GST_BASE_VIDEO_CODEC_SRC_PAD (decoder));

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

  gst_buffer_replace (&self->codec_data, NULL);

  GST_DEBUG_OBJECT (self, "Stopped decoder");

  return TRUE;
}

static gboolean
gst_omx_video_dec_negotiate (GstOMXVideoDec * self)
{
  GstOMXPort *port = self->out_port;
  GstVideoState *state = &GST_BASE_VIDEO_CODEC (self)->state;
  OMX_VIDEO_PARAM_PORTFORMATTYPE param;
  OMX_ERRORTYPE err;
  GstCaps *comp_supported_caps;
  GstCaps *templ_caps;
  GstCaps *peer_caps, *intersection;
  GstVideoFormat format;
  gint old_index;
  GstStructure *s;
  const gchar *format_str;

  templ_caps =
      gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_VIDEO_CODEC_SRC_PAD
          (self)));
  peer_caps =
      gst_pad_peer_query_caps (GST_BASE_VIDEO_CODEC_SRC_PAD (self), templ_caps);
  if (peer_caps) {
    intersection = peer_caps;
    gst_caps_unref (templ_caps);
  } else {
    intersection = templ_caps;
  }

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = port->index;
  param.nIndex = 0;
  if (state->fps_n == 0)
    param.xFramerate = 0;
  else
    param.xFramerate = (state->fps_n << 16) / (state->fps_d);

  old_index = -1;
  comp_supported_caps = gst_caps_new_empty ();
  do {
    err =
        gst_omx_component_get_parameter (self->component,
        OMX_IndexParamVideoPortFormat, &param);

    /* FIXME: Workaround for Bellagio that simply always
     * returns the same value regardless of nIndex and
     * never returns OMX_ErrorNoMore
     */
    if (old_index == param.nIndex)
      break;

    if (err == OMX_ErrorNone) {
      switch (param.eColorFormat) {
        case OMX_COLOR_FormatYUV420Planar:
          gst_caps_append_structure (comp_supported_caps,
              gst_structure_new ("video/x-raw",
                  "format", G_TYPE_STRING, "I420", NULL));
          break;
        case OMX_COLOR_FormatYUV420SemiPlanar:
          gst_caps_append_structure (comp_supported_caps,
              gst_structure_new ("video/x-raw",
                  "format", G_TYPE_STRING, "NV12", NULL));
          break;
        default:
          break;
      }
    }
    old_index = param.nIndex++;
  } while (err == OMX_ErrorNone);

  if (!gst_caps_is_empty (comp_supported_caps)) {
    GstCaps *tmp;

    tmp = gst_caps_intersect (comp_supported_caps, intersection);
    gst_caps_unref (intersection);
    intersection = tmp;
  }


  if (gst_caps_is_empty (intersection)) {
    gst_caps_unref (intersection);
    GST_ERROR_OBJECT (self, "Empty caps");
    return FALSE;
  }

  intersection = gst_caps_truncate (intersection);

  s = gst_caps_get_structure (intersection, 0);
  format_str = gst_structure_get_string (s, "format");
  if (!format_str ||
      (format =
          gst_video_format_from_string (format_str)) ==
      GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Invalid caps: %" GST_PTR_FORMAT, intersection);
    return FALSE;
  }

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
      param.eColorFormat = OMX_COLOR_FormatYUV420Planar;
      break;
    case GST_VIDEO_FORMAT_NV12:
      param.eColorFormat = OMX_COLOR_FormatYUV420SemiPlanar;
      break;
    default:
      GST_ERROR_OBJECT (self, "Unknown color format: %u", format);
      return FALSE;
      break;
  }

  /* Reset framerate, we only care about the color format here */
  param.xFramerate = 0;

  err =
      gst_omx_component_set_parameter (self->component,
      OMX_IndexParamVideoPortFormat, &param);
  if (err != OMX_ErrorNone) {
    GST_ERROR_OBJECT (self, "Failed to set video port format: %s (0x%08x)",
        gst_omx_error_to_string (err), err);
  }

  return (err == OMX_ErrorNone);
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

  GST_DEBUG_OBJECT (self, "Setting new caps %" GST_PTR_FORMAT, state->caps);

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
    gst_omx_video_dec_drain (self);

    if (klass->cdata.hacks & GST_OMX_HACK_NO_COMPONENT_RECONFIGURE) {
      GST_BASE_VIDEO_CODEC_STREAM_UNLOCK (self);
      gst_omx_video_dec_stop (GST_BASE_VIDEO_DECODER (self));
      gst_omx_video_dec_close (self);

      GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);

      if (!gst_omx_video_dec_open (self))
        return FALSE;
      needs_disable = FALSE;
    } else {
      if (gst_omx_port_manual_reconfigure (self->in_port,
              TRUE) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_set_enabled (self->in_port, FALSE) != OMX_ErrorNone)
        return FALSE;
    }
  }

  port_def.format.video.nFrameWidth = state->width;
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

  if (!gst_omx_video_dec_negotiate (self))
    return FALSE;

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
      (GstTaskFunction) gst_omx_video_dec_loop, decoder);

  return TRUE;
}

static gboolean
gst_omx_video_dec_reset (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Resetting decoder");

  gst_omx_video_dec_drain (self);

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
      (GstTaskFunction) gst_omx_video_dec_loop, decoder);

  GST_DEBUG_OBJECT (self, "Reset decoder");

  return TRUE;
}

static GstFlowReturn
gst_omx_video_dec_parse_data (GstBaseVideoDecoder * decoder, gboolean at_eos)
{
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_omx_video_dec_handle_frame (GstBaseVideoDecoder * decoder,
    GstVideoFrameState * frame)
{
  GstOMXAcquireBufferReturn acq_ret = GST_OMX_ACQUIRE_BUFFER_ERROR;
  GstOMXVideoDec *self;
  GstOMXVideoDecClass *klass;
  GstOMXBuffer *buf;
  GstBuffer *codec_data = NULL;
  guint offset = 0;
  GstClockTime timestamp, duration, timestamp_offset = 0;

  self = GST_OMX_VIDEO_DEC (decoder);
  klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Handling frame");

  if (self->eos) {
    GST_WARNING_OBJECT (self, "Got frame after EOS");
    return GST_FLOW_EOS;
  }

  timestamp = frame->presentation_timestamp;
  duration = frame->presentation_duration;

  if (self->downstream_flow_ret != GST_FLOW_OK) {
    GST_ERROR_OBJECT (self, "Downstream returned %s",
        gst_flow_get_name (self->downstream_flow_ret));

    return self->downstream_flow_ret;
  }

  if (klass->prepare_frame) {
    GstFlowReturn ret;

    ret = klass->prepare_frame (self, frame);
    if (ret != GST_FLOW_OK) {
      GST_ERROR_OBJECT (self, "Preparing frame failed: %s",
          gst_flow_get_name (ret));
      return ret;
    }
  }

  while (offset < gst_buffer_get_size (frame->sink_buffer)) {
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

    if (self->codec_data) {
      codec_data = self->codec_data;

      if (buf->omx_buf->nAllocLen - buf->omx_buf->nOffset <
          gst_buffer_get_size (codec_data)) {
        gst_omx_port_release_buffer (self->in_port, buf);
        goto too_large_codec_data;
      }

      buf->omx_buf->nFlags |= OMX_BUFFERFLAG_CODECCONFIG;
      buf->omx_buf->nFilledLen = gst_buffer_get_size (codec_data);;
      gst_buffer_extract (codec_data, 0,
          buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
          buf->omx_buf->nFilledLen);

      self->started = TRUE;
      gst_omx_port_release_buffer (self->in_port, buf);
      gst_buffer_replace (&self->codec_data, NULL);
      /* Acquire new buffer for the actual frame */
      continue;
    }

    /* Now handle the frame */

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    gst_buffer_extract (codec_data, offset,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);

    /* Interpolate timestamps if we're passing the buffer
     * in multiple chunks */
    if (offset != 0 && duration != GST_CLOCK_TIME_NONE) {
      timestamp_offset =
          gst_util_uint64_scale (offset, duration,
          gst_buffer_get_size (frame->sink_buffer));
    }

    if (timestamp != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTimeStamp =
          gst_util_uint64_scale (timestamp + timestamp_offset,
          OMX_TICKS_PER_SECOND, GST_SECOND);
      self->last_upstream_ts = timestamp + timestamp_offset;
    }
    if (duration != GST_CLOCK_TIME_NONE) {
      buf->omx_buf->nTickCount =
          gst_util_uint64_scale (buf->omx_buf->nFilledLen, duration,
          gst_buffer_get_size (frame->sink_buffer));
      self->last_upstream_ts += duration;
    }

    if (offset == 0) {
      BufferIdentification *id = g_slice_new0 (BufferIdentification);

      if (!GST_BUFFER_FLAG_IS_SET (frame->sink_buffer,
              GST_BUFFER_FLAG_DELTA_UNIT))
        buf->omx_buf->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;

      id->timestamp = buf->omx_buf->nTimeStamp;
      frame->coder_hook = id;
      frame->coder_hook_destroy_notify =
          (GDestroyNotify) buffer_identification_free;
    }

    /* TODO: Set flags
     *   - OMX_BUFFERFLAG_DECODEONLY for buffers that are outside
     *     the segment
     *   - OMX_BUFFERFLAG_ENDOFFRAME for parsed input
     */

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

too_large_codec_data:
  {
    GST_ELEMENT_ERROR (self, STREAM, FORMAT, (NULL),
        ("codec_data larger than supported by OpenMAX port (%u > %u)",
            gst_buffer_get_size (codec_data),
            self->in_port->port_def.nBufferSize));
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

static GstFlowReturn
gst_omx_video_dec_finish (GstBaseVideoDecoder * decoder)
{
  GstOMXVideoDec *self;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Sending EOS to the component");

  /* Don't send EOS buffer twice, this doesn't work */
  if (self->eos) {
    GST_DEBUG_OBJECT (self, "Component is already EOS");
    return GST_BASE_VIDEO_DECODER_FLOW_DROPPED;
  }
  self->eos = TRUE;

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

  return GST_BASE_VIDEO_DECODER_FLOW_DROPPED;
}

static GstFlowReturn
gst_omx_video_dec_drain (GstOMXVideoDec * self)
{
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;

  GST_DEBUG_OBJECT (self, "Draining component");

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

  if (G_UNLIKELY(self->component->hacks & GST_OMX_HACK_DRAIN_MAY_NOT_RETURN)) {
    GTimeVal tv = { .tv_sec = 0, .tv_usec = 500000 };

    if (!g_cond_timed_wait (self->drain_cond, self->drain_lock, &tv))
      GST_WARNING_OBJECT (self, "Drain timed out");
    else
      GST_DEBUG_OBJECT (self, "Drained component");

  } else {
    g_cond_wait (self->drain_cond, self->drain_lock);
    GST_DEBUG_OBJECT (self, "Drained component");
  }

  g_mutex_unlock (self->drain_lock);
  GST_BASE_VIDEO_CODEC_STREAM_LOCK (self);

  self->started = FALSE;

  return GST_FLOW_OK;
}
