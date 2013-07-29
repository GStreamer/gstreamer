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
#include <gst/video/videocontext.h>
#include <gst/vaapi/gstvaapidisplay.h>
#include <gst/vaapi/gstvaapiencoder_priv.h>
#include <gst/vaapi/gstvaapiencoder_objects.h>
#include "gstvaapiencode.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideometa.h"
#include "gstvaapivideomemory.h"
#include "gstvaapivideobufferpool.h"

#define GST_PLUGIN_NAME "vaapiencode"
#define GST_PLUGIN_DESC "A VA-API based video encoder"

#define GST_VAAPI_ENCODE_FLOW_TIMEOUT           GST_FLOW_CUSTOM_SUCCESS
#define GST_VAAPI_ENCODE_FLOW_MEM_ERROR         GST_FLOW_CUSTOM_ERROR
#define GST_VAAPI_ENCODE_FLOW_CONVERT_ERROR     GST_FLOW_CUSTOM_ERROR_1
#define GST_VAAPI_ENCODE_FLOW_CODEC_DATA_ERROR  GST_FLOW_CUSTOM_ERROR_2

typedef struct _GstVaapiEncodeFrameUserData
{
  GstVaapiEncObjUserDataHead head;
  GstBuffer *vaapi_buf;
} GstVaapiEncodeFrameUserData;

GST_DEBUG_CATEGORY_STATIC (gst_vaapiencode_debug);

#define GST_CAT_DEFAULT gst_vaapiencode_debug

#define GstVideoContextClass GstVideoContextInterface

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

/* context(display) interface */
static void
gst_vaapiencode_set_video_context (GstVideoContext * context,
    const gchar * type, const GValue * value)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (context);

  gst_vaapi_set_display (type, value, &encode->display);
}

static void
gst_video_context_interface_init (GstVideoContextInterface * iface)
{
  iface->set_context = gst_vaapiencode_set_video_context;
}

G_DEFINE_TYPE_WITH_CODE (GstVaapiEncode,
    gst_vaapiencode, GST_TYPE_VIDEO_ENCODER,
#if !GST_CHECK_VERSION(1,0,0)
    G_IMPLEMENT_INTERFACE (GST_TYPE_IMPLEMENTS_INTERFACE,
        gst_vaapiencode_implements_iface_init);
#endif
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_CONTEXT,
        gst_video_context_interface_init))

static gboolean
gst_vaapiencode_query (GstPad * pad, GstObject * parent,
    GstQuery * query)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (parent);
  gboolean success;

  GST_DEBUG ("vaapiencode query %s", GST_QUERY_TYPE_NAME (query));

  if (gst_vaapi_reply_to_query (query, encode->display))
    success = TRUE;
  else if (GST_PAD_IS_SINK (pad))
    success = encode->sinkpad_query (pad, parent, query);
  else
    success = encode->srcpad_query (pad, parent, query);;
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

  if (!gst_vaapi_coded_buffer_map (coded_buf, NULL))
    return GST_VAAPI_ENCODE_FLOW_MEM_ERROR;

  buf_size = gst_vaapi_coded_buffer_get_size (coded_buf);
  if (buf_size <= 0) {
    GST_ERROR ("get GstVaapiCodedBuf buffer size:%d", buf_size);
    return GST_VAAPI_ENCODE_FLOW_MEM_ERROR;
  }

  buf =
      gst_video_encoder_allocate_output_buffer (GST_VIDEO_ENCODER_CAST (encode),
      buf_size);
  if (!buf) {
    GST_ERROR ("failed to allocate output buffer of size %d", buf_size);
    return GST_VAAPI_ENCODE_FLOW_MEM_ERROR;
  }

  if (!gst_vaapi_coded_buffer_get_buffer (coded_buf, buf)) {
    GST_ERROR ("failed to get encoded buffer");
    gst_buffer_replace (&buf, NULL);
    return GST_VAAPI_ENCODE_FLOW_MEM_ERROR;
  }
  gst_vaapi_coded_buffer_unmap (coded_buf);
  *outbuf_ptr = buf;
  return GST_FLOW_OK;
}

