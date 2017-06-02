/*
 * Copyright (C) 2014-2017 SUMOMO Computer Association
 *     Authors Ayaka <ayaka@soulik.info>
 * Copyright (C) 2017 Collabora Ltd.
 *     Author: Nicolas Dufresne <nicolas.dufresne@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>

#include "gstv4l2videoenc.h"
#include "v4l2_calls.h"

#include <string.h>
#include <gst/gst-i18n-plugin.h>

GST_DEBUG_CATEGORY_STATIC (gst_v4l2_video_enc_debug);
#define GST_CAT_DEFAULT gst_v4l2_video_enc_debug

typedef struct
{
  gchar *device;
  GstCaps *sink_caps;
  GstCaps *src_caps;
} GstV4l2VideoEncCData;

enum
{
  PROP_0,
  V4L2_STD_OBJECT_PROPS,
};

#define gst_v4l2_video_enc_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstV4l2VideoEnc, gst_v4l2_video_enc,
    GST_TYPE_VIDEO_ENCODER);

static void
gst_v4l2_video_enc_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_OUTPUT_IO_MODE:
      gst_v4l2_object_set_property_helper (self->v4l2output,
          prop_id, value, pspec);
      break;
    case PROP_CAPTURE_IO_MODE:
      gst_v4l2_object_set_property_helper (self->v4l2capture,
          prop_id, value, pspec);
      break;

      /* By default, only set on output */
    default:
      if (!gst_v4l2_object_set_property_helper (self->v4l2output,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
  }
}

static void
gst_v4l2_video_enc_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (object);

  switch (prop_id) {
    case PROP_OUTPUT_IO_MODE:
      gst_v4l2_object_get_property_helper (self->v4l2output,
          prop_id, value, pspec);
      break;
    case PROP_CAPTURE_IO_MODE:
      gst_v4l2_object_get_property_helper (self->v4l2output,
          PROP_IO_MODE, value, pspec);
      break;

      /* By default read from output */
    default:
      if (!gst_v4l2_object_get_property_helper (self->v4l2output,
              prop_id, value, pspec)) {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
  }
}

static gboolean
gst_v4l2_video_enc_open (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Opening");

  if (!gst_v4l2_object_open (self->v4l2output))
    goto failure;

  if (!gst_v4l2_object_open_shared (self->v4l2capture, self->v4l2output))
    goto failure;

  self->probed_sinkcaps = gst_v4l2_object_get_caps (self->v4l2output,
      gst_v4l2_object_get_raw_caps ());

  if (gst_caps_is_empty (self->probed_sinkcaps))
    goto no_raw_format;

  self->probed_srccaps = gst_v4l2_object_get_caps (self->v4l2capture,
      gst_v4l2_object_get_codec_caps ());

  if (gst_caps_is_empty (self->probed_srccaps))
    goto no_encoded_format;

  return TRUE;

no_encoded_format:
  GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
      (_("Encoder on device %s has no supported output format"),
          self->v4l2output->videodev), (NULL));
  goto failure;


no_raw_format:
  GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
      (_("Encoder on device %s has no supported input format"),
          self->v4l2output->videodev), (NULL));
  goto failure;

failure:
  if (GST_V4L2_IS_OPEN (self->v4l2output))
    gst_v4l2_object_close (self->v4l2output);

  if (GST_V4L2_IS_OPEN (self->v4l2capture))
    gst_v4l2_object_close (self->v4l2capture);

  gst_caps_replace (&self->probed_srccaps, NULL);
  gst_caps_replace (&self->probed_sinkcaps, NULL);

  return FALSE;
}

static gboolean
gst_v4l2_video_enc_close (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Closing");

  gst_v4l2_object_close (self->v4l2output);
  gst_v4l2_object_close (self->v4l2capture);
  gst_caps_replace (&self->probed_srccaps, NULL);
  gst_caps_replace (&self->probed_sinkcaps, NULL);

  return TRUE;
}

