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
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
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

static gboolean gst_omx_video_dec_open (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_close (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_start (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_omx_video_dec_reset (GstVideoDecoder * decoder,
    gboolean hard);
static GstFlowReturn gst_omx_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static GstFlowReturn gst_omx_video_dec_finish (GstVideoDecoder * decoder);
static gboolean gst_omx_video_dec_decide_allocation (GstVideoDecoder * bdec,
    GstQuery * query);

static GstFlowReturn gst_omx_video_dec_drain (GstOMXVideoDec * self,
    gboolean is_eos);

enum
{
  PROP_0
};

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_video_dec_debug_category, "omxvideodec", 0, \
      "debug category for gst-omx video decoder base class");


G_DEFINE_ABSTRACT_TYPE_WITH_CODE (GstOMXVideoDec, gst_omx_video_dec,
    GST_TYPE_VIDEO_DECODER, DEBUG_INIT);

static void
gst_omx_video_dec_class_init (GstOMXVideoDecClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);
  GstVideoDecoderClass *video_decoder_class = GST_VIDEO_DECODER_CLASS (klass);

  gobject_class->finalize = gst_omx_video_dec_finalize;

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_change_state);

  video_decoder_class->open = GST_DEBUG_FUNCPTR (gst_omx_video_dec_open);
  video_decoder_class->close = GST_DEBUG_FUNCPTR (gst_omx_video_dec_close);
  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_omx_video_dec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_omx_video_dec_stop);
  video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_omx_video_dec_reset);
  video_decoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_set_format);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_handle_frame);
  video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_omx_video_dec_finish);
  video_decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_omx_video_dec_decide_allocation);

  klass->cdata.default_src_template_caps = "video/x-raw, "
      "width = " GST_VIDEO_SIZE_RANGE ", "
      "height = " GST_VIDEO_SIZE_RANGE ", " "framerate = " GST_VIDEO_FPS_RANGE;
}

static void
gst_omx_video_dec_init (GstOMXVideoDec * self)
{
  gst_video_decoder_set_packetized (GST_VIDEO_DECODER (self), TRUE);

  g_mutex_init (&self->drain_lock);
  g_cond_init (&self->drain_cond);
}

static gboolean
gst_omx_video_dec_open (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (decoder);
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
gst_omx_video_dec_close (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self = GST_OMX_VIDEO_DEC (decoder);

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

  g_mutex_clear (&self->drain_lock);
  g_cond_clear (&self->drain_cond);

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

      g_mutex_lock (&self->drain_lock);
      self->draining = FALSE;
      g_cond_broadcast (&self->drain_cond);
      g_mutex_unlock (&self->drain_lock);
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
      break;
    default:
      break;
  }

  return ret;
}

#define MAX_FRAME_DIST_TICKS  (5 * OMX_TICKS_PER_SECOND)
#define MAX_FRAME_DIST_FRAMES (100)

static GstVideoCodecFrame *
_find_nearest_frame (GstOMXVideoDec * self, GstOMXBuffer * buf)
{
  GList *l, *best_l = NULL;
  GList *finish_frames = NULL;
  GstVideoCodecFrame *best = NULL;
  guint64 best_timestamp = 0;
  guint64 best_diff = G_MAXUINT64;
  BufferIdentification *best_id = NULL;
  GList *frames;

  frames = gst_video_decoder_get_frames (GST_VIDEO_DECODER (self));

  for (l = frames; l; l = l->next) {
    GstVideoCodecFrame *tmp = l->data;
    BufferIdentification *id = gst_video_codec_frame_get_user_data (tmp);
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
    for (l = frames; l && l != best_l; l = l->next) {
      GstVideoCodecFrame *tmp = l->data;
      BufferIdentification *id = gst_video_codec_frame_get_user_data (tmp);
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
        finish_frames =
            g_list_prepend (finish_frames, gst_video_codec_frame_ref (tmp));
      }
    }
  }

  if (finish_frames) {
    g_warning ("Too old frames, bug in decoder -- please file a bug");
    for (l = finish_frames; l; l = l->next) {
      gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), l->data);
    }
  }

  if (best)
    gst_video_codec_frame_ref (best);

  g_list_foreach (frames, (GFunc) gst_video_codec_frame_unref, NULL);
  g_list_free (frames);

  return best;
}

