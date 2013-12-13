/*
 *  gstvaapiencode.c - VA-API video encoder
 *
 *  Copyright (C) 2013 Intel Corporation
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "gst/vaapi/sysdeps.h"
#include <gst/vaapi/gstvaapivalue.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include "gstvaapiencode.h"
#include "gstvaapivideocontext.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideometa.h"
#if GST_CHECK_VERSION(1,0,0)
#include "gstvaapivideomemory.h"
#include "gstvaapivideobufferpool.h"
#endif

#define GST_PLUGIN_NAME "vaapiencode"
#define GST_PLUGIN_DESC "A VA-API based video encoder"

#define GST_VAAPI_ENCODE_FLOW_TIMEOUT           GST_FLOW_CUSTOM_SUCCESS
#define GST_VAAPI_ENCODE_FLOW_MEM_ERROR         GST_FLOW_CUSTOM_ERROR
#define GST_VAAPI_ENCODE_FLOW_CONVERT_ERROR     GST_FLOW_CUSTOM_ERROR_1
#define GST_VAAPI_ENCODE_FLOW_CODEC_DATA_ERROR  GST_FLOW_CUSTOM_ERROR_2

GST_DEBUG_CATEGORY_STATIC (gst_vaapiencode_debug);
#define GST_CAT_DEFAULT gst_vaapiencode_debug

/* GstImplementsInterface interface */
#if !GST_CHECK_VERSION(1,0,0)
static gboolean
gst_vaapiencode_implements_interface_supported (GstImplementsInterface * iface,
    GType type)
{
  return (type == GST_TYPE_VIDEO_CONTEXT);
}

static void
gst_vaapiencode_implements_iface_init (GstImplementsInterfaceClass * iface)
{
  iface->supported = gst_vaapiencode_implements_interface_supported;
}
#endif

/* GstContext interface */
#if GST_CHECK_VERSION(1,1,0)
static void
gst_vaapiencode_set_context (GstElement * element, GstContext * context)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (element);
  GstVaapiDisplay *display = NULL;

  if (gst_vaapi_video_context_get_display (context, &display)) {
    GST_INFO_OBJECT (element, "set display %p", display);
    GST_VAAPI_PLUGIN_BASE_DISPLAY_REPLACE (encode, display);
    gst_vaapi_display_unref (display);
  }
}
#else
static void
gst_vaapiencode_set_video_context (GstVideoContext * context,
    const gchar * type, const GValue * value)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (context);

  gst_vaapi_set_display (type, value, &GST_VAAPI_PLUGIN_BASE_DISPLAY (encode));
}

static void
gst_video_context_interface_init (GstVideoContextInterface * iface)
{
  iface->set_context = gst_vaapiencode_set_video_context;
}

#define GstVideoContextClass GstVideoContextInterface
#endif

G_DEFINE_TYPE_WITH_CODE (GstVaapiEncode,
    gst_vaapiencode, GST_TYPE_VIDEO_ENCODER,
#if !GST_CHECK_VERSION(1,0,0)
    G_IMPLEMENT_INTERFACE (GST_TYPE_IMPLEMENTS_INTERFACE,
        gst_vaapiencode_implements_iface_init);
#endif
#if !GST_CHECK_VERSION(1,1,0)
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_CONTEXT,
        gst_video_context_interface_init)
#endif
    )

enum
{
  PROP_0,
  PROP_RATE_CONTROL,
  PROP_BITRATE,
};

static inline gboolean
ensure_display (GstVaapiEncode * encode)
{
  return gst_vaapi_ensure_display (encode,
      GST_VAAPI_DISPLAY_TYPE_ANY, &GST_VAAPI_PLUGIN_BASE_DISPLAY (encode));
}

static gboolean
ensure_uploader (GstVaapiEncode * encode)
{
#if !GST_CHECK_VERSION(1,0,0)
  if (!ensure_display (encode))
    return FALSE;

  if (!encode->uploader) {
    encode->uploader = gst_vaapi_uploader_new (
        GST_VAAPI_PLUGIN_BASE_DISPLAY (encode));
    if (!encode->uploader)
      return FALSE;
  }

  if (!gst_vaapi_uploader_ensure_display (encode->uploader,
           GST_VAAPI_PLUGIN_BASE_DISPLAY (encode)))
    return FALSE;
#endif
  return TRUE;
}