static gboolean
gst_v4l2_video_enc_start (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Starting");

  gst_v4l2_object_unlock (self->v4l2output);
  g_atomic_int_set (&self->active, TRUE);
  self->output_flow = GST_FLOW_OK;

  return TRUE;
}

static gboolean
gst_v4l2_video_enc_stop (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Stopping");

  gst_v4l2_object_unlock (self->v4l2output);
  gst_v4l2_object_unlock (self->v4l2capture);

  /* Wait for capture thread to stop */
  gst_pad_stop_task (encoder->srcpad);

  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);
  self->output_flow = GST_FLOW_OK;
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  /* Should have been flushed already */
  g_assert (g_atomic_int_get (&self->active) == FALSE);
  g_assert (g_atomic_int_get (&self->processing) == FALSE);

  gst_v4l2_object_stop (self->v4l2output);
  gst_v4l2_object_stop (self->v4l2capture);

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  GST_DEBUG_OBJECT (self, "Stopped");

  return TRUE;
}

static gboolean
gst_v4l2_encoder_cmd (GstV4l2Object * v4l2object, guint cmd, guint flags)
{
  struct v4l2_encoder_cmd ecmd = { 0, };

  GST_DEBUG_OBJECT (v4l2object->element,
      "sending v4l2 encoder command %u with flags %u", cmd, flags);

  if (!GST_V4L2_IS_OPEN (v4l2object))
    return FALSE;

  ecmd.cmd = cmd;
  ecmd.flags = flags;
  if (v4l2_ioctl (v4l2object->video_fd, VIDIOC_ENCODER_CMD, &ecmd) < 0)
    goto ecmd_failed;

  return TRUE;

ecmd_failed:
  if (errno == ENOTTY) {
    GST_INFO_OBJECT (v4l2object->element,
        "Failed to send encoder command %u with flags %u for '%s'. (%s)",
        cmd, flags, v4l2object->videodev, g_strerror (errno));
  } else {
    GST_ERROR_OBJECT (v4l2object->element,
        "Failed to send encoder command %u with flags %u for '%s'. (%s)",
        cmd, flags, v4l2object->videodev, g_strerror (errno));
  }
  return FALSE;
}

static GstFlowReturn
gst_v4l2_video_enc_finish (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;

  if (gst_pad_get_task_state (encoder->srcpad) != GST_TASK_STARTED)
    goto done;

  GST_DEBUG_OBJECT (self, "Finishing encoding");

  /* drop the stream lock while draining, so remaining buffers can be
   * pushed from the src pad task thread */
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  if (gst_v4l2_encoder_cmd (self->v4l2capture, V4L2_ENC_CMD_STOP, 0)) {
    GstTask *task = encoder->srcpad->task;

    /* Wait for the task to be drained */
    GST_OBJECT_LOCK (task);
    while (GST_TASK_STATE (task) == GST_TASK_STARTED)
      GST_TASK_WAIT (task);
    GST_OBJECT_UNLOCK (task);
    ret = GST_FLOW_FLUSHING;
  }

  /* and ensure the processing thread has stopped in case another error
   * occured. */
  gst_v4l2_object_unlock (self->v4l2capture);
  gst_pad_stop_task (encoder->srcpad);
  GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

  if (ret == GST_FLOW_FLUSHING)
    ret = self->output_flow;

  GST_DEBUG_OBJECT (encoder, "Done draining buffers");

done:
  return ret;
}