static gboolean
gst_omx_video_dec_fill_buffer (GstOMXVideoDec * self, GstOMXBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstVideoCodecState *state =
      gst_video_decoder_get_output_state (GST_VIDEO_DECODER (self));
  GstVideoInfo *vinfo = &state->info;
  OMX_PARAM_PORTDEFINITIONTYPE *port_def = &self->out_port->port_def;
  gboolean ret = FALSE;
  GstVideoFrame frame;

  if (vinfo->width != port_def->format.video.nFrameWidth ||
      vinfo->height != port_def->format.video.nFrameHeight) {
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

  switch (vinfo->finfo->format) {
    case GST_VIDEO_FORMAT_I420:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride;

      gst_video_frame_map (&frame, vinfo, outbuf, GST_MAP_WRITE);
      for (i = 0; i < 3; i++) {
        if (i == 0) {
          src_stride = port_def->format.video.nStride;
          dest_stride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, i);

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        } else {
          src_stride = port_def->format.video.nStride / 2;
          dest_stride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, i);

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

        dest = GST_VIDEO_FRAME_COMP_DATA (&frame, i);
        height = GST_VIDEO_FRAME_HEIGHT (&frame);

        for (j = 0; j < height; j++) {
          memcpy (dest, src, MIN (src_stride, dest_stride));
          src += src_stride;
          dest += dest_stride;
        }
      }
      gst_video_frame_unmap (&frame);
      ret = TRUE;
      break;
    }
    case GST_VIDEO_FORMAT_NV12:{
      gint i, j, height;
      guint8 *src, *dest;
      gint src_stride, dest_stride;

      gst_video_frame_map (&frame, vinfo, outbuf, GST_MAP_WRITE);
      for (i = 0; i < 2; i++) {
        if (i == 0) {
          src_stride = port_def->format.video.nStride;
          dest_stride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, i);

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        } else {
          src_stride = port_def->format.video.nStride;
          dest_stride = GST_VIDEO_FRAME_COMP_STRIDE (&frame, i);

          /* XXX: Try this if no stride was set */
          if (src_stride == 0)
            src_stride = dest_stride;
        }

        src = inbuf->omx_buf->pBuffer + inbuf->omx_buf->nOffset;
        if (i == 1)
          src +=
              port_def->format.video.nSliceHeight *
              port_def->format.video.nStride;

        dest = GST_VIDEO_FRAME_COMP_DATA (&frame, i);
        height = GST_VIDEO_FRAME_HEIGHT (&frame);

        for (j = 0; j < height; j++) {
          memcpy (dest, src, MIN (src_stride, dest_stride));
          src += src_stride;
          dest += dest_stride;
        }
      }
      gst_video_frame_unmap (&frame);
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

  gst_video_codec_state_unref (state);

  return ret;
}

