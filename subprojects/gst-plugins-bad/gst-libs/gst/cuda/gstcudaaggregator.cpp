/* GStreamer
 * Copyright (C) 2026 Seungha Yang <seungha@centricular.com>
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
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/cuda/gstcuda.h>
#include <gst/cuda/gstcuda-private.h>
#include <mutex>
#include <set>

GST_DEBUG_CATEGORY_STATIC (gst_cuda_aggregator_debug);
#define GST_CAT_DEFAULT gst_cuda_aggregator_debug

enum
{
  PROP_0,
  PROP_DEVICE_ID,
};

#define DEFAULT_DEVICE_ID -1

/* *INDENT-OFF* */
struct _GstCudaAggregatorPadPrivate
{
  ~_GstCudaAggregatorPadPrivate ()
  {
    gst_clear_buffer (&prepared_buf);
    if (fallback_pool) {
      gst_buffer_pool_set_active (fallback_pool, FALSE);
      gst_object_unref (fallback_pool);
    }
  }

  GstBufferPool *fallback_pool = nullptr;
  GstBuffer *prepared_buf = nullptr;
  GstVideoInfo pool_info;
};

struct _GstCudaAggregatorConvertPadPrivate
{
  ~_GstCudaAggregatorConvertPadPrivate ()
  {
    release_resources ();
  }

  void release_resources ()
  {
    gst_clear_buffer (&converted_buf);
    if (conv_pool) {
      gst_buffer_pool_set_active (conv_pool, FALSE);
      gst_object_unref (conv_pool);
    }
    gst_clear_object (&conv);
  }

  GstCudaConverter *conv = nullptr;
  GstBufferPool *conv_pool = nullptr;
  GstBuffer *converted_buf = nullptr;
  GstVideoInfo conversion_info;
  gboolean converter_config_changed = FALSE;

  std::recursive_mutex lock;
};

struct _GstCudaAggregatorPrivate
{
  ~_GstCudaAggregatorPrivate ()
  {
    gst_clear_cuda_stream (&stream);
    gst_clear_cuda_stream (&other_stream);
  }

  std::recursive_mutex lock;

  GstCudaStream *stream = nullptr;
  GstCudaStream *other_stream = nullptr;
  std::set<GstCudaStream *> sync_done_list;

  /* properties */
  gint device_id = DEFAULT_DEVICE_ID;
};
/* *INDENT-ON* */

static void gst_cuda_aggregator_pad_finalize (GObject * object);
static gboolean
gst_cuda_aggregator_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame);
static void gst_cuda_aggregator_pad_clean_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstVideoFrame * prepared_frame);

#define gst_cuda_aggregator_pad_parent_class parent_pad_class
G_DEFINE_TYPE (GstCudaAggregatorPad, gst_cuda_aggregator_pad,
    GST_TYPE_VIDEO_AGGREGATOR_PAD);

static void
gst_cuda_aggregator_pad_class_init (GstCudaAggregatorPadClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto vagg_pad_class = GST_VIDEO_AGGREGATOR_PAD_CLASS (klass);

  object_class->finalize = gst_cuda_aggregator_pad_finalize;

  vagg_pad_class->prepare_frame =
      GST_DEBUG_FUNCPTR (gst_cuda_aggregator_pad_prepare_frame);
  vagg_pad_class->clean_frame =
      GST_DEBUG_FUNCPTR (gst_cuda_aggregator_pad_clean_frame);
}

static void
gst_cuda_aggregator_pad_init (GstCudaAggregatorPad * self)
{
  self->priv = new GstCudaAggregatorPadPrivate ();
}

static void
gst_cuda_aggregator_pad_finalize (GObject * object)
{
  auto self = GST_CUDA_AGGREGATOR_PAD (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_pad_class)->finalize (object);
}