static gboolean
gst_v4l2_video_enc_set_format (GstVideoEncoder * encoder,
    GstVideoCodecState * state)
{
  gboolean ret = TRUE;
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstV4l2Error error = GST_V4L2_ERROR_INIT;
  GstCaps *outcaps;
  GstVideoCodecState *output;

  GST_DEBUG_OBJECT (self, "Setting format: %" GST_PTR_FORMAT, state->caps);

  if (self->input_state) {
    if (gst_v4l2_object_caps_equal (self->v4l2output, state->caps)) {
      GST_DEBUG_OBJECT (self, "Compatible caps");
      return TRUE;
    }

    if (gst_v4l2_video_enc_finish (encoder) != GST_FLOW_OK)
      return FALSE;

    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  outcaps = gst_pad_get_pad_template_caps (encoder->srcpad);
  outcaps = gst_caps_make_writable (outcaps);
  output = gst_video_encoder_set_output_state (encoder, outcaps, state);
  gst_video_codec_state_unref (output);

  if (!gst_video_encoder_negotiate (encoder))
    return FALSE;

  if (!gst_v4l2_object_set_format (self->v4l2output, state->caps, &error)) {
    gst_v4l2_error (self, &error);
    return FALSE;
  }

  self->input_state = gst_video_codec_state_ref (state);

  GST_DEBUG_OBJECT (self, "output caps: %" GST_PTR_FORMAT, state->caps);

  return ret;
}

static gboolean
gst_v4l2_video_enc_flush (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);

  GST_DEBUG_OBJECT (self, "Flushing");

  /* Ensure the processing thread has stopped for the reverse playback
   * iscount case */
  if (g_atomic_int_get (&self->processing)) {
    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

    gst_v4l2_object_unlock_stop (self->v4l2output);
    gst_v4l2_object_unlock_stop (self->v4l2capture);
    gst_pad_stop_task (encoder->srcpad);

    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);

  }

  self->output_flow = GST_FLOW_OK;

  gst_v4l2_object_unlock_stop (self->v4l2output);
  gst_v4l2_object_unlock_stop (self->v4l2capture);

  return TRUE;
}

static gboolean
gst_v4l2_video_enc_negotiate (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  gboolean ret;

  ret = GST_VIDEO_ENCODER_CLASS (parent_class)->negotiate (encoder);

  if (!gst_buffer_pool_set_active (GST_BUFFER_POOL (self->v4l2capture->pool),
          TRUE)) {
    GST_WARNING_OBJECT (self, "Could not activate capture buffer pool.");
    ret = FALSE;
  }

  return ret;
}

static GstVideoCodecFrame *
gst_v4l2_video_enc_get_oldest_frame (GstVideoEncoder * encoder)
{
  GstVideoCodecFrame *frame = NULL;
  GList *frames, *l;
  gint count = 0;

  frames = gst_video_encoder_get_frames (encoder);

  for (l = frames; l != NULL; l = l->next) {
    GstVideoCodecFrame *f = l->data;

    if (!frame || frame->pts > f->pts)
      frame = f;

    count++;
  }

  if (frame) {
    GST_LOG_OBJECT (encoder,
        "Oldest frame is %d %" GST_TIME_FORMAT
        " and %d frames left",
        frame->system_frame_number, GST_TIME_ARGS (frame->pts), count - 1);
    gst_video_codec_frame_ref (frame);
  }

  g_list_free_full (frames, (GDestroyNotify) gst_video_codec_frame_unref);

  return frame;
}

static void
gst_v4l2_video_enc_loop (GstVideoEncoder * encoder)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstVideoCodecFrame *frame;
  GstBuffer *buffer = NULL;
  GstFlowReturn ret;

  GST_LOG_OBJECT (encoder, "Allocate output buffer");

  buffer = gst_video_encoder_allocate_output_buffer (encoder,
      self->v4l2capture->info.size);

  if (NULL == buffer) {
    ret = GST_FLOW_FLUSHING;
    goto beach;
  }


  /* FIXME Check if buffer isn't the last one here */

  GST_LOG_OBJECT (encoder, "Process output buffer");
  ret =
      gst_v4l2_buffer_pool_process (GST_V4L2_BUFFER_POOL
      (self->v4l2capture->pool), &buffer);

  if (ret != GST_FLOW_OK)
    goto beach;

  frame = gst_v4l2_video_enc_get_oldest_frame (encoder);

  if (frame) {
    frame->output_buffer = buffer;
    buffer = NULL;
    ret = gst_video_encoder_finish_frame (encoder, frame);

    if (ret != GST_FLOW_OK)
      goto beach;
  } else {
    GST_WARNING_OBJECT (encoder, "Encoder is producing too many buffers");
    gst_buffer_unref (buffer);
  }

  return;