static GstFlowReturn
gst_vaapiencode_push_frame (GstVaapiEncode * encode, gint64 ms_timeout)
{
  GstVideoEncoder *const venc = GST_VIDEO_ENCODER_CAST (encode);
  GstVaapiEncodeClass *klass = GST_VAAPIENCODE_GET_CLASS (encode);
  GstVideoCodecFrame *out_frame = NULL;
  GstVaapiCodedBufferProxy *coded_buf_proxy = NULL;
  GstVaapiCodedBuffer *coded_buf;
  GstVaapiEncoderStatus encode_status;
  GstBuffer *output_buf;
  GstFlowReturn ret;

  g_return_val_if_fail (klass->allocate_buffer, GST_FLOW_ERROR);

  encode_status = gst_vaapi_encoder_get_buffer (encode->encoder,
      &out_frame, &coded_buf_proxy, ms_timeout);
  if (encode_status == GST_VAAPI_ENCODER_STATUS_TIMEOUT)
    return GST_VAAPI_ENCODE_FLOW_TIMEOUT;

  if (encode_status != GST_VAAPI_ENCODER_STATUS_SUCCESS) {
    GST_ERROR ("get encoded buffer failed, status:%d", encode_status);
    ret = GST_FLOW_ERROR;
    goto error;
  }

  g_assert (out_frame);
  gst_video_codec_frame_set_user_data (out_frame, NULL, NULL);

  coded_buf = coded_buf_proxy->buffer;
  g_assert (coded_buf);

  /* alloc buffer */
  ret = klass->allocate_buffer (encode, coded_buf, &output_buf);
  if (ret != GST_FLOW_OK)
    goto error;

  out_frame->output_buffer = output_buf;

  gst_vaapi_coded_buffer_proxy_replace (&coded_buf_proxy, NULL);

  /* check out_caps, need lock first */
  GST_VIDEO_ENCODER_STREAM_LOCK (encode);
  if (!encode->out_caps_done) {
    GstVaapiEncoderStatus encoder_status;
    GstVideoCodecState *old_state, *new_state;
    GstBuffer *codec_data;

    encoder_status =
        gst_vaapi_encoder_get_codec_data (encode->encoder, &codec_data);
    if (encoder_status != GST_VAAPI_ENCODER_STATUS_SUCCESS) {
      ret = GST_VAAPI_ENCODE_FLOW_CODEC_DATA_ERROR;
      goto error_unlock;
    }
    if (codec_data) {
      encode->srcpad_caps = gst_caps_make_writable (encode->srcpad_caps);
      gst_caps_set_simple (encode->srcpad_caps,
          "codec_data", GST_TYPE_BUFFER, codec_data, NULL);
      gst_buffer_replace (&codec_data, NULL);
      old_state =
          gst_video_encoder_get_output_state (GST_VIDEO_ENCODER_CAST (encode));
      new_state =
          gst_video_encoder_set_output_state (GST_VIDEO_ENCODER_CAST (encode),
          gst_caps_ref (encode->srcpad_caps), old_state);
      gst_video_codec_state_unref (old_state);
      gst_video_codec_state_unref (new_state);
      if (!gst_video_encoder_negotiate (GST_VIDEO_ENCODER_CAST (encode))) {
        GST_ERROR ("failed to negotiate with caps %" GST_PTR_FORMAT,
            encode->srcpad_caps);
        ret = GST_FLOW_NOT_NEGOTIATED;
        goto error_unlock;
      }
      GST_DEBUG ("updated srcpad caps to: %" GST_PTR_FORMAT,
          encode->srcpad_caps);
    }
    encode->out_caps_done = TRUE;
  }
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encode);

  GST_DEBUG ("output:%" GST_TIME_FORMAT ", size:%d",
      GST_TIME_ARGS (out_frame->pts), gst_buffer_get_size (output_buf));

  ret = gst_video_encoder_finish_frame (venc, out_frame);
  out_frame = NULL;
  if (ret != GST_FLOW_OK)
    goto error;

  return GST_FLOW_OK;

error_unlock:
  GST_VIDEO_ENCODER_STREAM_UNLOCK (encode);
