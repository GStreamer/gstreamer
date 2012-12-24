/* 
 * Copyright (C) 2012 Collabora Ltd.
 *     Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
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

#include "gstmfcdec.h"

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (gst_mfc_dec_debug);
#define GST_CAT_DEFAULT gst_mfc_dec_debug

static gboolean gst_mfc_dec_start (GstVideoDecoder * decoder);
static gboolean gst_mfc_dec_stop (GstVideoDecoder * decoder);
static gboolean gst_mfc_dec_set_format (GstVideoDecoder * decoder,
    GstVideoCodecState * state);
static gboolean gst_mfc_dec_reset (GstVideoDecoder * decoder, gboolean hard);
static GstFlowReturn gst_mfc_dec_finish (GstVideoDecoder * decoder);
static GstFlowReturn gst_mfc_dec_handle_frame (GstVideoDecoder * decoder,
    GstVideoCodecFrame * frame);
static gboolean gst_mfc_dec_negotiate (GstVideoDecoder * decoder);
static gboolean gst_mfc_dec_decide_allocation (GstVideoDecoder * decoder,
    GstQuery * query);

static GstStaticPadTemplate gst_mfc_dec_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("video/x-h264, "
        "parsed = (boolean) true, "
        "stream-format = (string) byte-stream, " "alignment = (string) au")
    );

static GstStaticPadTemplate gst_mfc_dec_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE ("{ I420, YV12, NV12 }"))
    );

#define parent_class gst_mfc_dec_parent_class
G_DEFINE_TYPE (GstMFCDec, gst_mfc_dec, GST_TYPE_VIDEO_DECODER);

static void
gst_mfc_dec_class_init (GstMFCDecClass * klass)
{
  GstElementClass *element_class;
  GstVideoDecoderClass *video_decoder_class;

  element_class = (GstElementClass *) klass;
  video_decoder_class = (GstVideoDecoderClass *) klass;

  mfc_dec_init_debug ();
  fimc_init_debug ();

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mfc_dec_src_template));
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_mfc_dec_sink_template));

  gst_element_class_set_static_metadata (element_class,
      "Samsung Exynos MFC decoder",
      "Codec/Decoder/Video",
      "Decode video streams via Samsung Exynos",
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  video_decoder_class->start = GST_DEBUG_FUNCPTR (gst_mfc_dec_start);
  video_decoder_class->stop = GST_DEBUG_FUNCPTR (gst_mfc_dec_stop);
  video_decoder_class->finish = GST_DEBUG_FUNCPTR (gst_mfc_dec_finish);
  video_decoder_class->reset = GST_DEBUG_FUNCPTR (gst_mfc_dec_reset);
  video_decoder_class->set_format = GST_DEBUG_FUNCPTR (gst_mfc_dec_set_format);
  video_decoder_class->negotiate = GST_DEBUG_FUNCPTR (gst_mfc_dec_negotiate);
  video_decoder_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_mfc_dec_decide_allocation);
  video_decoder_class->handle_frame =
      GST_DEBUG_FUNCPTR (gst_mfc_dec_handle_frame);

  GST_DEBUG_CATEGORY_INIT (gst_mfc_dec_debug, "mfcdec", 0,
      "Samsung Exynos MFC Decoder");
}

static void
gst_mfc_dec_init (GstMFCDec * self)
{
  GstVideoDecoder *decoder = (GstVideoDecoder *) self;

  gst_video_decoder_set_packetized (decoder, TRUE);
}

static gboolean
gst_mfc_dec_start (GstVideoDecoder * decoder)
{
  GstMFCDec *self = GST_MFC_DEC (decoder);
  Fimc *fimc;

  GST_DEBUG_OBJECT (self, "Starting");

  self->width = 0;
  self->height = 0;
  self->crop_left = 0;
  self->crop_top = 0;
  self->crop_width = 0;
  self->crop_height = 0;

  /* Initialize with H264 here, we chose the correct codec in set_format */
  self->context = mfc_dec_create (CODEC_TYPE_H264, 1);
  if (!self->context) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("Failed to initialize MFC decoder context"), (NULL));
    return FALSE;
  }

  fimc = fimc_new ();

  if (!fimc) {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT,
        ("Failed to initialize FIMC context"), (NULL));
    return FALSE;
  }
  self->fimc = fimc;

  return TRUE;
}