beach:
  GST_DEBUG_OBJECT (encoder, "Leaving output thread");

  gst_buffer_replace (&buffer, NULL);
  self->output_flow = ret;
  g_atomic_int_set (&self->processing, FALSE);
  gst_v4l2_object_unlock (self->v4l2output);
  gst_pad_pause_task (encoder->srcpad);
}

static void
gst_v4l2_video_enc_loop_stopped (GstV4l2VideoEnc * self)
{
  if (g_atomic_int_get (&self->processing)) {
    GST_DEBUG_OBJECT (self, "Early stop of encoding thread");
    self->output_flow = GST_FLOW_FLUSHING;
    g_atomic_int_set (&self->processing, FALSE);
  }

  GST_DEBUG_OBJECT (self, "Encoding task destroyed: %s",
      gst_flow_get_name (self->output_flow));

}

static GstFlowReturn
gst_v4l2_video_enc_handle_frame (GstVideoEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (self, "Handling frame %d", frame->system_frame_number);

  if (G_UNLIKELY (!g_atomic_int_get (&self->active)))
    goto flushing;

  if (gst_pad_get_task_state (GST_VIDEO_DECODER_SRC_PAD (self)) ==
      GST_TASK_STOPPED) {
    GstBufferPool *pool = GST_BUFFER_POOL (self->v4l2output->pool);

    /* It possible that the processing thread stopped due to an error */
    if (self->output_flow != GST_FLOW_OK &&
        self->output_flow != GST_FLOW_FLUSHING) {
      GST_DEBUG_OBJECT (self, "Processing loop stopped with error, leaving");
      ret = self->output_flow;
      goto drop;
    }

    /* Ensure input internal pool is active */
    if (!gst_buffer_pool_is_active (pool)) {
      GstStructure *config = gst_buffer_pool_get_config (pool);
      guint min = MAX (self->v4l2output->min_buffers, GST_V4L2_MIN_BUFFERS);

      gst_buffer_pool_config_set_params (config, self->input_state->caps,
          self->v4l2output->info.size, min, min);

      /* There is no reason to refuse this config */
      if (!gst_buffer_pool_set_config (pool, config))
        goto activate_failed;

      if (!gst_buffer_pool_set_active (pool, TRUE))
        goto activate_failed;
    }

    GST_DEBUG_OBJECT (self, "Starting encoding thread");

    /* Start the processing task, when it quits, the task will disable input
     * processing to unlock input if draining, or prevent potential block */
    if (!gst_pad_start_task (encoder->srcpad,
            (GstTaskFunction) gst_v4l2_video_enc_loop, self,
            (GDestroyNotify) gst_v4l2_video_enc_loop_stopped))
      goto start_task_failed;
  }

  if (frame->input_buffer) {
    GST_VIDEO_ENCODER_STREAM_UNLOCK (encoder);
    ret =
        gst_v4l2_buffer_pool_process (GST_V4L2_BUFFER_POOL
        (self->v4l2output->pool), &frame->input_buffer);
    GST_VIDEO_ENCODER_STREAM_LOCK (encoder);

    if (ret == GST_FLOW_FLUSHING) {
      if (gst_pad_get_task_state (GST_VIDEO_DECODER_SRC_PAD (self)) !=
          GST_TASK_STARTED)
        ret = self->output_flow;
      goto drop;
    } else if (ret != GST_FLOW_OK) {
      goto process_failed;
    }
  }

  gst_video_codec_frame_unref (frame);
  return ret;

  /* ERRORS */
activate_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, SETTINGS,
        (_("Failed to allocate required memory.")),
        ("Buffer pool activation failed"));
    return GST_FLOW_ERROR;

  }