static void
gst_omx_video_dec_loop (GstOMXVideoDec * self)
{
  GstOMXVideoDecClass *klass;
  GstOMXPort *port = self->out_port;
  GstOMXBuffer *buf = NULL;
  GstVideoCodecFrame *frame;
  GstFlowReturn flow_ret = GST_FLOW_OK;
  GstOMXAcquireBufferReturn acq_return;
  GstClockTimeDiff deadline;
  gboolean is_eos;

  klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

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

  if (!gst_pad_has_current_caps (GST_VIDEO_DECODER_SRC_PAD (self))
      || acq_return == GST_OMX_ACQUIRE_BUFFER_RECONFIGURED) {
    GstVideoCodecState *state;
    OMX_PARAM_PORTDEFINITIONTYPE port_def;
    GstVideoFormat format;

    GST_DEBUG_OBJECT (self, "Port settings have changed, updating caps");

    GST_VIDEO_DECODER_STREAM_LOCK (self);
    gst_omx_port_get_port_definition (port, &port_def);
    g_assert (port_def.format.video.eCompressionFormat ==
        OMX_VIDEO_CodingUnused);

    switch (port_def.format.video.eColorFormat) {
      case OMX_COLOR_FormatYUV420Planar:
      case OMX_COLOR_FormatYUV420PackedPlanar:
        GST_DEBUG_OBJECT (self, "Output is I420 (%d)",
            port_def.format.video.eColorFormat);
        format = GST_VIDEO_FORMAT_I420;
        break;
      case OMX_COLOR_FormatYUV420SemiPlanar:
        GST_DEBUG_OBJECT (self, "Output is NV12 (%d)",
            port_def.format.video.eColorFormat);
        format = GST_VIDEO_FORMAT_NV12;
        break;
      default:
        GST_ERROR_OBJECT (self, "Unsupported color format: %d",
            port_def.format.video.eColorFormat);
        if (buf)
          gst_omx_port_release_buffer (self->out_port, buf);
        GST_VIDEO_DECODER_STREAM_UNLOCK (self);
        goto caps_failed;
        break;
    }

    GST_DEBUG_OBJECT (self,
        "Setting output state: format %s, width %d, height %d",
        gst_video_format_to_string (format), port_def.format.video.nFrameWidth,
        port_def.format.video.nFrameHeight);

    state = gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
        format, port_def.format.video.nFrameWidth,
        port_def.format.video.nFrameHeight, self->input_state);

    /* Take framerate and pixel-aspect-ratio from sinkpad caps */

    if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self))) {
      if (buf)
        gst_omx_port_release_buffer (self->out_port, buf);
      gst_video_codec_state_unref (state);
      GST_VIDEO_DECODER_STREAM_UNLOCK (self);
      goto caps_failed;
    }

    gst_video_codec_state_unref (state);
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);

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

    GST_VIDEO_DECODER_STREAM_LOCK (self);
    frame = _find_nearest_frame (self, buf);

    is_eos = ! !(buf->omx_buf->nFlags & OMX_BUFFERFLAG_EOS);

    if (frame
        && (deadline = gst_video_decoder_get_max_decode_time
            (GST_VIDEO_DECODER (self), frame)) < 0) {
      GST_WARNING_OBJECT (self,
          "Frame is too late, dropping (deadline %" GST_TIME_FORMAT ")",
          GST_TIME_ARGS (-deadline));
      flow_ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
    } else if (!frame && buf->omx_buf->nFilledLen > 0) {
      GstBuffer *outbuf;

      /* This sometimes happens at EOS or if the input is not properly framed,
       * let's handle it gracefully by allocating a new buffer for the current
       * caps and filling it
       */

      GST_ERROR_OBJECT (self, "No corresponding frame found");

      outbuf =
          gst_video_decoder_allocate_output_buffer (GST_VIDEO_DECODER (self));

      if (!gst_omx_video_dec_fill_buffer (self, buf, outbuf)) {
        gst_buffer_unref (outbuf);
        gst_omx_port_release_buffer (self->out_port, buf);
        goto invalid_buffer;
      }

      flow_ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), outbuf);
    } else if (buf->omx_buf->nFilledLen > 0) {
      if ((flow_ret = gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER
                  (self), frame)) == GST_FLOW_OK) {
        /* FIXME: This currently happens because of a race condition too.
         * We first need to reconfigure the output port and then the input
         * port if both need reconfiguration.
         */
        if (!gst_omx_video_dec_fill_buffer (self, buf, frame->output_buffer)) {
          gst_buffer_replace (&frame->output_buffer, NULL);
          flow_ret =
              gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
          gst_omx_port_release_buffer (self->out_port, buf);
          goto invalid_buffer;
        }
        flow_ret =
            gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
      }
    } else if (frame != NULL) {
      flow_ret =
          gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
    }

    GST_DEBUG_OBJECT (self, "Read frame from component");

    if (is_eos || flow_ret == GST_FLOW_EOS) {
      g_mutex_lock (&self->drain_lock);
      if (self->draining) {
        GST_DEBUG_OBJECT (self, "Drained");
        self->draining = FALSE;
        g_cond_broadcast (&self->drain_cond);
      } else if (flow_ret == GST_FLOW_OK) {
        GST_DEBUG_OBJECT (self, "Component signalled EOS");
        flow_ret = GST_FLOW_EOS;
      }
      g_mutex_unlock (&self->drain_lock);
    } else {
      GST_DEBUG_OBJECT (self, "Finished frame: %s",
          gst_flow_get_name (flow_ret));
    }

    gst_omx_port_release_buffer (port, buf);

    self->downstream_flow_ret = flow_ret;
  } else {
    g_assert ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER));
    GST_VIDEO_DECODER_STREAM_LOCK (self);
    flow_ret = GST_FLOW_EOS;
  }

  if (flow_ret != GST_FLOW_OK)
    goto flow_error;

  GST_VIDEO_DECODER_STREAM_UNLOCK (self);

  return;