static gboolean
ensure_uploader_caps (GstVaapiEncode * encode)
{
#if !GST_CHECK_VERSION(1,0,0)
  if (GST_VIDEO_INFO_IS_YUV (&encode->sink_video_info) &&
      !gst_vaapi_uploader_ensure_caps (encode->uploader, encode->sinkpad_caps,
          NULL))
    return FALSE;
#endif
  return TRUE;
}

static gboolean
gst_vaapiencode_query (GST_PAD_QUERY_FUNCTION_ARGS)
{
  GstVaapiEncode *const encode =
      GST_VAAPIENCODE_CAST (gst_pad_get_parent_element (pad));
  gboolean success;

  GST_INFO_OBJECT (encode, "query type %s", GST_QUERY_TYPE_NAME (query));

  if (gst_vaapi_reply_to_query (query, GST_VAAPI_PLUGIN_BASE_DISPLAY (encode)))
    success = TRUE;
  else if (GST_PAD_IS_SINK (pad))
    success = GST_PAD_QUERY_FUNCTION_CALL (encode->sinkpad_query,
        encode->sinkpad, parent, query);
  else
    success = GST_PAD_QUERY_FUNCTION_CALL (encode->srcpad_query,
        encode->srcpad, parent, query);

  gst_object_unref (encode);
  return success;
}

static GstFlowReturn
gst_vaapiencode_default_allocate_buffer (GstVaapiEncode * encode,
    GstVaapiCodedBuffer * coded_buf, GstBuffer ** outbuf_ptr)
{
  GstBuffer *buf;
  gint32 buf_size;

  g_return_val_if_fail (coded_buf != NULL, GST_FLOW_ERROR);
  g_return_val_if_fail (outbuf_ptr != NULL, GST_FLOW_ERROR);

  buf_size = gst_vaapi_coded_buffer_get_size (coded_buf);
  if (buf_size <= 0)
    goto error_invalid_buffer;

#if GST_CHECK_VERSION(1,0,0)
  buf =
      gst_video_encoder_allocate_output_buffer (GST_VIDEO_ENCODER_CAST (encode),
      buf_size);
#else
  buf = gst_buffer_new_and_alloc (buf_size);
#endif
  if (!buf)
    goto error_create_buffer;
  if (!gst_vaapi_coded_buffer_copy_into (buf, coded_buf))
    goto error_copy_buffer;

  *outbuf_ptr = buf;
  return GST_FLOW_OK;

  /* ERRORS */
error_invalid_buffer:
  {
    GST_ERROR ("invalid GstVaapiCodedBuffer size (%d)", buf_size);
    return GST_VAAPI_ENCODE_FLOW_MEM_ERROR;
  }
error_create_buffer:
  {
    GST_ERROR ("failed to create output buffer of size %d", buf_size);
    return GST_VAAPI_ENCODE_FLOW_MEM_ERROR;
  }
error_copy_buffer:
  {
    GST_ERROR ("failed to copy GstVaapiCodedBuffer data");
    gst_buffer_unref (buf);
    return GST_VAAPI_ENCODE_FLOW_MEM_ERROR;
  }
}