error:
  gst_vaapi_coded_buffer_proxy_replace (&coded_buf_proxy, NULL);
  if (out_frame)
    gst_video_codec_frame_unref (out_frame);

  return ret;
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
gst_vaapiencode_get_caps (GstPad * pad)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (GST_OBJECT_PARENT (pad));
  GstCaps *caps;

  if (encode->sinkpad_caps)
    caps = gst_caps_ref (encode->sinkpad_caps);
  else
    caps = gst_caps_copy (gst_pad_get_pad_template_caps (pad));
  return caps;
}

static gboolean
gst_vaapiencode_destroy (GstVaapiEncode * encode)
{
  gst_vaapi_encoder_replace (&encode->encoder, NULL);
  g_clear_object (&encode->video_buffer_pool);

  if (encode->sinkpad_caps) {
    gst_caps_unref (encode->sinkpad_caps);
    encode->sinkpad_caps = NULL;
  }

  if (encode->srcpad_caps) {
    gst_caps_unref (encode->srcpad_caps);
    encode->srcpad_caps = NULL;
  }

  gst_vaapi_display_replace (&encode->display, NULL);
  return TRUE;
}

static inline gboolean
ensure_display (GstVaapiEncode * encode)
{
  return gst_vaapi_ensure_display (encode,
      GST_VAAPI_DISPLAY_TYPE_ANY, &encode->display);
}

static gboolean
ensure_encoder (GstVaapiEncode * encode)
{
  GstVaapiEncodeClass *klass = GST_VAAPIENCODE_GET_CLASS (encode);

  g_return_val_if_fail (klass->create_encoder, FALSE);

  if (!ensure_display (encode))
    return FALSE;

  encode->encoder = klass->create_encoder (encode, encode->display);
  g_assert (encode->encoder);
  return (encode->encoder ? TRUE : FALSE);
}

static gboolean
gst_vaapiencode_open (GstVideoEncoder * venc)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (venc);
  GstVaapiDisplay *const old_display = encode->display;
  gboolean success;

  encode->display = NULL;
  success = ensure_display (encode);
  if (old_display)
    gst_vaapi_display_unref (old_display);

  GST_DEBUG ("ensure display %s, display:%p",
      (success ? "okay" : "failed"), encode->display);
  return success;
}

static gboolean
gst_vaapiencode_close (GstVideoEncoder * venc)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (venc);

  GST_DEBUG ("vaapiencode starting close");

  return gst_vaapiencode_destroy (encode);
}

static inline gboolean
gst_vaapiencode_update_sink_caps (GstVaapiEncode * encode,
    GstVideoCodecState * state)
{
  gst_caps_replace (&encode->sinkpad_caps, state->caps);
  encode->sink_video_info = state->info;
  return TRUE;
}

static gboolean
gst_vaapiencode_update_src_caps (GstVaapiEncode * encode,
    GstVideoCodecState * in_state)
{
  GstVideoCodecState *out_state;
  GstStructure *structure;
  GstCaps *outcaps, *allowed_caps, *template_caps, *intersect;
  GstVaapiEncoderStatus encoder_status;
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
    encoder_status =
        gst_vaapi_encoder_get_codec_data (encode->encoder, &codec_data);
    if (encoder_status == GST_VAAPI_ENCODER_STATUS_SUCCESS) {
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

  pool = gst_vaapi_video_buffer_pool_new (encode->display);
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
}

static gboolean
gst_vaapiencode_set_format (GstVideoEncoder * venc, GstVideoCodecState * state)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (venc);

  g_return_val_if_fail (state->caps != NULL, FALSE);

  if (!gst_vaapiencode_ensure_video_buffer_pool (encode, state->caps))
    return FALSE;

  if (!ensure_encoder (encode))
    return FALSE;
  if (!gst_vaapiencode_update_sink_caps (encode, state))
    return FALSE;
  if (!gst_vaapiencode_update_src_caps (encode, state))
    return FALSE;

  if (encode->out_caps_done && !gst_video_encoder_negotiate (venc)) {
    GST_ERROR ("failed to negotiate with caps %" GST_PTR_FORMAT,
        encode->srcpad_caps);
    return FALSE;
  }

  return gst_pad_start_task (encode->srcpad,
      (GstTaskFunction) gst_vaapiencode_buffer_loop, encode, NULL);
  return TRUE;
}