static GstBuffer *
gst_cuda_aggregator_upload_frame (GstCudaAggregator * self,
    GstVideoAggregatorPad * pad, GstBuffer * buffer)
{
  auto cpad = GST_CUDA_AGGREGATOR_PAD (pad);
  auto priv = cpad->priv;
  GstVideoFrame src, dst;

  auto mem = gst_buffer_peek_memory (buffer, 0);
  if (gst_is_cuda_memory (mem)) {
    auto cmem = GST_CUDA_MEMORY_CAST (mem);
    if (cmem->context == self->context)
      return gst_buffer_ref (buffer);
  }

  if (!gst_video_frame_map (&src, &pad->info, buffer, GST_MAP_READ)) {
    GST_ERROR_OBJECT (pad, "Couldn't map src frame");
    return nullptr;
  }

  auto frame_width = GST_VIDEO_FRAME_WIDTH (&src);
  auto frame_height = GST_VIDEO_FRAME_HEIGHT (&src);

  if (priv->fallback_pool &&
      (priv->pool_info.width != frame_width ||
          priv->pool_info.height != frame_height)) {
    /* Size can be different if crop meta is in use */
    GST_DEBUG_OBJECT (pad,
        "Fallback pool size mismatch, releasing old fallback pool");
    gst_buffer_pool_set_active (priv->fallback_pool, FALSE);
    gst_clear_object (&priv->fallback_pool);
  }

  if (!priv->fallback_pool) {
    priv->fallback_pool = gst_cuda_buffer_pool_new (self->context);
    auto config = gst_buffer_pool_get_config (priv->fallback_pool);
    auto stream = self->priv->other_stream;
    if (!stream)
      stream = self->priv->stream;

    gst_buffer_pool_config_set_cuda_stream (config, stream);

    gst_video_info_set_format (&priv->pool_info,
        GST_VIDEO_INFO_FORMAT (&pad->info), frame_width, frame_height);

    auto caps = gst_video_info_to_caps (&priv->pool_info);
    gst_buffer_pool_config_set_params (config,
        caps, priv->pool_info.size, 0, 0);
    gst_caps_unref (caps);
    if (!gst_buffer_pool_set_config (priv->fallback_pool, config)) {
      GST_ERROR_OBJECT (pad, "Set config failed");
      gst_clear_object (&priv->fallback_pool);
      return nullptr;
    }

    if (!gst_buffer_pool_set_active (priv->fallback_pool, TRUE)) {
      GST_ERROR_OBJECT (pad, "Set active failed");
      gst_clear_object (&priv->fallback_pool);
      return nullptr;
    }
  }

  GstBuffer *outbuf = nullptr;
  gst_buffer_pool_acquire_buffer (priv->fallback_pool, &outbuf, nullptr);
  if (!outbuf) {
    GST_ERROR_OBJECT (self, "Couldn't acquire buffer");
    gst_video_frame_unmap (&src);
    return nullptr;
  }

  if (!gst_video_frame_map (&dst, &pad->info, outbuf, GST_MAP_WRITE)) {
    GST_ERROR_OBJECT (pad, "Couldn't map dst frame");
    gst_video_frame_unmap (&src);
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  auto ret = gst_video_frame_copy (&dst, &src);
  gst_video_frame_unmap (&dst);
  gst_video_frame_unmap (&src);

  if (!ret) {
    GST_ERROR_OBJECT (pad, "Couldn't copy frame");
    gst_buffer_unref (outbuf);
    return nullptr;
  }

  gst_buffer_copy_into (outbuf, buffer, GST_BUFFER_COPY_METADATA, 0, -1);

  return outbuf;
}

static gboolean
gst_cuda_aggregator_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame)
{
  auto self = GST_CUDA_AGGREGATOR_PAD (pad);
  auto priv = self->priv;

  buffer = gst_cuda_aggregator_upload_frame (GST_CUDA_AGGREGATOR (vagg),
      pad, buffer);
  if (!buffer)
    return FALSE;

  if (!gst_video_frame_map (prepared_frame,
          &pad->info, buffer, GST_MAP_READ_CUDA)) {
    GST_ERROR_OBJECT (self, "Couldn't map frame");
    gst_buffer_unref (buffer);
    return FALSE;
  }

  priv->prepared_buf = buffer;

  return TRUE;
}