static gboolean
gst_mfc_dec_stop (GstVideoDecoder * video_decoder)
{
  GstMFCDec *self = GST_MFC_DEC (video_decoder);

  GST_DEBUG_OBJECT (self, "Stopping");

  if (self->input_state) {
    gst_video_codec_state_unref (self->input_state);
    self->input_state = NULL;
  }

  if (self->context) {
    mfc_dec_destroy (self->context);
    self->context = NULL;
  }
  self->initialized = FALSE;

  if (self->fimc) {
    fimc_free (self->fimc);
    self->fimc = NULL;
  }

  GST_DEBUG_OBJECT (self, "Stopped");

  return TRUE;
}

static gboolean
gst_mfc_dec_set_format (GstVideoDecoder * decoder, GstVideoCodecState * state)
{
  GstMFCDec *self = GST_MFC_DEC (decoder);
  GstStructure *s;
  gint ret;

  GST_DEBUG_OBJECT (self, "Setting format: %" GST_PTR_FORMAT, state->caps);

  s = gst_caps_get_structure (state->caps, 0);

  if (gst_structure_has_name (s, "video/x-h264")) {
    if ((ret = mfc_dec_set_codec (self->context, CODEC_TYPE_H264)) < 0) {
      GST_ELEMENT_ERROR (self, LIBRARY, SETTINGS,
          ("Failed to set codec to H264"), (NULL));
      return FALSE;
    }
  } else {
    g_return_val_if_reached (FALSE);
  }

  if (self->input_state)
    gst_video_codec_state_unref (self->input_state);
  self->input_state = gst_video_codec_state_ref (state);

  return TRUE;
}

static gboolean
gst_mfc_dec_reset (GstVideoDecoder * decoder, gboolean hard)
{
  GstMFCDec *self = GST_MFC_DEC (decoder);

  GST_DEBUG_OBJECT (self, "Resetting");
  if (self->context)
    mfc_dec_flush (self->context);

  return TRUE;
}

static GstFlowReturn
gst_mfc_dec_queue_input (GstMFCDec * self, GstBuffer * inbuf)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gint mfc_ret;
  struct mfc_buffer *mfc_inbuf = NULL;
  guint8 *mfc_inbuf_data;
  gint mfc_inbuf_size;
  GstMapInfo map;

  GST_DEBUG_OBJECT (self, "Dequeueing input");

  if ((mfc_ret = mfc_dec_dequeue_input (self->context, &mfc_inbuf)) < 0) {
    if (mfc_ret == -2) {
      GST_DEBUG_OBJECT (self, "Timeout dequeueing input, trying again");
      mfc_ret = mfc_dec_dequeue_input (self->context, &mfc_inbuf);
    }

    if (mfc_ret < 0)
      goto dequeue_error;
  }

  g_assert (mfc_inbuf != NULL);

  if (inbuf) {
    gst_buffer_map (inbuf, &map, GST_MAP_READ);

    mfc_inbuf_data = (guint8 *) mfc_buffer_get_input_data (mfc_inbuf);
    g_assert (mfc_inbuf_data != NULL);
    mfc_inbuf_size = mfc_buffer_get_input_max_size (mfc_inbuf);

    GST_DEBUG_OBJECT (self, "Have input buffer %p with size %d", mfc_inbuf_data,
        mfc_inbuf_size);

    if ((gsize) mfc_inbuf_size < map.size)
      goto too_small_inbuf;

    memcpy (mfc_inbuf_data, map.data, map.size);
    mfc_buffer_set_input_size (mfc_inbuf, map.size);

    gst_buffer_unmap (inbuf, &map);
  } else {
    GST_DEBUG_OBJECT (self, "Passing EOS input buffer");

    mfc_buffer_set_input_size (mfc_inbuf, 0);
  }

  if ((mfc_ret = mfc_dec_enqueue_input (self->context, mfc_inbuf)) < 0)
    goto enqueue_error;

done:
  return ret;

dequeue_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
        ("Failed to dequeue input buffer"), ("mfc_dec_dequeue_input: %d",
            mfc_ret));
    ret = GST_FLOW_ERROR;
    goto done;
  }

too_small_inbuf:
  {
    GST_ELEMENT_ERROR (self, STREAM, FORMAT, ("Too large input frames"),
        ("Maximum size %d, got %d", mfc_inbuf_size, map.size));
    ret = GST_FLOW_ERROR;
    gst_buffer_unmap (inbuf, &map);
    goto done;
  }