static gboolean
gst_vaapiencode_reset (GstVideoEncoder * venc, gboolean hard)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (venc);

  GST_DEBUG ("vaapiencode starting reset");

  /* FIXME: compare sink_caps with encoder */
  encode->is_running = FALSE;
  encode->out_caps_done = FALSE;
  return TRUE;
}

static GstFlowReturn
gst_vaapiencode_get_vaapi_buffer (GstVaapiEncode * encode,
    GstBuffer * src_buffer, GstBuffer ** out_buffer_ptr)
{
  GstVaapiVideoMeta *meta;
  GstBuffer *out_buffer;
  GstVideoFrame src_frame, out_frame;
  GstFlowReturn ret;

  *out_buffer_ptr = NULL;
  meta = gst_buffer_get_vaapi_video_meta (src_buffer);
  if (meta) {
    *out_buffer_ptr = gst_buffer_ref (src_buffer);
    return GST_FLOW_OK;
  }

  if (!GST_VIDEO_INFO_IS_YUV (&encode->sink_video_info)) {
    GST_ERROR ("unsupported video buffer");
    return GST_FLOW_EOS;
  }

  GST_DEBUG ("buffer %p not from our pool, copying", src_buffer);

  if (!encode->video_buffer_pool)
    goto error_no_pool;

  if (!gst_buffer_pool_set_active (encode->video_buffer_pool, TRUE))
    goto error_activate_pool;

  ret = gst_buffer_pool_acquire_buffer (encode->video_buffer_pool,
      &out_buffer, NULL);
  if (ret != GST_FLOW_OK)
    goto error_create_buffer;

  if (!gst_video_frame_map (&src_frame, &encode->sink_video_info, src_buffer,
          GST_MAP_READ))
    goto error_map_src_buffer;

  if (!gst_video_frame_map (&out_frame, &encode->sink_video_info, out_buffer,
          GST_MAP_WRITE))
    goto error_map_dst_buffer;

  gst_video_frame_copy (&out_frame, &src_frame);
  gst_video_frame_unmap (&out_frame);
  gst_video_frame_unmap (&src_frame);

  *out_buffer_ptr = out_buffer;
  return GST_FLOW_OK;

  /* ERRORS */
error_no_pool:
  GST_ERROR ("no buffer pool was negotiated");
  return GST_FLOW_ERROR;
error_activate_pool:
  GST_ERROR ("failed to activate buffer pool");
  return GST_FLOW_ERROR;
error_create_buffer:
  GST_WARNING ("failed to create image. Skipping this frame");
  return GST_FLOW_OK;
error_map_dst_buffer:
  gst_video_frame_unmap (&src_frame);
  // fall-through
error_map_src_buffer:
  GST_WARNING ("failed to map buffer. Skipping this frame");
  gst_buffer_unref (out_buffer);
  return GST_FLOW_OK;
}

static inline gpointer
_create_user_data (GstBuffer * buf)
{
  GstVaapiVideoMeta *meta;
  GstVaapiSurface *surface;
  GstVaapiEncodeFrameUserData *user_data;

  meta = gst_buffer_get_vaapi_video_meta (buf);
  if (!meta) {
    GST_DEBUG ("convert to vaapi buffer failed");
    return NULL;
  }
  surface = gst_vaapi_video_meta_get_surface (meta);
  if (!surface) {
    GST_DEBUG ("vaapi_meta of codec frame doesn't have vaapisurfaceproxy");
    return NULL;
  }

  user_data = g_slice_new0 (GstVaapiEncodeFrameUserData);
  user_data->head.surface = surface;
  user_data->vaapi_buf = gst_buffer_ref (buf);
  return user_data;
}

static void
_destroy_user_data (gpointer data)
{
  GstVaapiEncodeFrameUserData *user_data = (GstVaapiEncodeFrameUserData *) data;

  g_assert (data);
  if (!user_data)
    return;
  gst_buffer_replace (&user_data->vaapi_buf, NULL);
  g_slice_free (GstVaapiEncodeFrameUserData, user_data);
}