static void
gst_cuda_aggregator_pad_clean_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstVideoFrame * prepared_frame)
{
  auto self = GST_CUDA_AGGREGATOR_PAD (pad);
  auto priv = self->priv;

  if (prepared_frame->buffer)
    gst_video_frame_unmap (prepared_frame);

  memset (prepared_frame, 0, sizeof (GstVideoFrame));
  gst_clear_buffer (&priv->prepared_buf);
}

static void gst_cuda_aggregator_convert_pad_finalize (GObject * object);
static gboolean
gst_cuda_aggregator_convert_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame);
static void
gst_cuda_aggregator_convert_pad_clean_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstVideoFrame * prepared_frame);
static void gst_cuda_aggregator_convert_pad_update_conversion_info
    (GstVideoAggregatorPad * pad);
static void gst_cuda_aggregator_convert_pad_create_conversion_info
    (GstCudaAggregatorConvertPad * pad, GstCudaAggregator * agg,
    GstVideoInfo * convert_info);

#define gst_cuda_aggregator_convert_pad_parent_class parent_convert_pad_class
G_DEFINE_TYPE (GstCudaAggregatorConvertPad, gst_cuda_aggregator_convert_pad,
    GST_TYPE_CUDA_AGGREGATOR_PAD);

static void
gst_cuda_aggregator_convert_pad_class_init (GstCudaAggregatorConvertPadClass *
    klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto vagg_pad_class = GST_VIDEO_AGGREGATOR_PAD_CLASS (klass);

  object_class->finalize = gst_cuda_aggregator_convert_pad_finalize;

  vagg_pad_class->prepare_frame =
      GST_DEBUG_FUNCPTR (gst_cuda_aggregator_convert_pad_prepare_frame);
  vagg_pad_class->clean_frame =
      GST_DEBUG_FUNCPTR (gst_cuda_aggregator_convert_pad_clean_frame);
  vagg_pad_class->update_conversion_info =
      GST_DEBUG_FUNCPTR
      (gst_cuda_aggregator_convert_pad_update_conversion_info);

  klass->create_conversion_info =
      GST_DEBUG_FUNCPTR
      (gst_cuda_aggregator_convert_pad_create_conversion_info);
}

static void
gst_cuda_aggregator_convert_pad_init (GstCudaAggregatorConvertPad * self)
{
  self->priv = new GstCudaAggregatorConvertPadPrivate ();
}

static void
gst_cuda_aggregator_convert_pad_finalize (GObject * object)
{
  auto self = GST_CUDA_AGGREGATOR_CONVERT_PAD (object);

  delete self->priv;

  G_OBJECT_CLASS (parent_convert_pad_class)->finalize (object);
}