static GstFlowReturn
gst_vaapiencode_push_frame (GstVaapiEncode * encode, gint64 timeout)
{
  GstVideoEncoder *const venc = GST_VIDEO_ENCODER_CAST (encode);
  GstVaapiEncodeClass *const klass = GST_VAAPIENCODE_GET_CLASS (encode);
  GstVideoCodecFrame *out_frame;
  GstVaapiCodedBufferProxy *codedbuf_proxy = NULL;
  GstVaapiEncoderStatus status;
  GstBuffer *out_buffer;
  GstFlowReturn ret;

  status = gst_vaapi_encoder_get_buffer_with_timeout (encode->encoder,
      &codedbuf_proxy, timeout);
  if (status == GST_VAAPI_ENCODER_STATUS_NO_BUFFER)
    return GST_VAAPI_ENCODE_FLOW_TIMEOUT;
  if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    goto error_get_buffer;

  out_frame = gst_vaapi_coded_buffer_proxy_get_user_data (codedbuf_proxy);
  if (!out_frame)
    goto error_get_buffer;
  gst_video_codec_frame_ref (out_frame);
  gst_video_codec_frame_set_user_data (out_frame, NULL, NULL);

  /* Allocate and copy buffer into system memory */
  out_buffer = NULL;
  ret = klass->allocate_buffer (encode,
      GST_VAAPI_CODED_BUFFER_PROXY_BUFFER (codedbuf_proxy), &out_buffer);
  gst_vaapi_coded_buffer_proxy_replace (&codedbuf_proxy, NULL);
  if (ret != GST_FLOW_OK)
    goto error_allocate_buffer;

  gst_buffer_replace (&out_frame->output_buffer, out_buffer);
  gst_buffer_unref (out_buffer);

  /* Check output caps */
  GST_VIDEO_ENCODER_STREAM_LOCK (encode);
  if (!encode->out_caps_done) {
    GstVideoCodecState *old_state, *new_state;
    GstBuffer *codec_data;

    status = gst_vaapi_encoder_get_codec_data (encode->encoder, &codec_data);
    if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
      goto error_codec_data;

    if (codec_data) {
      encode->srcpad_caps = gst_caps_make_writable (encode->srcpad_caps);
      gst_caps_set_simple (encode->srcpad_caps,
          "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
      gst_buffer_unref (codec_data);
      old_state =
          gst_video_encoder_get_output_state (GST_VIDEO_ENCODER_CAST (encode));
      new_state =
          gst_video_encoder_set_output_state (GST_VIDEO_ENCODER_CAST (encode),
          gst_caps_ref (encode->srcpad_caps), old_state);
      gst_video_codec_state_unref (old_state);
      gst_video_codec_state_unref (new_state);
      GST_DEBUG ("updated srcpad caps to: %" GST_PTR_FORMAT,
          encode->srcpad_caps);
    }
    encode->out_caps_done = TRUE;
  }
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encode);

  GST_DEBUG ("output:%" GST_TIME_FORMAT ", size:%d",
      GST_TIME_ARGS (out_frame->pts), gst_buffer_get_size (out_buffer));

  return gst_video_encoder_finish_frame (venc, out_frame);

  /* ERRORS */
error_get_buffer:
  {
    GST_ERROR ("failed to get encoded buffer (status %d)", status);
    if (codedbuf_proxy)
      gst_vaapi_coded_buffer_proxy_unref (codedbuf_proxy);
    return GST_FLOW_ERROR;
  }
error_allocate_buffer:
  {
    GST_ERROR ("failed to allocate encoded buffer in system memory");
    if (out_buffer)
      gst_buffer_unref (out_buffer);
    gst_video_codec_frame_unref (out_frame);
    return ret;
  }
error_codec_data:
  {
    GST_ERROR ("failed to construct codec-data (status %d)", status);
    GST_VIDEO_ENCODER_STREAM_UNLOCK (encode);
    gst_video_codec_frame_unref (out_frame);
    return GST_VAAPI_ENCODE_FLOW_CODEC_DATA_ERROR;
  }
}

static void
gst_vaapiencode_buffer_loop (GstVaapiEncode * encode)
{
  GstFlowReturn ret;
  const gint64 timeout = 50000; /* microseconds */

  ret = gst_vaapiencode_push_frame (encode, timeout);
  if (ret == GST_FLOW_OK || ret == GST_VAAPI_ENCODE_FLOW_TIMEOUT)
    return;

  gst_pad_pause_task (encode->srcpad);
}

static GstCaps *
gst_vaapiencode_get_caps_impl (GstVideoEncoder * venc)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (venc);
  GstCaps *caps;

  if (encode->sinkpad_caps)
    caps = gst_caps_ref (encode->sinkpad_caps);
  else {
#if GST_CHECK_VERSION(1,0,0)
    caps = gst_pad_get_pad_template_caps (encode->sinkpad);
#else
    caps = gst_caps_from_string (GST_VAAPI_SURFACE_CAPS);

    if (caps && ensure_uploader (encode)) {
      GstCaps *const yuv_caps = gst_vaapi_uploader_get_caps (encode->uploader);
      if (yuv_caps) {
        caps = gst_caps_make_writable (caps);
        gst_caps_append (caps, gst_caps_copy (yuv_caps));
      }
    }
#endif
  }
  return caps;
}