enqueue_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
        ("Failed to enqueue input buffer"), ("mfc_dec_enqueue_input: %d",
            mfc_ret));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static GstVideoCodecFrame *
gst_mfc_dec_get_earliest_frame (GstMFCDec * self)
{
  GstVideoCodecFrame *frame = NULL;
  GList *frames, *l;

  frames = gst_video_decoder_get_frames (GST_VIDEO_DECODER (self));

  for (l = frames; l; l = l->next) {
    GstVideoCodecFrame *tmp = (GstVideoCodecFrame *) l->data;

    if (!frame) {
      frame = tmp;
    } else if (frame->pts > tmp->pts) {
      gst_video_codec_frame_unref (frame);
      frame = tmp;
    } else {
      gst_video_codec_frame_unref (tmp);
    }
  }

  g_list_free (frames);

  return frame;
}

static gboolean
gst_mfc_dec_negotiate (GstVideoDecoder * decoder)
{
  GstMFCDec *self = GST_MFC_DEC (decoder);
  Fimc *fimc = self->fimc;
  GstVideoCodecState *state;
  GstCaps *allowed_caps;
  GstVideoFormat format = GST_VIDEO_FORMAT_I420;
  FimcColorFormat fimc_format;

  allowed_caps = gst_pad_get_allowed_caps (GST_VIDEO_DECODER_SRC_PAD (self));
  allowed_caps = gst_caps_truncate (allowed_caps);
  allowed_caps = gst_caps_fixate (allowed_caps);
  if (!gst_caps_is_empty (allowed_caps)) {
    const gchar *format_str;
    GstStructure *s = gst_caps_get_structure (allowed_caps, 0);

    format_str = gst_structure_get_string (s, "format");
    if (format_str)
      format = gst_video_format_from_string (format_str);
  }
  gst_caps_unref (allowed_caps);

  switch (format) {
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
      fimc_format = FIMC_COLOR_FORMAT_YUV420P;
      break;
    case GST_VIDEO_FORMAT_NV12:
      fimc_format = FIMC_COLOR_FORMAT_YUV420SP;
      break;
    default:
      g_assert_not_reached ();
      break;
  }

  if (fimc_set_src_format (fimc, FIMC_COLOR_FORMAT_YUV420SPT, self->width,
          self->height, self->src_stride, self->crop_left, self->crop_top,
          self->crop_width, self->crop_height) < 0)
    goto fimc_src_error;

  if (fimc_set_dst_format_direct (fimc, fimc_format, self->width,
          self->height, self->crop_left, self->crop_top, self->crop_width,
          self->crop_height, self->dst, self->dst_stride) < 0)
    goto fimc_dst_error;

  GST_DEBUG_OBJECT (self,
      "Got direct output buffer: %p [%d], %p [%d], %p [%d]", self->dst[0],
      self->dst_stride[0], self->dst[1], self->dst_stride[1], self->dst[2],
      self->dst_stride[2]);

  state =
      gst_video_decoder_set_output_state (GST_VIDEO_DECODER (self),
      format, self->crop_width, self->crop_height, self->input_state);

  gst_video_codec_state_unref (state);

  return GST_VIDEO_DECODER_CLASS (parent_class)->negotiate (decoder);

fimc_src_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
        ("Failed to set FIMC source parameters"), (NULL));
    return FALSE;
  }

fimc_dst_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
        ("Failed to set FIMC destination parameters"), (NULL));
    return FALSE;
  }
}