static gboolean
gst_cuda_aggregator_convert_pad_prepare_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstBuffer * buffer,
    GstVideoFrame * prepared_frame)
{
  auto self = GST_CUDA_AGGREGATOR_CONVERT_PAD (pad);
  auto priv = self->priv;
  auto cagg = GST_CUDA_AGGREGATOR (vagg);

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (priv->converter_config_changed) {
      auto klass = GST_CUDA_AGGREGATOR_CONVERT_PAD_GET_CLASS (pad);
      GstVideoInfo conversion_info;

      gst_video_info_init (&conversion_info);
      klass->create_conversion_info (self, cagg, &conversion_info);
      if (!conversion_info.finfo) {
        return FALSE;
      }

      priv->conversion_info = conversion_info;
      priv->release_resources ();

      if (!gst_video_info_is_equal (&pad->info, &priv->conversion_info)) {
        priv->conv = gst_cuda_converter_new (cagg->context, &pad->info,
            &priv->conversion_info, nullptr);

        if (!priv->conv) {
          GST_WARNING_OBJECT (pad, "No path found for conversion");
          return FALSE;
        }

        priv->conv_pool = gst_cuda_buffer_pool_new (cagg->context);
        auto config = gst_buffer_pool_get_config (priv->conv_pool);
        auto stream = cagg->priv->other_stream;
        if (!stream)
          stream = cagg->priv->stream;

        gst_buffer_pool_config_set_cuda_stream (config, stream);

        auto caps = gst_video_info_to_caps (&priv->conversion_info);
        gst_buffer_pool_config_set_params (config,
            caps, priv->conversion_info.size, 0, 0);
        gst_caps_unref (caps);
        if (!gst_buffer_pool_set_config (priv->conv_pool, config)) {
          GST_ERROR_OBJECT (pad, "Set config failed");
          priv->release_resources ();
          return FALSE;
        }

        if (!gst_buffer_pool_set_active (priv->conv_pool, TRUE)) {
          GST_ERROR_OBJECT (pad, "Set active failed");
          priv->release_resources ();
          return FALSE;
        }

        GST_DEBUG_OBJECT (pad, "This pad will be converted from %s to %s",
            gst_video_format_to_string (GST_VIDEO_INFO_FORMAT (&pad->info)),
            gst_video_format_to_string (GST_VIDEO_INFO_FORMAT
                (&priv->conversion_info)));
      } else {
        GST_DEBUG_OBJECT (pad, "This pad will not need conversion");
      }

      priv->converter_config_changed = FALSE;
    }
  }

  /* Performs uploading first */
  if (!gst_cuda_aggregator_pad_prepare_frame (pad,
          vagg, buffer, prepared_frame)) {
    return FALSE;
  }

  if (!prepared_frame->buffer)
    return TRUE;

  if (!priv->conv)
    return TRUE;

  GstBuffer *conv_buf = nullptr;
  gst_buffer_pool_acquire_buffer (priv->conv_pool, &conv_buf, nullptr);
  if (!conv_buf) {
    GST_ERROR_OBJECT (pad, "Couldn't acquire convert dest buffer");
    return FALSE;
  }

  GstVideoFrame conv_frame;
  if (!gst_video_frame_map (&conv_frame,
          &priv->conversion_info, conv_buf, GST_MAP_WRITE_CUDA)) {
    GST_ERROR_OBJECT (pad, "Couldn't map dst frame");
    gst_buffer_unref (conv_buf);
    return FALSE;
  }

  auto in_cmem =
      (GstCudaMemory *) gst_buffer_peek_memory (prepared_frame->buffer, 0);

  /* Stream preference,
   * 1) associated with input buffer
   * 2) downstream proposed one
   * 3) our own stream */
  auto in_stream = gst_cuda_memory_get_stream (in_cmem);
  GstCudaStream *stream = in_stream;
  if (!stream)
    stream = cagg->priv->other_stream;
  if (!stream)
    stream = cagg->priv->stream;

  auto conv_ret = gst_cuda_converter_convert_frame (priv->conv, stream,
      prepared_frame, &conv_frame);
  gst_video_frame_unmap (&conv_frame);

  if (!conv_ret) {
    GST_ERROR_OBJECT (pad, "Couldn't convert frame");
    gst_buffer_unref (conv_buf);
    return FALSE;
  }

  /* Unmap input buffer and remap converted one */
  gst_cuda_aggregator_pad_clean_frame (pad, vagg, prepared_frame);
  if (!gst_video_frame_map (prepared_frame,
          &priv->conversion_info, conv_buf, GST_MAP_READ_CUDA)) {
    GST_ERROR_OBJECT (self, "Couldn't map frame");
    gst_buffer_unref (conv_buf);
    return FALSE;
  }

  priv->converted_buf = conv_buf;

  return TRUE;
}