#if GST_CHECK_VERSION(1,0,0)
static GstCaps *
gst_vaapiencode_get_caps (GstVideoEncoder * venc, GstCaps * filter)
{
  GstCaps *caps, *out_caps;

  out_caps = gst_vaapiencode_get_caps_impl (venc);
  if (out_caps && filter) {
    caps = gst_caps_intersect_full (out_caps, filter, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (out_caps);
    out_caps = caps;
  }
  return out_caps;
}
#else
#define gst_vaapiencode_get_caps gst_vaapiencode_get_caps_impl
#endif

static gboolean
gst_vaapiencode_destroy (GstVaapiEncode * encode)
{
  gst_vaapi_encoder_replace (&encode->encoder, NULL);
#if GST_CHECK_VERSION(1,0,0)
  g_clear_object (&encode->video_buffer_pool);
#endif
  g_clear_object (&encode->uploader);
  gst_caps_replace (&encode->sinkpad_caps, NULL);
  gst_caps_replace (&encode->srcpad_caps, NULL);
  return TRUE;
}

static gboolean
ensure_encoder (GstVaapiEncode * encode)
{
  GstVaapiEncodeClass *klass = GST_VAAPIENCODE_GET_CLASS (encode);

  g_return_val_if_fail (klass->create_encoder, FALSE);

  if (!ensure_display (encode))
    return FALSE;
  if (!ensure_uploader (encode))
    return FALSE;
  if (!ensure_uploader_caps (encode))
    return FALSE;

  encode->encoder = klass->create_encoder (encode,
      GST_VAAPI_PLUGIN_BASE_DISPLAY (encode));
  if (!encode->encoder)
    return FALSE;
  return TRUE;
}

static gboolean
gst_vaapiencode_open (GstVideoEncoder * venc)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (venc);
  GstVaapiDisplay *const old_display = GST_VAAPI_PLUGIN_BASE_DISPLAY (encode);
  gboolean success;

  if (!gst_vaapi_plugin_base_open (GST_VAAPI_PLUGIN_BASE (encode)))
    return FALSE;

  GST_VAAPI_PLUGIN_BASE_DISPLAY (encode) = NULL;
  success = ensure_display (encode);
  if (old_display)
    gst_vaapi_display_unref (old_display);
  return success;
}

static gboolean
gst_vaapiencode_close (GstVideoEncoder * venc)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (venc);

  gst_vaapiencode_destroy (encode);
  gst_vaapi_plugin_base_close (GST_VAAPI_PLUGIN_BASE (encode));
  return TRUE;
}

static inline gboolean
gst_vaapiencode_update_sink_caps (GstVaapiEncode * encode,
    GstVideoCodecState * state)
{
  gst_caps_replace (&encode->sinkpad_caps, state->caps);
  encode->sink_video_info = state->info;

#if !GST_CHECK_VERSION(1,0,0)
  if (GST_VIDEO_INFO_IS_YUV (&encode->sink_video_info)) {
    /* Ensure the uploader is set up for upstream allocated buffers */
    GstVaapiUploader *const uploader = encode->uploader;
    if (!gst_vaapi_uploader_ensure_display (uploader, GST_VAAPI_PLUGIN_BASE_DISPLAY (encode)))
      return FALSE;
    if (!gst_vaapi_uploader_ensure_caps (uploader, state->caps, NULL))
      return FALSE;
  }
#endif
  return TRUE;
}