component_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED, (NULL),
        ("OpenMAX component in error state %s (0x%08x)",
            gst_omx_component_get_last_error_string (self->component),
            gst_omx_component_get_last_error (self->component)));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }

flushing:
  {
    GST_DEBUG_OBJECT (self, "Flushing -- stopping task");
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_FLUSHING;
    self->started = FALSE;
    return;
  }

flow_error:
  {
    if (flow_ret == GST_FLOW_EOS) {
      GST_DEBUG_OBJECT (self, "EOS");

      gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    } else if (flow_ret == GST_FLOW_NOT_LINKED || flow_ret < GST_FLOW_EOS) {
      GST_ELEMENT_ERROR (self, STREAM, FAILED, ("Internal data stream error."),
          ("stream stopped, reason %s", gst_flow_get_name (flow_ret)));

      gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self),
          gst_event_new_eos ());
      gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    }
    self->started = FALSE;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }

reconfigure_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Unable to reconfigure output port"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_ERROR;
    self->started = FALSE;
    return;
  }

invalid_buffer:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL),
        ("Invalid sized input buffer"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    return;
  }

caps_failed:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS, (NULL), ("Failed to set caps"));
    gst_pad_push_event (GST_VIDEO_DECODER_SRC_PAD (self), gst_event_new_eos ());
    gst_pad_pause_task (GST_VIDEO_DECODER_SRC_PAD (self));
    self->downstream_flow_ret = GST_FLOW_NOT_NEGOTIATED;
    self->started = FALSE;
    return;
  }
}

static gboolean
gst_omx_video_dec_start (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self;
  gboolean ret;

  self = GST_OMX_VIDEO_DEC (decoder);

  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  ret =
      gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, self, NULL);

  return ret;
}

static gboolean
gst_omx_video_dec_stop (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Stopping decoder");

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  gst_pad_stop_task (GST_VIDEO_DECODER_SRC_PAD (decoder));

  if (gst_omx_component_get_state (self->component, 0) > OMX_StateIdle)
    gst_omx_component_set_state (self->component, OMX_StateIdle);

  self->downstream_flow_ret = GST_FLOW_FLUSHING;
  self->started = FALSE;
  self->eos = FALSE;

  g_mutex_lock (&self->drain_lock);
  self->draining = FALSE;
  g_cond_broadcast (&self->drain_cond);
  g_mutex_unlock (&self->drain_lock);

  gst_omx_component_get_state (self->component, 5 * GST_SECOND);

  gst_buffer_replace (&self->codec_data, NULL);

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = NULL;

  GST_DEBUG_OBJECT (self, "Stopped decoder");

  return TRUE;
}

typedef struct
{
  GstVideoFormat format;
  OMX_COLOR_FORMATTYPE type;
} VideoNegotiationMap;

static void
video_negotiation_map_free (VideoNegotiationMap * m)
{
  g_slice_free (VideoNegotiationMap, m);
}