static void
gst_cuda_aggregator_convert_pad_clean_frame (GstVideoAggregatorPad * pad,
    GstVideoAggregator * vagg, GstVideoFrame * prepared_frame)
{
  auto self = GST_CUDA_AGGREGATOR_CONVERT_PAD (pad);
  auto priv = self->priv;

  gst_cuda_aggregator_pad_clean_frame (pad, vagg, prepared_frame);
  gst_clear_buffer (&priv->converted_buf);
}

static void
gst_cuda_aggregator_convert_pad_update_conversion_info (GstVideoAggregatorPad *
    pad)
{
  auto self = GST_CUDA_AGGREGATOR_CONVERT_PAD (pad);
  auto priv = self->priv;

  GST_DEBUG_OBJECT (pad, "Need conversion info update");

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  priv->converter_config_changed = TRUE;
}

/* Same as gstvideoaggregator.c */
static void
    gst_cuda_aggregator_convert_pad_create_conversion_info
    (GstCudaAggregatorConvertPad * pad, GstCudaAggregator * agg,
    GstVideoInfo * convert_info)
{
  auto vpad = GST_VIDEO_AGGREGATOR_PAD (pad);
  auto vagg = GST_VIDEO_AGGREGATOR (agg);
  gchar *colorimetry, *best_colorimetry;
  gchar *chroma, *best_chroma;

  g_return_if_fail (GST_IS_CUDA_AGGREGATOR_CONVERT_PAD (pad));
  g_return_if_fail (convert_info);

  if (!vpad->info.finfo
      || GST_VIDEO_INFO_FORMAT (&vpad->info) == GST_VIDEO_FORMAT_UNKNOWN) {
    return;
  }

  if (!vagg->info.finfo
      || GST_VIDEO_INFO_FORMAT (&vagg->info) == GST_VIDEO_FORMAT_UNKNOWN) {
    return;
  }

  colorimetry = gst_video_colorimetry_to_string (&vpad->info.colorimetry);
  chroma = gst_video_chroma_site_to_string (vpad->info.chroma_site);

  best_colorimetry = gst_video_colorimetry_to_string (&vagg->info.colorimetry);
  best_chroma = gst_video_chroma_site_to_string (vagg->info.chroma_site);

  if (GST_VIDEO_INFO_FORMAT (&vagg->info) != GST_VIDEO_INFO_FORMAT (&vpad->info)
      || g_strcmp0 (colorimetry, best_colorimetry)
      || g_strcmp0 (chroma, best_chroma)) {
    GstVideoInfo tmp_info;

    /* Initialize with the wanted video format and our original width and
     * height as we don't want to rescale. Then copy over the wanted
     * colorimetry, and chroma-site and our current pixel-aspect-ratio
     * and other relevant fields.
     */
    gst_video_info_set_format (&tmp_info, GST_VIDEO_INFO_FORMAT (&vagg->info),
        vpad->info.width, vpad->info.height);
    tmp_info.chroma_site = vagg->info.chroma_site;
    tmp_info.colorimetry = vagg->info.colorimetry;
    tmp_info.par_n = vpad->info.par_n;
    tmp_info.par_d = vpad->info.par_d;
    tmp_info.fps_n = vpad->info.fps_n;
    tmp_info.fps_d = vpad->info.fps_d;
    tmp_info.flags = vpad->info.flags;
    tmp_info.interlace_mode = vpad->info.interlace_mode;

    *convert_info = tmp_info;
  } else {
    *convert_info = vpad->info;
  }

  g_free (colorimetry);
  g_free (best_colorimetry);
  g_free (chroma);
  g_free (best_chroma);
}

static void gst_cuda_aggregator_finalize (GObject * object);
static void gst_cuda_aggregator_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_cuda_aggregator_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_cuda_aggregator_set_context (GstElement * element,
    GstContext * context);

static gboolean gst_cuda_aggregator_start (GstAggregator * agg);
static gboolean gst_cuda_aggregator_stop (GstAggregator * agg);
static gboolean gst_cuda_aggregator_sink_query (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * query);
static gboolean gst_cuda_aggregator_src_query (GstAggregator * agg,
    GstQuery * query);