static gboolean
gst_vaapiencode_update_src_caps (GstVaapiEncode * encode,
    GstVideoCodecState * in_state)
{
  GstVideoCodecState *out_state;
  GstStructure *structure;
  GstCaps *outcaps, *allowed_caps, *template_caps, *intersect;
  GstVaapiEncoderStatus status;
  GstBuffer *codec_data = NULL;

  g_return_val_if_fail (encode->encoder, FALSE);

  encode->out_caps_done = FALSE;

  /* get peer caps for stream-format avc/bytestream, codec_data */
  template_caps = gst_pad_get_pad_template_caps (encode->srcpad);
  allowed_caps = gst_pad_get_allowed_caps (encode->srcpad);
  intersect = gst_caps_intersect (template_caps, allowed_caps);
  gst_caps_unref (template_caps);
  gst_caps_unref (allowed_caps);

  /* codec data was not set */
  outcaps = gst_vaapi_encoder_set_format (encode->encoder, in_state, intersect);
  gst_caps_unref (intersect);
  g_return_val_if_fail (outcaps, FALSE);

  if (!gst_caps_is_fixed (outcaps)) {
    GST_ERROR ("encoder output caps was not fixed");
    gst_caps_unref (outcaps);
    return FALSE;
  }
  structure = gst_caps_get_structure (outcaps, 0);
  if (!gst_structure_has_field (structure, "codec_data")) {
    status = gst_vaapi_encoder_get_codec_data (encode->encoder, &codec_data);
    if (status == GST_VAAPI_ENCODER_STATUS_SUCCESS) {
      if (codec_data) {
        outcaps = gst_caps_make_writable (outcaps);
        gst_caps_set_simple (outcaps,
            "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
        gst_buffer_replace (&codec_data, NULL);
      }
      encode->out_caps_done = TRUE;
    }
  } else
    encode->out_caps_done = TRUE;

  out_state =
      gst_video_encoder_set_output_state (GST_VIDEO_ENCODER_CAST (encode),
      outcaps, in_state);

  gst_caps_replace (&encode->srcpad_caps, out_state->caps);
  gst_video_codec_state_unref (out_state);

  GST_DEBUG ("set srcpad caps to: %" GST_PTR_FORMAT, encode->srcpad_caps);
  return TRUE;
}

static gboolean
gst_vaapiencode_ensure_video_buffer_pool (GstVaapiEncode * encode,
    GstCaps * caps)
{
#if GST_CHECK_VERSION(1,0,0)
  GstBufferPool *pool;
  GstCaps *pool_caps;
  GstStructure *config;
  GstVideoInfo vi;
  gboolean need_pool;

  if (!ensure_display (encode))
    return FALSE;

  if (encode->video_buffer_pool) {
    config = gst_buffer_pool_get_config (encode->video_buffer_pool);
    gst_buffer_pool_config_get_params (config, &pool_caps, NULL, NULL, NULL);
    need_pool = !gst_caps_is_equal (caps, pool_caps);
    gst_structure_free (config);
    if (!need_pool)
      return TRUE;
    g_clear_object (&encode->video_buffer_pool);
    encode->video_buffer_size = 0;
  }

  pool = gst_vaapi_video_buffer_pool_new (GST_VAAPI_PLUGIN_BASE_DISPLAY (encode));
  if (!pool)
    goto error_create_pool;

  gst_video_info_init (&vi);
  gst_video_info_from_caps (&vi, caps);
  if (GST_VIDEO_INFO_FORMAT (&vi) == GST_VIDEO_FORMAT_ENCODED) {
    GST_DEBUG ("assume video buffer pool format is NV12");
    gst_video_info_set_format (&vi, GST_VIDEO_FORMAT_NV12,
        GST_VIDEO_INFO_WIDTH (&vi), GST_VIDEO_INFO_HEIGHT (&vi));
  }
  encode->video_buffer_size = vi.size;

  config = gst_buffer_pool_get_config (pool);
  gst_buffer_pool_config_set_params (config, caps, encode->video_buffer_size,
      0, 0);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VAAPI_VIDEO_META);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (!gst_buffer_pool_set_config (pool, config))
    goto error_pool_config;
  encode->video_buffer_pool = pool;
  return TRUE;

  /* ERRORS */
error_create_pool:
  {
    GST_ERROR ("failed to create buffer pool");
    return FALSE;
  }
error_pool_config:
  {
    GST_ERROR ("failed to reset buffer pool config");
    gst_object_unref (pool);
    return FALSE;
  }
#else
  return TRUE;
#endif
}

static gboolean
gst_vaapiencode_set_format (GstVideoEncoder * venc, GstVideoCodecState * state)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (venc);

  g_return_val_if_fail (state->caps != NULL, FALSE);

  if (!gst_vaapiencode_ensure_video_buffer_pool (encode, state->caps))
    return FALSE;

  if (!ensure_encoder (encode))
    return FALSE;
  if (!gst_vaapiencode_update_sink_caps (encode, state))
    return FALSE;
  if (!gst_vaapiencode_update_src_caps (encode, state))
    return FALSE;