static gboolean
gst_omx_video_dec_negotiate (GstOMXVideoDec * self)
{
  GstOMXPort *port = self->out_port;
  GstVideoCodecState *state = self->input_state;
  GstVideoInfo *info = &state->info;
  OMX_VIDEO_PARAM_PORTFORMATTYPE param;
  OMX_ERRORTYPE err;
  GstCaps *comp_supported_caps;
  GList *negotiation_map = NULL, *l;
  GstCaps *intersection;
  GstVideoFormat format;
  gint old_index;
  GstStructure *s;
  const gchar *format_str;

  GST_DEBUG_OBJECT (self, "Trying to negotiate a video format with downstream");

  intersection = gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (self));

  GST_DEBUG_OBJECT (self, "Allowed downstream caps: %" GST_PTR_FORMAT,
      intersection);

  GST_OMX_INIT_STRUCT (&param);
  param.nPortIndex = port->index;
  param.nIndex = 0;
  if (info->fps_n == 0)
    param.xFramerate = 0;
  else
    param.xFramerate = (info->fps_n << 16) / (info->fps_d);

  old_index = -1;
  comp_supported_caps = gst_caps_new_empty ();
  do {
    VideoNegotiationMap *m;

    err =
        gst_omx_component_get_parameter (self->component,
        OMX_IndexParamVideoPortFormat, &param);

    /* FIXME: Workaround for Bellagio that simply always
     * returns the same value regardless of nIndex and
     * never returns OMX_ErrorNoMore
     */
    if (old_index == param.nIndex)
      break;

    if (err == OMX_ErrorNone || err == OMX_ErrorNoMore) {
      switch (param.eColorFormat) {
        case OMX_COLOR_FormatYUV420Planar:
        case OMX_COLOR_FormatYUV420PackedPlanar:
          m = g_slice_new0 (VideoNegotiationMap);
          m->format = GST_VIDEO_FORMAT_I420;
          m->type = param.eColorFormat;
          negotiation_map = g_list_append (negotiation_map, m);
          gst_caps_append_structure (comp_supported_caps,
              gst_structure_new ("video/x-raw",
                  "format", G_TYPE_STRING, "I420", NULL));
          GST_DEBUG_OBJECT (self, "Component supports I420 (%d) at index %d",
              param.eColorFormat, param.nIndex);
          break;
        case OMX_COLOR_FormatYUV420SemiPlanar:
          m = g_slice_new0 (VideoNegotiationMap);
          m->format = GST_VIDEO_FORMAT_NV12;
          m->type = param.eColorFormat;
          negotiation_map = g_list_append (negotiation_map, m);
          gst_caps_append_structure (comp_supported_caps,
              gst_structure_new ("video/x-raw",
                  "format", G_TYPE_STRING, "NV12", NULL));
          GST_DEBUG_OBJECT (self, "Component supports NV12 (%d) at index %d",
              param.eColorFormat, param.nIndex);
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

  gst_caps_unref (comp_supported_caps);

  if (gst_caps_is_empty (intersection)) {
    gst_caps_unref (intersection);
    GST_ERROR_OBJECT (self, "Empty caps");
    g_list_free_full (negotiation_map,
        (GDestroyNotify) video_negotiation_map_free);
    return FALSE;
  }

  intersection = gst_caps_truncate (intersection);
  intersection = gst_caps_fixate (intersection);

  s = gst_caps_get_structure (intersection, 0);
  format_str = gst_structure_get_string (s, "format");
  if (!format_str ||
      (format =
          gst_video_format_from_string (format_str)) ==
      GST_VIDEO_FORMAT_UNKNOWN) {
    GST_ERROR_OBJECT (self, "Invalid caps: %" GST_PTR_FORMAT, intersection);
    g_list_free_full (negotiation_map,
        (GDestroyNotify) video_negotiation_map_free);
    return FALSE;
  }

  for (l = negotiation_map; l; l = l->next) {
    VideoNegotiationMap *m = l->data;

    if (m->format == format) {
      param.eColorFormat = m->type;
      break;
    }
  }

  GST_DEBUG_OBJECT (self, "Negotiating color format %s (%d)", format_str,
      param.eColorFormat);

  /* We must find something here */
  g_assert (l != NULL);
  g_list_free_full (negotiation_map,
      (GDestroyNotify) video_negotiation_map_free);

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
gst_omx_video_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state)
{
  GstOMXVideoDec *self;
  GstOMXVideoDecClass *klass;
  GstVideoInfo *info = &state->info;
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
  is_format_change |= port_def.format.video.nFrameWidth != info->width;
  is_format_change |= port_def.format.video.nFrameHeight != info->height;
  is_format_change |= (port_def.format.video.xFramerate == 0
      && info->fps_n != 0)
      || (port_def.format.video.xFramerate !=
      (info->fps_n << 16) / (info->fps_d));
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
    if (self->input_state)
      gst_video_codec_state_unref (self->input_state);
    self->input_state = gst_video_codec_state_ref (state);
    return TRUE;
  }

  if (needs_disable && is_format_change) {
    GST_DEBUG_OBJECT (self, "Need to disable and drain decoder");

    gst_omx_video_dec_drain (self, FALSE);

    if (klass->cdata.hacks & GST_OMX_HACK_NO_COMPONENT_RECONFIGURE) {
      GST_VIDEO_DECODER_STREAM_UNLOCK (self);
      gst_omx_video_dec_stop (GST_VIDEO_DECODER (self));
      gst_omx_video_dec_close (GST_VIDEO_DECODER (self));

      GST_VIDEO_DECODER_STREAM_LOCK (self);

      if (!gst_omx_video_dec_open (GST_VIDEO_DECODER (self)))
        return FALSE;
      needs_disable = FALSE;
    } else {
      if (gst_omx_port_manual_reconfigure (self->in_port,
              TRUE) != OMX_ErrorNone)
        return FALSE;

      if (gst_omx_port_set_enabled (self->in_port, FALSE) != OMX_ErrorNone)
        return FALSE;
    }
    if (self->input_state)
      gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;

    GST_DEBUG_OBJECT (self, "Decoder drained and disabled");
  }

  port_def.format.video.nFrameWidth = info->width;
  port_def.format.video.nFrameHeight = info->height;
  if (info->fps_n == 0)
    port_def.format.video.xFramerate = 0;
  else
    port_def.format.video.xFramerate = (info->fps_n << 16) / (info->fps_d);

  GST_DEBUG_OBJECT (self, "Setting inport port definition");

  if (!gst_omx_port_update_port_definition (self->in_port, &port_def))
    return FALSE;

  GST_DEBUG_OBJECT (self, "Setting outport port definition");
  if (!gst_omx_port_update_port_definition (self->out_port, NULL))
    return FALSE;

  if (klass->set_format) {
    if (!klass->set_format (self, self->in_port, state)) {
      GST_ERROR_OBJECT (self, "Subclass failed to set the new format");
      return FALSE;
    }
  }

  gst_buffer_replace (&self->codec_data, state->codec_data);
  self->input_state = gst_video_codec_state_ref (state);

  if (!gst_omx_video_dec_negotiate (self)) {
    GST_LOG_OBJECT (self, "Negotiation failed, will get output format later");
  }

  GST_DEBUG_OBJECT (self, "Enabling component");

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
  GST_DEBUG_OBJECT (self, "Starting task again");

  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, decoder, NULL);

  return TRUE;
}