static gboolean
gst_cuda_aggregator_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query);
static gboolean gst_cuda_aggregator_decide_allocation (GstAggregator * agg,
    GstQuery * query);
static GstFlowReturn
gst_cuda_aggregator_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf);

#define gst_cuda_aggregator_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstCudaAggregator, gst_cuda_aggregator,
    GST_TYPE_VIDEO_AGGREGATOR);

static void
gst_cuda_aggregator_class_init (GstCudaAggregatorClass * klass)
{
  auto object_class = G_OBJECT_CLASS (klass);
  auto element_class = GST_ELEMENT_CLASS (klass);
  auto agg_class = GST_AGGREGATOR_CLASS (klass);
  auto vagg_class = GST_VIDEO_AGGREGATOR_CLASS (klass);

  object_class->finalize = gst_cuda_aggregator_finalize;
  object_class->set_property = gst_cuda_aggregator_set_property;
  object_class->get_property = gst_cuda_aggregator_get_property;

  g_object_class_install_property (object_class, PROP_DEVICE_ID,
      g_param_spec_int ("cuda-device-id", "Cuda Device ID",
          "Set the GPU device to use for operations (-1 = auto)",
          -1, G_MAXINT, DEFAULT_DEVICE_ID,
          (GParamFlags) (G_PARAM_READWRITE | GST_PARAM_MUTABLE_READY |
              G_PARAM_STATIC_STRINGS)));

  element_class->set_context =
      GST_DEBUG_FUNCPTR (gst_cuda_aggregator_set_context);

  agg_class->start = GST_DEBUG_FUNCPTR (gst_cuda_aggregator_start);
  agg_class->stop = GST_DEBUG_FUNCPTR (gst_cuda_aggregator_stop);
  agg_class->sink_query = GST_DEBUG_FUNCPTR (gst_cuda_aggregator_sink_query);
  agg_class->src_query = GST_DEBUG_FUNCPTR (gst_cuda_aggregator_src_query);
  agg_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_aggregator_propose_allocation);
  agg_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_cuda_aggregator_decide_allocation);

  vagg_class->aggregate_frames =
      GST_DEBUG_FUNCPTR (gst_cuda_aggregator_aggregate_frames);

  GST_DEBUG_CATEGORY_INIT (gst_cuda_aggregator_debug,
      "cudaggregator", 0, "cudaggregator");
}

static void
gst_cuda_aggregator_init (GstCudaAggregator * self)
{
  self->priv = new GstCudaAggregatorPrivate ();
}