#if GST_CHECK_VERSION(1,0,0)
  if (encode->out_caps_done && !gst_video_encoder_negotiate (venc)) {
    GST_ERROR ("failed to negotiate with caps %" GST_PTR_FORMAT,
        encode->srcpad_caps);
    return FALSE;
  }
#endif

  return gst_pad_start_task (encode->srcpad,
      (GstTaskFunction) gst_vaapiencode_buffer_loop, encode, NULL);
}

static gboolean
gst_vaapiencode_reset (GstVideoEncoder * venc, gboolean hard)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (venc);

  GST_DEBUG ("vaapiencode starting reset");

  /* FIXME: compare sink_caps with encoder */
  encode->out_caps_done = FALSE;
  return TRUE;
}

static GstFlowReturn
get_source_buffer (GstVaapiEncode * encode, GstBuffer * src_buffer,
    GstBuffer ** out_buffer_ptr)
{
  GstVaapiVideoMeta *meta;
  GstBuffer *out_buffer;
#if GST_CHECK_VERSION(1,0,0)
  GstVideoFrame src_frame, out_frame;
  gboolean success;
#endif

  meta = gst_buffer_get_vaapi_video_meta (src_buffer);
  if (meta) {
    *out_buffer_ptr = gst_buffer_ref (src_buffer);
    return GST_FLOW_OK;
  }

#if GST_CHECK_VERSION(1,0,0)
  if (!GST_VIDEO_INFO_IS_YUV (&encode->sink_video_info))
    goto error_invalid_buffer;

  if (!encode->video_buffer_pool)
    goto error_no_pool;

  if (!gst_buffer_pool_set_active (encode->video_buffer_pool, TRUE))
    goto error_activate_pool;

  out_buffer = NULL;
  success = gst_buffer_pool_acquire_buffer (encode->video_buffer_pool,
      &out_buffer, NULL) == GST_FLOW_OK;
  if (!success)
    goto error_create_buffer;

  if (!gst_video_frame_map (&src_frame, &encode->sink_video_info, src_buffer,
          GST_MAP_READ))
    goto error_map_src_buffer;

  if (!gst_video_frame_map (&out_frame, &encode->sink_video_info, out_buffer,
          GST_MAP_WRITE))
    goto error_map_dst_buffer;

  success = gst_video_frame_copy (&out_frame, &src_frame);
  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&src_frame);
  if (!success)
    goto error_copy_buffer;

  gst_buffer_copy_into (out_buffer, src_buffer, GST_BUFFER_COPY_TIMESTAMPS, 0,
      -1);

  *out_buffer_ptr = out_buffer;
  return GST_FLOW_OK;

  /* ERRORS */
error_invalid_buffer:
  {
    GST_ERROR ("unsupported video buffer");
    return GST_FLOW_EOS;
  }
error_no_pool:
  {
    GST_ERROR ("no buffer pool was negotiated");
    return GST_FLOW_ERROR;
  }
error_activate_pool:
  {
    GST_ERROR ("failed to activate buffer pool");
    return GST_FLOW_ERROR;
  }
error_map_dst_buffer:
  {
    gst_video_frame_unmap (&src_frame);
    // fall-through
  }
error_map_src_buffer:
  {
    GST_WARNING ("failed to map buffer. Skipping this frame");
    gst_buffer_unref (out_buffer);
    return GST_FLOW_OK;
  }
#else
  out_buffer = gst_vaapi_uploader_get_buffer (encode->uploader);
  if (!out_buffer)
    goto error_create_buffer;
  if (!gst_vaapi_uploader_process (encode->uploader, src_buffer, out_buffer))
    goto error_copy_buffer;

  gst_buffer_copy_metadata (out_buffer, src_buffer,
      GST_BUFFER_COPY_FLAGS | GST_BUFFER_COPY_TIMESTAMPS);

  *out_buffer_ptr = out_buffer;
  return GST_FLOW_OK;
#endif

  /* ERRORS */
error_create_buffer:
  {
    GST_WARNING ("failed to create buffer. Skipping this frame");
    return GST_FLOW_OK;
  }