static GstFlowReturn
gst_mfc_dec_dequeue_output (GstMFCDec * self)
{
  GstFlowReturn ret = GST_FLOW_OK;
  gint mfc_ret;
  GstVideoCodecFrame *frame = NULL;
  GstBuffer *outbuf = NULL;
  struct mfc_buffer *mfc_outbuf = NULL;
  gint width, height;
  gint crop_left, crop_top, crop_width, crop_height;
  gint src_ystride, src_uvstride;
  GstVideoCodecState *state = NULL;
  gint64 deadline;
  Fimc *fimc = NULL;
  GstVideoFrame vframe;

  if (!self->initialized) {
    GST_DEBUG_OBJECT (self, "Initializing decoder");
    if ((mfc_ret = mfc_dec_init (self->context, 1)) < 0)
      goto initialize_error;
    self->initialized = TRUE;
  }

  while ((mfc_ret = mfc_dec_output_available (self->context)) > 0) {
    GST_DEBUG_OBJECT (self, "Dequeueing output");

    mfc_dec_get_output_size (self->context, &width, &height);
    mfc_dec_get_output_stride (self->context, &src_ystride, &src_uvstride);
    mfc_dec_get_crop_size (self->context, &crop_left, &crop_top, &crop_width,
        &crop_height);

    GST_DEBUG_OBJECT (self, "Have output buffer: width %d, height %d, "
        "Y stride %d, UV stride %d, "
        "crop_left %d, crop_right %d, "
        "crop_width %d, crop_height %d", width, height, src_ystride,
        src_uvstride, crop_left, crop_top, crop_width, crop_height);

    state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (self));

    if (!state || self->width != width || self->height != height ||
        self->src_stride[0] != src_ystride
        || self->src_stride[1] != src_uvstride
        || self->crop_left != self->crop_left || self->crop_top != crop_top
        || self->crop_width != crop_width || self->crop_height != crop_height) {
      self->width = width;
      self->height = height;
      self->crop_left = crop_left;
      self->crop_top = crop_top;
      self->crop_width = crop_width;
      self->crop_height = crop_height;
      self->src_stride[0] = src_ystride;
      self->src_stride[1] = src_uvstride;
      self->src_stride[2] = 0;

      if (!gst_video_decoder_negotiate (GST_VIDEO_DECODER (self)))
        goto negotiate_error;

      if (state)
        gst_video_codec_state_unref (state);
      state = gst_video_decoder_get_output_state (GST_VIDEO_DECODER (self));
    }

    if ((mfc_ret = mfc_dec_dequeue_output (self->context, &mfc_outbuf)) < 0) {
      if (mfc_ret == -2) {
        GST_DEBUG_OBJECT (self, "Timeout dequeueing output, trying again");
        mfc_ret = mfc_dec_dequeue_output (self->context, &mfc_outbuf);
      }

      if (mfc_ret < 0)
        goto dequeue_error;
    }

    g_assert (mfc_outbuf != NULL);

    /* FIXME: Replace this by gst_video_decoder_get_frame() with an ID */
    frame = gst_mfc_dec_get_earliest_frame (self);

    if (frame) {
      deadline =
          gst_video_decoder_get_max_decode_time (GST_VIDEO_DECODER (self),
          frame);
      if (deadline < 0) {
        GST_LOG_OBJECT (self,
            "Dropping too late frame: deadline %" G_GINT64_FORMAT, deadline);
        ret = gst_video_decoder_drop_frame (GST_VIDEO_DECODER (self), frame);
        goto done;
      }

      if (!frame->output_buffer)
        ret =
            gst_video_decoder_allocate_output_frame (GST_VIDEO_DECODER (self),
            frame);

      if (ret != GST_FLOW_OK)
        goto alloc_error;

      outbuf = frame->output_buffer;
    } else {
      outbuf =
          gst_video_decoder_allocate_output_buffer (GST_VIDEO_DECODER (self));

      if (!outbuf) {
        ret = GST_FLOW_ERROR;
        goto alloc_error;
      }
    }

    {
      const guint8 *mfc_outbuf_comps[3] = { NULL, };
      gint i, j, h, w, src_stride, dst_stride;
      guint8 *dst_, *src_;

      fimc = self->fimc;

      mfc_buffer_get_output_data (mfc_outbuf, (void **) &mfc_outbuf_comps[0],
          (void **) &mfc_outbuf_comps[1]);

      if (fimc_convert_direct (fimc, (void **) mfc_outbuf_comps) < 0)
        goto fimc_convert_error;

      if (!gst_video_frame_map (&vframe, &state->info, outbuf, GST_MAP_WRITE))
        goto frame_map_error;

      switch (state->info.finfo->format) {
        case GST_VIDEO_FORMAT_I420:
        case GST_VIDEO_FORMAT_YV12:
          for (j = 0; j < 3; j++) {
            dst_ = (guint8 *) GST_VIDEO_FRAME_COMP_DATA (&vframe, j);
            src_ = self->dst[j];
            src_stride = self->dst_stride[j];
            h = GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, j);
            w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, j);
            dst_stride = GST_VIDEO_FRAME_COMP_STRIDE (&vframe, j);
            for (i = 0; i < h; i++) {
              memcpy (dst_, src_, w);
              dst_ += dst_stride;
              src_ += src_stride;
            }
          }
          break;
        case GST_VIDEO_FORMAT_NV12:
          for (j = 0; j < 2; j++) {
            dst_ = (guint8 *) GST_VIDEO_FRAME_PLANE_DATA (&vframe, j);
            src_ = self->dst[j];
            src_stride = self->dst_stride[j];
            h = GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, j);
            w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, j) * (j == 0 ? 1 : 2);
            dst_stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, j);
            for (i = 0; i < h; i++) {
              memcpy (dst_, src_, w);
              dst_ += dst_stride;
              src_ += src_stride;
            }
          }
          break;
        default:
          g_assert_not_reached ();
          break;
      }

      gst_video_frame_unmap (&vframe);
    }

    if (frame) {
      ret = gst_video_decoder_finish_frame (GST_VIDEO_DECODER (self), frame);
      frame = NULL;
      outbuf = NULL;
    } else {
      ret = gst_pad_push (GST_VIDEO_DECODER_SRC_PAD (self), outbuf);
      outbuf = NULL;
    }

    if (ret != GST_FLOW_OK)
      GST_INFO_OBJECT (self, "Pushing frame returned: %s",
          gst_flow_get_name (ret));

  done:
    if (mfc_outbuf) {
      if ((mfc_ret = mfc_dec_enqueue_output (self->context, mfc_outbuf)) < 0)
        goto enqueue_error;
    }

    if (!frame && outbuf)
      gst_buffer_unref (outbuf);
    if (state)
      gst_video_codec_state_unref (state);
    if (frame)
      gst_video_codec_frame_unref (frame);

    if (ret != GST_FLOW_OK)
      break;
  }

  return ret;