static void
gst_cuda_aggregator_finalize (GObject * object)
{
  auto self = GST_CUDA_AGGREGATOR (object);

  delete self->priv;

  gst_clear_object (&self->context);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_cuda_aggregator_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  auto self = GST_CUDA_AGGREGATOR (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_DEVICE_ID:
      priv->device_id = g_value_get_int (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_aggregator_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  auto self = GST_CUDA_AGGREGATOR (object);
  auto priv = self->priv;

  std::lock_guard < std::recursive_mutex > lk (priv->lock);
  switch (prop_id) {
    case PROP_DEVICE_ID:
      g_value_set_int (value, priv->device_id);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_cuda_aggregator_set_context (GstElement * element, GstContext * context)
{
  auto self = GST_CUDA_AGGREGATOR (element);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_cuda_handle_set_context (element, context, priv->device_id,
        &self->context);
  }

  GST_ELEMENT_CLASS (parent_class)->set_context (element, context);
}

static gboolean
gst_cuda_aggregator_start (GstAggregator * agg)
{
  auto self = GST_CUDA_AGGREGATOR (agg);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    if (!gst_cuda_ensure_element_context (GST_ELEMENT_CAST (self),
            priv->device_id, &self->context)) {
      GST_ERROR_OBJECT (self, "Failed to get context");
      return FALSE;
    }
  }

  priv->stream = gst_cuda_stream_new (self->context);
  if (!priv->stream) {
    GST_ERROR_OBJECT (self, "Couldn't create stream");
    gst_clear_object (&self->context);
    return FALSE;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->start (agg);
}

static gboolean
gst_cuda_aggregator_stop (GstAggregator * agg)
{
  auto self = GST_CUDA_AGGREGATOR (agg);
  auto priv = self->priv;

  {
    std::lock_guard < std::recursive_mutex > lk (priv->lock);
    gst_clear_cuda_stream (&priv->other_stream);
    gst_clear_cuda_stream (&priv->stream);
    gst_clear_object (&self->context);
  }

  return GST_AGGREGATOR_CLASS (parent_class)->stop (agg);
}

static gboolean
gst_cuda_aggregator_sink_query (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * query)
{
  auto self = GST_CUDA_AGGREGATOR (agg);
  auto priv = self->priv;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      std::lock_guard < std::recursive_mutex > lk (priv->lock);
      if (gst_cuda_handle_context_query (GST_ELEMENT (agg), query,
              self->context)) {
        return TRUE;
      }
      break;
    }
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->sink_query (agg, pad, query);
}

static gboolean
gst_cuda_aggregator_src_query (GstAggregator * agg, GstQuery * query)
{
  auto self = GST_CUDA_AGGREGATOR (agg);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      if (gst_cuda_handle_context_query (GST_ELEMENT (agg), query,
              self->context)) {
        return TRUE;
      }
      break;
    default:
      break;
  }

  return GST_AGGREGATOR_CLASS (parent_class)->src_query (agg, query);
}

static gboolean
gst_cuda_aggregator_propose_allocation (GstAggregator * agg,
    GstAggregatorPad * pad, GstQuery * decide_query, GstQuery * query)
{
  auto self = GST_CUDA_AGGREGATOR (agg);
  auto priv = self->priv;
  GstVideoInfo info;
  GstCaps *caps;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (gst_query_get_n_allocation_pools (query) == 0) {
    auto use_cuda = FALSE;
    auto features = gst_caps_get_features (caps, 0);
    if (gst_caps_features_contains (features,
            GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
      use_cuda = TRUE;
    }

    GstBufferPool *pool = nullptr;
    if (use_cuda)
      pool = gst_cuda_buffer_pool_new (self->context);
    else
      pool = gst_video_buffer_pool_new ();

    if (!pool) {
      GST_ERROR_OBJECT (self, "Failed to create buffer pool");
      return FALSE;
    }

    auto config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_VIDEO_META);

    if (use_cuda) {
      if (priv->other_stream)
        gst_buffer_pool_config_set_cuda_stream (config, priv->other_stream);
      else
        gst_buffer_pool_config_set_cuda_stream (config, priv->stream);
    }

    guint size = GST_VIDEO_INFO_SIZE (&info);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

    if (!gst_buffer_pool_set_config (pool, config)) {
      GST_ERROR_OBJECT (pool, "Couldn't set config");
      gst_object_unref (pool);

      return FALSE;
    }

    if (use_cuda) {
      /* bufferpool will recalculate size, update it here */
      config = gst_buffer_pool_get_config (pool);
      gst_buffer_pool_config_get_params (config,
          nullptr, &size, nullptr, nullptr);
      gst_structure_free (config);
    }

    gst_query_add_allocation_pool (query, pool, size, 0, 0);
    gst_object_unref (pool);
  }

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, nullptr);

  return TRUE;
}