error_copy_buffer:
  {
    GST_WARNING ("failed to upload buffer to VA surface. Skipping this frame");
    gst_buffer_unref (out_buffer);
    return GST_FLOW_OK;
  }
}

static GstFlowReturn
gst_vaapiencode_handle_frame (GstVideoEncoder * venc,
    GstVideoCodecFrame * frame)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (venc);
  GstVaapiEncoderStatus status;
  GstVaapiVideoMeta *meta;
  GstVaapiSurfaceProxy *proxy;
  GstFlowReturn ret;
  GstBuffer *buf;

  buf = NULL;
  ret = get_source_buffer (encode, frame->input_buffer, &buf);
  if (ret != GST_FLOW_OK)
    goto error_buffer_invalid;

  gst_buffer_replace (&frame->input_buffer, buf);
  gst_buffer_unref (buf);

  meta = gst_buffer_get_vaapi_video_meta (buf);
  if (!meta)
    goto error_buffer_no_meta;

  proxy = gst_vaapi_video_meta_get_surface_proxy (meta);
  if (!proxy)
    goto error_buffer_no_surface_proxy;

  gst_video_codec_frame_set_user_data (frame,
      gst_vaapi_surface_proxy_ref (proxy),
      (GDestroyNotify)gst_vaapi_surface_proxy_unref);

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encode);
  status = gst_vaapi_encoder_put_frame (encode->encoder, frame);
  GST_VIDEO_ENCODER_STREAM_LOCK (encode);
  if (status < GST_VAAPI_ENCODER_STATUS_SUCCESS)
    goto error_encode_frame;

  gst_video_codec_frame_unref (frame);
  return GST_FLOW_OK;

  /* ERRORS */
error_buffer_invalid:
  {
    if (buf)
      gst_buffer_unref (buf);
    gst_video_codec_frame_unref (frame);
    return ret;
  }
error_buffer_no_meta:
  {
    GST_ERROR ("failed to get GstVaapiVideoMeta information");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }
error_buffer_no_surface_proxy:
  {
    GST_ERROR ("failed to get VA surface proxy");
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }
error_encode_frame:
  {
    GST_ERROR ("failed to encode frame %d (status %d)",
        frame->system_frame_number, status);
    gst_video_codec_frame_unref (frame);
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_vaapiencode_finish (GstVideoEncoder * venc)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (venc);
  GstVaapiEncoderStatus status;
  GstFlowReturn ret = GST_FLOW_OK;

  status = gst_vaapi_encoder_flush (encode->encoder);

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encode);
  gst_pad_stop_task (encode->srcpad);
  GST_VIDEO_ENCODER_STREAM_LOCK (encode);

  while (status == GST_VAAPI_ENCODER_STATUS_SUCCESS && ret == GST_FLOW_OK)
    ret = gst_vaapiencode_push_frame (encode, 0);

  if (ret == GST_VAAPI_ENCODE_FLOW_TIMEOUT)
    ret = GST_FLOW_OK;
  return ret;
}

#if GST_CHECK_VERSION(1,0,0)
static gboolean
gst_vaapiencode_propose_allocation (GstVideoEncoder * venc, GstQuery * query)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (venc);
  GstCaps *caps = NULL;
  gboolean need_pool;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (need_pool) {
    if (!caps)
      goto error_no_caps;
    if (!gst_vaapiencode_ensure_video_buffer_pool (encode, caps))
      return FALSE;
    gst_query_add_allocation_pool (query, encode->video_buffer_pool,
        encode->video_buffer_size, 0, 0);
  }

  gst_query_add_allocation_meta (query, GST_VAAPI_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  return TRUE;

  /* ERRORS */
error_no_caps:
  {
    GST_ERROR ("no caps specified");
    return FALSE;
  }
}
#endif

static inline gboolean
check_ratecontrol (GstVaapiEncode * encode, GstVaapiRateControl rate_control)
{
  GstVaapiEncodeClass *const klass = GST_VAAPIENCODE_GET_CLASS (encode);

  return !klass->check_ratecontrol || klass->check_ratecontrol (encode,
      rate_control);
}