static gboolean
gst_omx_video_dec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  /* FIXME: Handle different values of hard */

  GST_DEBUG_OBJECT (self, "Resetting decoder");

  gst_omx_port_set_flushing (self->in_port, TRUE);
  gst_omx_port_set_flushing (self->out_port, TRUE);

  /* Wait until the srcpad loop is finished,
   * unlock GST_VIDEO_DECODER_STREAM_LOCK to prevent deadlocks
   * caused by using this lock from inside the loop function */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);
  GST_PAD_STREAM_LOCK (GST_VIDEO_DECODER_SRC_PAD (self));
  GST_PAD_STREAM_UNLOCK (GST_VIDEO_DECODER_SRC_PAD (self));
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  gst_omx_port_set_flushing (self->in_port, FALSE);
  gst_omx_port_set_flushing (self->out_port, FALSE);

  /* Start the srcpad loop again */
  self->last_upstream_ts = 0;
  self->eos = FALSE;
  self->downstream_flow_ret = GST_FLOW_OK;
  gst_pad_start_task (GST_VIDEO_DECODER_SRC_PAD (self),
      (GstTaskFunction) gst_omx_video_dec_loop, decoder, NULL);

  GST_DEBUG_OBJECT (self, "Reset decoder");

  return TRUE;
}