flushing:
  {
    ret = GST_FLOW_FLUSHING;
    goto drop;
  }
start_task_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        (_("Failed to start encoding thread.")), (NULL));
    g_atomic_int_set (&self->processing, FALSE);
    ret = GST_FLOW_ERROR;
    goto drop;
  }
process_failed:
  {
    GST_ELEMENT_ERROR (self, RESOURCE, FAILED,
        (_("Failed to process frame.")),
        ("Maybe be due to not enough memory or failing driver"));
    ret = GST_FLOW_ERROR;
    goto drop;
  }
drop:
  {
    gst_video_encoder_finish_frame (encoder, frame);
    return ret;
  }
}

static gboolean
gst_v4l2_video_enc_decide_allocation (GstVideoEncoder *
    encoder, GstQuery * query)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  GstVideoCodecState *state = gst_video_encoder_get_output_state (encoder);
  GstV4l2Error error = GST_V4L2_ERROR_INIT;
  GstClockTime latency;
  gboolean ret = FALSE;

  /* We need to set the format here, since this is called right after
   * GstVideoEncoder have set the width, height and framerate into the state
   * caps. These are needed by the driver to calculate the buffer size and to
   * implement bitrate adaptation. */
  if (!gst_v4l2_object_set_format (self->v4l2capture, state->caps, &error)) {
    gst_v4l2_error (self, &error);
    ret = FALSE;
    goto done;
  }

  if (gst_v4l2_object_decide_allocation (self->v4l2capture, query)) {
    GstVideoEncoderClass *enc_class = GST_VIDEO_ENCODER_CLASS (parent_class);
    ret = enc_class->decide_allocation (encoder, query);
  }

  /* FIXME This may not be entirely correct, as encoder may keep some
   * observation withouth delaying the encoding. Linux Media API need some
   * more work to explicitly expressed the decoder / encoder latency. This
   * value will then become max latency, and the reported driver latency would
   * become the min latency. */
  latency = self->v4l2capture->min_buffers * self->v4l2capture->duration;
  gst_video_encoder_set_latency (encoder, latency, latency);

done:
  gst_video_codec_state_unref (state);
  return ret;
}

static gboolean
gst_v4l2_video_enc_propose_allocation (GstVideoEncoder *
    encoder, GstQuery * query)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  gboolean ret = FALSE;

  GST_DEBUG_OBJECT (self, "called");

  if (query == NULL)
    ret = TRUE;
  else
    ret = gst_v4l2_object_propose_allocation (self->v4l2output, query);

  if (ret)
    ret = GST_VIDEO_ENCODER_CLASS (parent_class)->propose_allocation (encoder,
        query);

  return ret;
}