static void
gst_vaapiencode_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (object);

  switch (prop_id) {
    case PROP_RATE_CONTROL:
    {
      GstVaapiRateControl rate_control = g_value_get_enum (value);
      if (check_ratecontrol (encode, rate_control)) {
        encode->rate_control = rate_control;
      } else {
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      }
      break;
    }
    case PROP_BITRATE:
      encode->bitrate = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapiencode_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (object);

  switch (prop_id) {
    case PROP_RATE_CONTROL:
      g_value_set_enum (value, encode->rate_control);
      break;
    case PROP_BITRATE:
      g_value_set_uint (value, encode->bitrate);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapiencode_finalize (GObject * object)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE_CAST (object);

  gst_vaapiencode_destroy (encode);

  encode->sinkpad = NULL;
  encode->srcpad = NULL;

  gst_vaapi_plugin_base_finalize (GST_VAAPI_PLUGIN_BASE (object));
  G_OBJECT_CLASS (gst_vaapiencode_parent_class)->finalize (object);
}

static void
gst_vaapiencode_init (GstVaapiEncode * encode)
{
  gst_vaapi_plugin_base_init (GST_VAAPI_PLUGIN_BASE (encode), GST_CAT_DEFAULT);

  /* sink pad */
  encode->sinkpad = GST_VIDEO_ENCODER_SINK_PAD (encode);
  encode->sinkpad_query = GST_PAD_QUERYFUNC (encode->sinkpad);
  gst_pad_set_query_function (encode->sinkpad, gst_vaapiencode_query);
  gst_video_info_init (&encode->sink_video_info);

  /* src pad */
  encode->srcpad = GST_VIDEO_ENCODER_SRC_PAD (encode);
  encode->srcpad_query = GST_PAD_QUERYFUNC (encode->srcpad);
  gst_pad_set_query_function (encode->srcpad, gst_vaapiencode_query);

  gst_pad_use_fixed_caps (encode->srcpad);
}

static void
gst_vaapiencode_class_init (GstVaapiEncodeClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
#if GST_CHECK_VERSION(1,1,0)
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
#endif
  GstVideoEncoderClass *const venc_class = GST_VIDEO_ENCODER_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_vaapiencode_debug,
      GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

  gst_vaapi_plugin_base_class_init (GST_VAAPI_PLUGIN_BASE_CLASS (klass));

  object_class->finalize = gst_vaapiencode_finalize;
  object_class->set_property = gst_vaapiencode_set_property;
  object_class->get_property = gst_vaapiencode_get_property;

#if GST_CHECK_VERSION(1,1,0)
  element_class->set_context = GST_DEBUG_FUNCPTR (gst_vaapiencode_set_context);
#endif

  venc_class->open = GST_DEBUG_FUNCPTR (gst_vaapiencode_open);
  venc_class->close = GST_DEBUG_FUNCPTR (gst_vaapiencode_close);
  venc_class->set_format = GST_DEBUG_FUNCPTR (gst_vaapiencode_set_format);
  venc_class->reset = GST_DEBUG_FUNCPTR (gst_vaapiencode_reset);
  venc_class->handle_frame = GST_DEBUG_FUNCPTR (gst_vaapiencode_handle_frame);
  venc_class->finish = GST_DEBUG_FUNCPTR (gst_vaapiencode_finish);
  venc_class->getcaps = GST_DEBUG_FUNCPTR (gst_vaapiencode_get_caps);

#if GST_CHECK_VERSION(1,0,0)
  venc_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_vaapiencode_propose_allocation);
#endif

  klass->allocate_buffer = gst_vaapiencode_default_allocate_buffer;

  /* Registering debug symbols for function pointers */
  GST_DEBUG_REGISTER_FUNCPTR (gst_vaapiencode_query);

  g_object_class_install_property (object_class,
      PROP_RATE_CONTROL,
      g_param_spec_enum ("rate-control",
          "Rate Control",
          "Rate control mode",
          GST_VAAPI_TYPE_RATE_CONTROL,
          GST_VAAPI_RATECONTROL_NONE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (object_class,
      PROP_BITRATE,
      g_param_spec_uint ("bitrate",
          "Bitrate (kbps)",
          "The desired bitrate expressed in kbps (0: auto-calculate)",
          0, 100 * 1024, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
}