initialize_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, INIT, ("Failed to initialize output"),
        ("mfc_dec_init: %d", mfc_ret));
    ret = GST_FLOW_ERROR;
    goto done;
  }

negotiate_error:
  {
    GST_ELEMENT_ERROR (self, CORE, NEGOTIATION, ("Failed to negotiate"),
        (NULL));
    ret = GST_FLOW_NOT_NEGOTIATED;
    goto done;
  }

dequeue_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
        ("Failed to dequeue output buffer"), ("mfc_dec_dequeue_output: %d",
            mfc_ret));
    ret = GST_FLOW_ERROR;
    goto done;
  }

alloc_error:
  {
    GST_ELEMENT_ERROR (self, CORE, FAILED, ("Failed to allocate output buffer"),
        (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }

frame_map_error:
  {
    GST_ELEMENT_ERROR (self, CORE, FAILED, ("Failed to map output buffer"),
        (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }

fimc_convert_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
        ("Failed to convert via FIMC"), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }

enqueue_error:
  {
    GST_ELEMENT_ERROR (self, LIBRARY, FAILED,
        ("Failed to enqueue output buffer"), ("mfc_dec_enqueue_output: %d",
            mfc_ret));
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static GstFlowReturn
gst_mfc_dec_finish (GstVideoDecoder * decoder)
{
  GstMFCDec *self = GST_MFC_DEC (decoder);
  GstFlowReturn ret;

  GST_DEBUG_OBJECT (self, "Finishing decoding");

  if ((ret = gst_mfc_dec_queue_input (self, NULL)) != GST_FLOW_OK)
    return ret;

  return gst_mfc_dec_dequeue_output (self);
}

static GstFlowReturn
gst_mfc_dec_handle_frame (GstVideoDecoder * decoder, GstVideoCodecFrame * frame)
{
  GstMFCDec *self = GST_MFC_DEC (decoder);
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG_OBJECT (self, "Handling frame");

  /* FIXME: Would be good to assign an ID to input frames */
  if (frame->pts == GST_CLOCK_TIME_NONE) {
    GST_ERROR_OBJECT (self, "Only PTS timestamped streams supported so far");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }

  if ((ret =
          gst_mfc_dec_queue_input (self, frame->input_buffer)) != GST_FLOW_OK) {
    gst_video_codec_frame_unref (frame);
    return ret;
  }

  gst_video_codec_frame_unref (frame);

  return gst_mfc_dec_dequeue_output (self);
}

static gboolean
gst_mfc_dec_decide_allocation (GstVideoDecoder * decoder, GstQuery * query)
{
  GstBufferPool *pool;
  GstStructure *config;

  if (!GST_VIDEO_DECODER_CLASS (parent_class)->decide_allocation (decoder,
          query))
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