static gboolean
gst_v4l2_video_enc_src_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = TRUE;
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:{
      GstCaps *filter, *result = NULL;
      GstPad *pad = GST_VIDEO_ENCODER_SRC_PAD (encoder);

      gst_query_parse_caps (query, &filter);

      /* FIXME Try and not probe the entire encoder, but only the implement
       * subclass format */
      if (self->probed_srccaps) {
        GstCaps *tmpl = gst_pad_get_pad_template_caps (pad);
        result = gst_caps_intersect (tmpl, self->probed_srccaps);
        gst_caps_unref (tmpl);
      } else
        result = gst_pad_get_pad_template_caps (pad);

      if (filter) {
        GstCaps *tmp = result;
        result =
            gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (tmp);
      }

      GST_DEBUG_OBJECT (self, "Returning src caps %" GST_PTR_FORMAT, result);

      gst_query_set_caps_result (query, result);
      gst_caps_unref (result);
      break;
    }

    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->src_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_v4l2_video_enc_sink_query (GstVideoEncoder * encoder, GstQuery * query)
{
  gboolean ret = TRUE;
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CAPS:{
      GstCaps *filter, *result = NULL;
      GstPad *pad = GST_VIDEO_ENCODER_SINK_PAD (encoder);

      gst_query_parse_caps (query, &filter);

      if (self->probed_sinkcaps)
        result = gst_caps_ref (self->probed_sinkcaps);
      else
        result = gst_pad_get_pad_template_caps (pad);

      if (filter) {
        GstCaps *tmp = result;
        result =
            gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
        gst_caps_unref (tmp);
      }

      GST_DEBUG_OBJECT (self, "Returning sink caps %" GST_PTR_FORMAT, result);

      gst_query_set_caps_result (query, result);
      gst_caps_unref (result);
      break;
    }

    default:
      ret = GST_VIDEO_ENCODER_CLASS (parent_class)->sink_query (encoder, query);
      break;
  }

  return ret;
}

static gboolean
gst_v4l2_video_enc_sink_event (GstVideoEncoder * encoder, GstEvent * event)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (encoder);
  gboolean ret;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      GST_DEBUG_OBJECT (self, "flush start");
      gst_v4l2_object_unlock (self->v4l2output);
      gst_v4l2_object_unlock (self->v4l2capture);
      break;
    default:
      break;
  }

  ret = GST_VIDEO_ENCODER_CLASS (parent_class)->sink_event (encoder, event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
      gst_pad_stop_task (encoder->srcpad);
      GST_DEBUG_OBJECT (self, "flush start done");
    default:
      break;
  }

  return ret;
}

static GstStateChangeReturn
gst_v4l2_video_enc_change_state (GstElement * element,
    GstStateChange transition)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (element);

  if (transition == GST_STATE_CHANGE_PAUSED_TO_READY) {
    g_atomic_int_set (&self->active, FALSE);
    gst_v4l2_object_unlock (self->v4l2output);
    gst_v4l2_object_unlock (self->v4l2capture);
  }

  return GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
}

static void
gst_v4l2_video_enc_dispose (GObject * object)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (object);

  gst_caps_replace (&self->probed_sinkcaps, NULL);
  gst_caps_replace (&self->probed_srccaps, NULL);

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_v4l2_video_enc_finalize (GObject * object)
{
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (object);

  gst_v4l2_object_destroy (self->v4l2capture);
  gst_v4l2_object_destroy (self->v4l2output);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_v4l2_video_enc_init (GstV4l2VideoEnc * self)
{
  /* V4L2 object are created in subinstance_init */
}

static void
gst_v4l2_video_enc_subinstance_init (GTypeInstance * instance, gpointer g_class)
{
  GstV4l2VideoEncClass *klass = GST_V4L2_VIDEO_ENC_CLASS (g_class);
  GstV4l2VideoEnc *self = GST_V4L2_VIDEO_ENC (instance);

  self->v4l2output = gst_v4l2_object_new (GST_ELEMENT (self),
      V4L2_BUF_TYPE_VIDEO_OUTPUT, klass->default_device,
      gst_v4l2_get_output, gst_v4l2_set_output, NULL);
  self->v4l2output->no_initial_format = TRUE;
  self->v4l2output->keep_aspect = FALSE;

  self->v4l2capture = gst_v4l2_object_new (GST_ELEMENT (self),
      V4L2_BUF_TYPE_VIDEO_CAPTURE, klass->default_device,
      gst_v4l2_get_input, gst_v4l2_set_input, NULL);
  self->v4l2capture->no_initial_format = TRUE;
  self->v4l2output->keep_aspect = FALSE;
}

static void
gst_v4l2_video_enc_class_init (GstV4l2VideoEncClass * klass)
{
  GstElementClass *element_class;
  GObjectClass *gobject_class;
  GstVideoEncoderClass *video_encoder_class;

  parent_class = g_type_class_peek_parent (klass);

  element_class = (GstElementClass *) klass;
  gobject_class = (GObjectClass *) klass;
  video_encoder_class = (GstVideoEncoderClass *) klass;

  GST_DEBUG_CATEGORY_INIT (gst_v4l2_video_enc_debug, "v4l2videoenc", 0,
      "V4L2 Video Encoder");

  gobject_class->dispose = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_dispose);
  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_finalize);
  gobject_class->set_property =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_set_property);
  gobject_class->get_property =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_get_property);

  video_encoder_class->open = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_open);
  video_encoder_class->close = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_close);
  video_encoder_class->start = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_start);
  video_encoder_class->stop = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_stop);
  video_encoder_class->finish = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_finish);
  video_encoder_class->flush = GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_flush);
  video_encoder_class->set_format =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_set_format);
  video_encoder_class->negotiate =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_negotiate);
  video_encoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_decide_allocation);
  video_encoder_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_propose_allocation);
  video_encoder_class->sink_query =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_sink_query);
  video_encoder_class->src_query =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_src_query);
  video_encoder_class->sink_event =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_sink_event);
  video_encoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_handle_frame);

  element_class->change_state =
      GST_DEBUG_FUNCPTR (gst_v4l2_video_enc_change_state);

  gst_v4l2_object_install_m2m_properties_helper (gobject_class);
}