static GstFlowReturn
gst_vaapiencode_handle_frame (GstVideoEncoder * venc,
    GstVideoCodecFrame * frame)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (venc);
  GstFlowReturn ret = GST_FLOW_OK;
  GstVaapiEncoderStatus encoder_ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
  GstBuffer *vaapi_buf = NULL;
  gpointer user_data;

  g_assert (encode && encode->encoder);
  g_assert (frame && frame->input_buffer);

  ret =
      gst_vaapiencode_get_vaapi_buffer (encode, frame->input_buffer,
      &vaapi_buf);
  GST_VAAPI_ENCODER_CHECK_STATUS (ret == GST_FLOW_OK, ret,
      "convert to vaapi buffer failed");

  user_data = _create_user_data (vaapi_buf);
  GST_VAAPI_ENCODER_CHECK_STATUS (user_data,
      ret, "create frame user data failed");

  gst_video_codec_frame_set_user_data (frame, user_data, _destroy_user_data);

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encode);
  /*encoding frames */
  encoder_ret = gst_vaapi_encoder_put_frame (encode->encoder, frame);
  GST_VIDEO_ENCODER_STREAM_LOCK (encode);

  GST_VAAPI_ENCODER_CHECK_STATUS (GST_VAAPI_ENCODER_STATUS_SUCCESS <=
      encoder_ret, GST_FLOW_ERROR, "gst_vaapiencoder_encode failed.");

end:
  gst_video_codec_frame_unref (frame);
  gst_buffer_replace (&vaapi_buf, NULL);
  return ret;
}

static GstFlowReturn
gst_vaapiencode_finish (GstVideoEncoder * venc)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (venc);
  GstVaapiEncoderStatus status;
  GstFlowReturn ret = GST_FLOW_OK;

  GST_DEBUG ("vaapiencode starting finish");

  status = gst_vaapi_encoder_flush (encode->encoder);

  GST_VIDEO_ENCODER_STREAM_UNLOCK (encode);
  gst_pad_stop_task (encode->srcpad);
  GST_VIDEO_ENCODER_STREAM_LOCK (encode);

  while (status == GST_VAAPI_ENCODER_STATUS_SUCCESS && ret == GST_FLOW_OK)
    ret = gst_vaapiencode_push_frame (encode, 0);

  if (ret == GST_VAAPI_ENCODE_FLOW_TIMEOUT);
  ret = GST_FLOW_OK;
  return ret;
}

static gboolean
gst_vaapiencode_propose_allocation (GstVideoEncoder * venc, GstQuery * query)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (venc);
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

static void
gst_vaapiencode_finalize (GObject * object)
{
  GstVaapiEncode *const encode = GST_VAAPIENCODE (object);

  gst_vaapiencode_destroy (encode);

  encode->sinkpad = NULL;
  encode->srcpad = NULL;

  G_OBJECT_CLASS (gst_vaapiencode_parent_class)->finalize (object);
}

static void
gst_vaapiencode_init (GstVaapiEncode * encode)
{
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
  GstVideoEncoderClass *const venc_class = GST_VIDEO_ENCODER_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_vaapiencode_debug,
      GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

  object_class->finalize = gst_vaapiencode_finalize;

  venc_class->open = GST_DEBUG_FUNCPTR (gst_vaapiencode_open);
  venc_class->close = GST_DEBUG_FUNCPTR (gst_vaapiencode_close);
  venc_class->set_format = GST_DEBUG_FUNCPTR (gst_vaapiencode_set_format);
  venc_class->reset = GST_DEBUG_FUNCPTR (gst_vaapiencode_reset);
  venc_class->handle_frame = GST_DEBUG_FUNCPTR (gst_vaapiencode_handle_frame);
  venc_class->finish = GST_DEBUG_FUNCPTR (gst_vaapiencode_finish);

  venc_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_vaapiencode_propose_allocation);

  klass->allocate_buffer = gst_vaapiencode_default_allocate_buffer;

  /* Registering debug symbols for function pointers */
  GST_DEBUG_REGISTER_FUNCPTR (gst_vaapiencode_get_caps);
  GST_DEBUG_REGISTER_FUNCPTR (gst_vaapiencode_query);
}