static gboolean
gst_cuda_aggregator_decide_allocation (GstAggregator * agg, GstQuery * query)
{
  auto self = GST_CUDA_AGGREGATOR (agg);
  auto priv = self->priv;
  GstCaps *caps;
  GstBufferPool *pool = nullptr;
  guint n, size, min, max;
  GstVideoInfo info;

  gst_query_parse_allocation (query, &caps, nullptr);

  if (!caps) {
    GST_DEBUG_OBJECT (self, "No output caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (self, "Invalid caps");
    return FALSE;
  }

  auto use_cuda = FALSE;
  auto features = gst_caps_get_features (caps, 0);
  if (gst_caps_features_contains (features,
          GST_CAPS_FEATURE_MEMORY_CUDA_MEMORY)) {
    use_cuda = TRUE;
  }

  n = gst_query_get_n_allocation_pools (query);
  if (n > 0)
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

  /* create our own pool */
  if (pool) {
    if (use_cuda) {
      if (!GST_IS_CUDA_BUFFER_POOL (pool)) {
        GST_DEBUG_OBJECT (self,
            "Downstream pool is not cuda, will create new one");
        gst_clear_object (&pool);
      } else {
        auto cpool = GST_CUDA_BUFFER_POOL (pool);
        if (cpool->context != self->context) {
          GST_DEBUG_OBJECT (self, "Different context, will create new one");
          gst_clear_object (&pool);
        }
      }
    }
  }

  size = (guint) info.size;

  if (!pool) {
    if (use_cuda)
      pool = gst_cuda_buffer_pool_new (self->context);
    else
      pool = gst_video_buffer_pool_new ();
    min = 0;
    max = 0;
  }

  auto config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_clear_cuda_stream (&priv->other_stream);
  if (use_cuda) {
    priv->other_stream = gst_buffer_pool_config_get_cuda_stream (config);
    if (priv->other_stream) {
      GST_DEBUG_OBJECT (self, "Downstream provided CUDA stream");
    } else {
      GST_DEBUG_OBJECT (self, "Set our stream to decided buffer pool");
      gst_buffer_pool_config_set_cuda_stream (config, priv->stream);
    }
  }

  if (!gst_buffer_pool_set_config (pool, config)) {
    GST_ERROR_OBJECT (self, "Set config failed");
    gst_object_unref (pool);
    return FALSE;
  }

  if (use_cuda) {
    /* bufferpool will recalculate size, gets updated size here */
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, nullptr, &size, nullptr,
        nullptr);
    gst_structure_free (config);
  }

  if (n > 0)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_object_unref (pool);

  return TRUE;
}

static GstFlowReturn
gst_cuda_aggregator_aggregate_frames (GstVideoAggregator * vagg,
    GstBuffer * outbuf)
{
  auto self = GST_CUDA_AGGREGATOR (vagg);
  auto priv = self->priv;
  auto klass = GST_CUDA_AGGREGATOR_GET_CLASS (self);

  GST_LOG_OBJECT (self, "aggregate");

  auto stream = priv->other_stream;
  if (!stream)
    stream = priv->stream;

  /* Waits for sync if there's any commands queued in different stream */
  if (stream) {
    GList *iter;
    priv->sync_done_list.clear ();

    GST_OBJECT_LOCK (self);
    for (iter = GST_ELEMENT (vagg)->sinkpads; iter; iter = g_list_next (iter)) {
      auto pad = GST_VIDEO_AGGREGATOR_PAD (iter->data);
      auto in_frame = gst_video_aggregator_pad_get_prepared_frame (pad);

      if (!in_frame)
        continue;

      auto in_cmem = (GstCudaMemory *)
          gst_buffer_peek_memory (in_frame->buffer, 0);
      auto in_stream = gst_cuda_memory_get_stream (in_cmem);
      if (in_stream != stream) {
        if (priv->sync_done_list.find (in_stream) !=
            priv->sync_done_list.end ()) {
          gst_cuda_memory_sync (in_cmem);
          priv->sync_done_list.insert (in_stream);
        }
      }
    }
    GST_OBJECT_UNLOCK (self);
  }

  if (!gst_cuda_context_push (self->context)) {
    GST_ERROR_OBJECT (self, "Couldn't push context");
    return GST_FLOW_ERROR;
  }

  auto ret = klass->aggregate_cuda_frames (self, stream, outbuf);

  gst_cuda_context_pop (nullptr);

  return ret;
}