static void
gst_v4l2_video_enc_subclass_init (gpointer g_class, gpointer data)
{
  GstV4l2VideoEncClass *klass = GST_V4L2_VIDEO_ENC_CLASS (g_class);
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
  GstV4l2VideoEncCData *cdata = data;

  klass->default_device = cdata->device;

  /* Note: gst_pad_template_new() take the floating ref from the caps */
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("sink", GST_PAD_SINK, GST_PAD_ALWAYS,
          cdata->sink_caps));
  gst_element_class_add_pad_template (element_class,
      gst_pad_template_new ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
          cdata->src_caps));

  g_free (cdata);
}

/* Probing functions */
gboolean
gst_v4l2_video_enc_register (GstPlugin * plugin, GType type,
    const char *codec, const gchar * basename, const gchar * device_path,
    GstCaps * sink_caps, GstCaps * codec_caps, GstCaps * src_caps)
{
  GstCaps *filtered_caps;
  GTypeQuery type_query;
  GTypeInfo type_info = { 0, };
  GType subtype;
  gchar *type_name;
  GstV4l2VideoEncCData *cdata;

  filtered_caps = gst_caps_intersect (src_caps, codec_caps);

  cdata = g_new0 (GstV4l2VideoEncCData, 1);
  cdata->device = g_strdup (device_path);
  cdata->sink_caps = gst_caps_ref (sink_caps);
  cdata->src_caps = gst_caps_ref (filtered_caps);

  g_type_query (type, &type_query);
  memset (&type_info, 0, sizeof (type_info));
  type_info.class_size = type_query.class_size;
  type_info.instance_size = type_query.instance_size;
  type_info.class_init = gst_v4l2_video_enc_subclass_init;
  type_info.class_data = cdata;
  type_info.instance_init = gst_v4l2_video_enc_subinstance_init;

  /* The first encoder to be registered should use a constant name, like
   * v4l2h264enc, for any additional encoders, we create unique names. Encoder
   * names may change between boots, so this should help gain stable names for
   * the most common use cases. */
  type_name = g_strdup_printf ("v4l2%senc", codec);

  if (g_type_from_name (type_name) != 0) {
    g_free (type_name);
    type_name = g_strdup_printf ("v4l2%s%senc", basename, codec);
  }

  subtype = g_type_register_static (type, type_name, &type_info, 0);
  gst_element_register (plugin, type_name, GST_RANK_PRIMARY + 1, subtype);

  g_free (type_name);

  return TRUE;
}