static GstFlowReturn
gst_omx_video_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame)
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

  timestamp = frame->pts;
  duration = frame->duration;

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

  while (offset < gst_buffer_get_size (frame->input_buffer)) {
    /* Make sure to release the base class stream lock, otherwise
     * _loop() can't call _finish_frame() and we might block forever
     * because no input buffers are released */
    GST_VIDEO_DECODER_STREAM_UNLOCK (self);
    acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
    GST_VIDEO_DECODER_STREAM_LOCK (self);

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
      GST_DEBUG_OBJECT (self, "Passing codec data to the component");

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
    GST_DEBUG_OBJECT (self, "Passing frame offset %d to the component", offset);

    /* Copy the buffer content in chunks of size as requested
     * by the port */
    gst_buffer_extract (frame->input_buffer, offset,
        buf->omx_buf->pBuffer + buf->omx_buf->nOffset,
        buf->omx_buf->nFilledLen);

    /* Interpolate timestamps if we're passing the buffer
     * in multiple chunks */
    if (offset != 0 && duration != GST_CLOCK_TIME_NONE) {
      timestamp_offset =
          gst_util_uint64_scale (offset, duration,
          gst_buffer_get_size (frame->input_buffer));
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
          gst_buffer_get_size (frame->input_buffer));
      self->last_upstream_ts += duration;
    }

    if (offset == 0) {
      BufferIdentification *id = g_slice_new0 (BufferIdentification);

      if (GST_VIDEO_CODEC_FRAME_IS_SYNC_POINT (frame))
        buf->omx_buf->nFlags |= OMX_BUFFERFLAG_SYNCFRAME;

      id->timestamp = buf->omx_buf->nTimeStamp;
      gst_video_codec_frame_set_user_data (frame, id,
          (GDestroyNotify) buffer_identification_free);
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

  GST_DEBUG_OBJECT (self, "Passed frame to component");

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
gst_omx_video_dec_finish (GstVideoDecoder * decoder)
{
  GstOMXVideoDec *self;

  self = GST_OMX_VIDEO_DEC (decoder);

  return gst_omx_video_dec_drain (self, TRUE);
}

static GstFlowReturn
gst_omx_video_dec_drain (GstOMXVideoDec * self, gboolean is_eos)
{
  GstOMXVideoDecClass *klass;
  GstOMXBuffer *buf;
  GstOMXAcquireBufferReturn acq_ret;

  GST_DEBUG_OBJECT (self, "Draining component");

  klass = GST_OMX_VIDEO_DEC_GET_CLASS (self);

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
  if (is_eos)
    self->eos = TRUE;

  if ((klass->cdata.hacks & GST_OMX_HACK_NO_EMPTY_EOS_BUFFER)) {
    GST_WARNING_OBJECT (self, "Component does not support empty EOS buffers");
    return GST_FLOW_OK;
  }

  /* Make sure to release the base class stream lock, otherwise
   * _loop() can't call _finish_frame() and we might block forever
   * because no input buffers are released */
  GST_VIDEO_DECODER_STREAM_UNLOCK (self);

  /* Send an EOS buffer to the component and let the base
   * class drop the EOS event. We will send it later when
   * the EOS buffer arrives on the output port. */
  acq_ret = gst_omx_port_acquire_buffer (self->in_port, &buf);
  if (acq_ret != GST_OMX_ACQUIRE_BUFFER_OK) {
    GST_VIDEO_DECODER_STREAM_LOCK (self);
    GST_ERROR_OBJECT (self, "Failed to acquire buffer for draining: %d",
        acq_ret);
    return GST_FLOW_ERROR;
  }

  g_mutex_lock (&self->drain_lock);
  self->draining = TRUE;
  buf->omx_buf->nFilledLen = 0;
  buf->omx_buf->nTimeStamp =
      gst_util_uint64_scale (self->last_upstream_ts, OMX_TICKS_PER_SECOND,
      GST_SECOND);
  buf->omx_buf->nTickCount = 0;
  buf->omx_buf->nFlags |= OMX_BUFFERFLAG_EOS;
  gst_omx_port_release_buffer (self->in_port, buf);
  GST_DEBUG_OBJECT (self, "Waiting until component is drained");

  if (G_UNLIKELY (self->component->hacks & GST_OMX_HACK_DRAIN_MAY_NOT_RETURN)) {
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
  GST_VIDEO_DECODER_STREAM_LOCK (self);

  self->started = FALSE;

  return GST_FLOW_OK;
}

static gboolean
gst_omx_video_dec_decide_allocation (GstVideoDecoder * bdec, GstQuery * query)
{
  GstBufferPool *pool;
  GstStructure *config;

  if (!GST_VIDEO_DECODER_CLASS
      (gst_omx_video_dec_parent_class)->decide_allocation (bdec, query))
    return FALSE;

  g_assert (gst_query_get_n_allocation_pools (query) > 0);
  gst_query_parse_nth_allocation_pool (query, 0, &pool, NULL, NULL, NULL);
  g_assert (pool != NULL);

  config = gst_buffer_pool_get_config (pool);
  if (gst_query_find_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL)) {
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);
  }
  gst_buffer_pool_set_config (pool, config);
  gst_object_unref (pool);

  return TRUE;
}
