/*
 *  gstvaapiencoder.c - VA encoder abstraction
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
#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapiencoder.h"
#include "gstvaapiencoder_priv.h"
#include "gstvaapicontext.h"
#include "gstvaapidisplay_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

typedef struct _GstVaapiCodedBufferProxyClass GstVaapiCodedBufferProxyClass;

#define GST_VAAPI_ENCODER_LOCK(encoder)                         \
  G_STMT_START {                                                \
    g_mutex_lock (&(GST_VAAPI_ENCODER_CAST(encoder))->lock);    \
  } G_STMT_END

#define GST_VAAPI_ENCODER_UNLOCK(encoder)                               \
  G_STMT_START {                                                        \
    g_mutex_unlock (&(GST_VAAPI_ENCODER_CAST(encoder))->lock);          \
  } G_STMT_END

#define GST_VAAPI_ENCODER_BUF_FREE_WAIT(encoder)                        \
  G_STMT_START {                                                        \
    g_cond_wait (&(GST_VAAPI_ENCODER_CAST(encoder))->codedbuf_free,     \
                 &(GST_VAAPI_ENCODER_CAST(encoder))->lock);             \
  } G_STMT_END

#define GST_VAAPI_ENCODER_BUF_FREE_SIGNAL(encoder)                      \
  G_STMT_START {                                                        \
    g_cond_signal (&(GST_VAAPI_ENCODER_CAST(encoder))->codedbuf_free);  \
  } G_STMT_END

#define GST_VAAPI_ENCODER_FREE_SURFACE_WAIT(encoder)                    \
  G_STMT_START {                                                        \
    g_cond_wait (&(GST_VAAPI_ENCODER_CAST(encoder))->surface_free,      \
                 &(GST_VAAPI_ENCODER_CAST(encoder))->lock);             \
  } G_STMT_END

#define GST_VAAPI_ENCODER_FREE_SURFACE_SIGNAL(encoder)                  \
  G_STMT_START {                                                        \
    g_cond_signal (&(GST_VAAPI_ENCODER_CAST(encoder))->surface_free);   \
  } G_STMT_END

#define GST_VAAPI_ENCODER_SYNC_SIGNAL(encoder)                          \
  G_STMT_START {                                                        \
    g_cond_signal (&(GST_VAAPI_ENCODER_CAST(encoder))->sync_ready);     \
  } G_STMT_END

static inline gboolean
GST_VAAPI_ENCODER_SYNC_WAIT_TIMEOUT (GstVaapiEncoder * encoder, gint64 timeout)
{
  gint64 end_time = g_get_monotonic_time () + timeout;
  return g_cond_wait_until (&encoder->sync_ready, &encoder->lock, end_time);
}

static GstVaapiCodedBuffer *
gst_vaapi_encoder_dequeue_coded_buffer (GstVaapiEncoder * encoder);

static void
gst_vaapi_encoder_queue_coded_buffer (GstVaapiEncoder * encoder,
    GstVaapiCodedBuffer * buf);

typedef struct
{
  GstVaapiEncPicture *picture;
  GstVaapiCodedBufferProxy *buf;
} GstVaapiEncoderSyncPic;

static void
gst_vaapi_coded_buffer_proxy_finalize (GstVaapiCodedBufferProxy * proxy)
{
  if (proxy->buffer) {
    gst_vaapi_coded_buffer_unmap (proxy->buffer);
    if (proxy->encoder)
      gst_vaapi_encoder_queue_coded_buffer (proxy->encoder, proxy->buffer);
    else {
      g_assert (FALSE);
      gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (proxy->buffer));
    }
    proxy->buffer = NULL;
  }
  gst_vaapi_encoder_replace (&proxy->encoder, NULL);
}

static void
gst_vaapi_coded_buffer_proxy_class_init (GstVaapiCodedBufferProxyClass * klass)
{
  GstVaapiMiniObjectClass *const object_class =
      GST_VAAPI_MINI_OBJECT_CLASS (klass);

  object_class->size = sizeof (GstVaapiCodedBufferProxy);
  object_class->finalize =
      (GDestroyNotify) gst_vaapi_coded_buffer_proxy_finalize;
}

static inline const GstVaapiCodedBufferProxyClass *
gst_vaapi_coded_buffer_proxy_class (void)
{
  static GstVaapiCodedBufferProxyClass g_class;
  static gsize g_class_init = FALSE;

  if (g_once_init_enter (&g_class_init)) {
    gst_vaapi_coded_buffer_proxy_class_init (&g_class);
    g_once_init_leave (&g_class_init, TRUE);
  }
  return (&g_class);
}

GstVaapiCodedBufferProxy *
gst_vaapi_coded_buffer_proxy_new (GstVaapiEncoder * encoder)
{
  GstVaapiCodedBuffer *buf;
  GstVaapiCodedBufferProxy *ret;

  g_assert (encoder);
  buf = gst_vaapi_encoder_dequeue_coded_buffer (encoder);
  if (!buf)
    return NULL;

  ret = (GstVaapiCodedBufferProxy *)
      gst_vaapi_mini_object_new0 (GST_VAAPI_MINI_OBJECT_CLASS
      (gst_vaapi_coded_buffer_proxy_class ()));
  g_assert (ret);
  ret->encoder = gst_vaapi_encoder_ref (encoder);
  ret->buffer = buf;
  return ret;
}

GstVaapiCodedBufferProxy *
gst_vaapi_coded_buffer_proxy_ref (GstVaapiCodedBufferProxy * proxy)
{
  return (GstVaapiCodedBufferProxy *)
      gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (proxy));
}

void
gst_vaapi_coded_buffer_proxy_unref (GstVaapiCodedBufferProxy * proxy)
{
  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (proxy));
}

void
gst_vaapi_coded_buffer_proxy_replace (GstVaapiCodedBufferProxy ** old_proxy_ptr,
    GstVaapiCodedBufferProxy * new_proxy)
{
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **) old_proxy_ptr,
      GST_VAAPI_MINI_OBJECT (new_proxy));
}

GstVaapiEncoder *
gst_vaapi_encoder_ref (GstVaapiEncoder * encoder)
{
  return gst_vaapi_object_ref (encoder);
}

void
gst_vaapi_encoder_unref (GstVaapiEncoder * encoder)
{
  gst_vaapi_object_unref (encoder);
}

void
gst_vaapi_encoder_replace (GstVaapiEncoder ** old_encoder_ptr,
    GstVaapiEncoder * new_encoder)
{
  gst_vaapi_object_replace (old_encoder_ptr, new_encoder);
}

static gboolean
gst_vaapi_encoder_init_coded_buffer_queue (GstVaapiEncoder * encoder,
    guint count)
{
  GstVaapiCodedBuffer *buf;
  guint i = 0;

  GST_VAAPI_ENCODER_LOCK (encoder);
  if (count > encoder->max_buf_num)
    count = encoder->max_buf_num;

  g_assert (encoder->buf_size);
  for (i = 0; i < count; ++i) {
    buf = GST_VAAPI_CODED_BUFFER_NEW (encoder, encoder->buf_size);
    g_queue_push_tail (&encoder->coded_buffers, buf);
    ++encoder->buf_count;
  }
  g_assert (encoder->buf_count <= encoder->max_buf_num);

  GST_VAAPI_ENCODER_UNLOCK (encoder);
  return TRUE;
}

static GstVaapiCodedBuffer *
gst_vaapi_encoder_dequeue_coded_buffer (GstVaapiEncoder * encoder)
{
  GstVaapiCodedBuffer *ret = NULL;

  GST_VAAPI_ENCODER_LOCK (encoder);
  while (encoder->buf_count >= encoder->max_buf_num &&
      g_queue_is_empty (&encoder->coded_buffers)) {
    GST_VAAPI_ENCODER_BUF_FREE_WAIT (encoder);
  }
  if (!g_queue_is_empty (&encoder->coded_buffers)) {
    ret = (GstVaapiCodedBuffer *) g_queue_pop_head (&encoder->coded_buffers);
    goto end;
  }

  g_assert (encoder->buf_size);
  ret = GST_VAAPI_CODED_BUFFER_NEW (encoder, encoder->buf_size);
  if (ret)
    ++encoder->buf_count;

end:
  GST_VAAPI_ENCODER_UNLOCK (encoder);
  return ret;
}

static void
gst_vaapi_encoder_queue_coded_buffer (GstVaapiEncoder * encoder,
    GstVaapiCodedBuffer * buf)
{
  g_assert (buf);
  g_return_if_fail (buf);

  GST_VAAPI_ENCODER_LOCK (encoder);
  g_queue_push_tail (&encoder->coded_buffers, buf);
  GST_VAAPI_ENCODER_BUF_FREE_SIGNAL (encoder);
  GST_VAAPI_ENCODER_UNLOCK (encoder);
}

static gboolean
gst_vaapi_encoder_free_coded_buffers (GstVaapiEncoder * encoder)
{
  GstVaapiCodedBuffer *buf;
  guint count = 0;
  gboolean ret;

  GST_VAAPI_ENCODER_LOCK (encoder);
  while (!g_queue_is_empty (&encoder->coded_buffers)) {
    buf = (GstVaapiCodedBuffer *) g_queue_pop_head (&encoder->coded_buffers);
    g_assert (buf);
    gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (buf));
    ++count;
  }
  ret = (count == encoder->buf_count);
  GST_VAAPI_ENCODER_UNLOCK (encoder);

  if (!ret) {
    GST_ERROR ("coded buffer leak, freed count:%d, total buf:%d",
        count, encoder->buf_count);
  }

  return ret;
}

static void
_surface_proxy_released_notify (GstVaapiEncoder * encoder)
{
  GST_VAAPI_ENCODER_FREE_SURFACE_SIGNAL (encoder);
}

GstVaapiSurfaceProxy *
gst_vaapi_encoder_create_surface (GstVaapiEncoder * encoder)
{
  GstVaapiSurfaceProxy *proxy;

  g_assert (encoder && encoder->context);
  g_return_val_if_fail (encoder->context, NULL);

  GST_VAAPI_ENCODER_LOCK (encoder);
  while (!gst_vaapi_context_get_surface_count (encoder->context)) {
    GST_VAAPI_ENCODER_FREE_SURFACE_WAIT (encoder);
  }
  proxy = gst_vaapi_context_get_surface_proxy (encoder->context);
  GST_VAAPI_ENCODER_UNLOCK (encoder);

  gst_vaapi_surface_proxy_set_destroy_notify (proxy,
      (GDestroyNotify) _surface_proxy_released_notify, encoder);

  return proxy;
}

void
gst_vaapi_encoder_release_surface (GstVaapiEncoder * encoder,
    GstVaapiSurfaceProxy * surface)
{
  GST_VAAPI_ENCODER_LOCK (encoder);
  gst_vaapi_surface_proxy_unref (surface);
  GST_VAAPI_ENCODER_UNLOCK (encoder);
}

static GstVaapiEncoderSyncPic *
_create_sync_picture (GstVaapiEncPicture * picture,
    GstVaapiCodedBufferProxy * coded_buf)
{
  GstVaapiEncoderSyncPic *sync = g_slice_new0 (GstVaapiEncoderSyncPic);

  g_assert (picture && coded_buf);
  sync->picture = gst_vaapi_enc_picture_ref (picture);
  sync->buf = gst_vaapi_coded_buffer_proxy_ref (coded_buf);
  return sync;
}

static void
_free_sync_picture (GstVaapiEncoder * encoder,
    GstVaapiEncoderSyncPic * sync_pic)
{
  g_assert (sync_pic);

  if (sync_pic->picture)
    gst_vaapi_enc_picture_unref (sync_pic->picture);
  if (sync_pic->buf)
    gst_vaapi_coded_buffer_proxy_unref (sync_pic->buf);
  g_slice_free (GstVaapiEncoderSyncPic, sync_pic);
}

static void
gst_vaapi_encoder_free_sync_pictures (GstVaapiEncoder * encoder)
{
  GstVaapiEncoderSyncPic *sync;

  GST_VAAPI_ENCODER_LOCK (encoder);
  while (!g_queue_is_empty (&encoder->sync_pictures)) {
    sync =
        (GstVaapiEncoderSyncPic *) g_queue_pop_head (&encoder->sync_pictures);
    _free_sync_picture (encoder, sync);
  }
  GST_VAAPI_ENCODER_UNLOCK (encoder);
}

static gboolean
gst_vaapi_encoder_push_sync_picture (GstVaapiEncoder * encoder,
    GstVaapiEncoderSyncPic * sync_pic)
{
  GST_VAAPI_ENCODER_LOCK (encoder);
  g_queue_push_tail (&encoder->sync_pictures, sync_pic);
  GST_VAAPI_ENCODER_SYNC_SIGNAL (encoder);
  GST_VAAPI_ENCODER_UNLOCK (encoder);
  return TRUE;
}

static GstVaapiEncoderStatus
gst_vaapi_encoder_pop_sync_picture (GstVaapiEncoder * encoder,
    GstVaapiEncoderSyncPic ** sync_pic, guint64 timeout)
{
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;

  *sync_pic = NULL;

  GST_VAAPI_ENCODER_LOCK (encoder);
  if (g_queue_is_empty (&encoder->sync_pictures) &&
      !GST_VAAPI_ENCODER_SYNC_WAIT_TIMEOUT (encoder, timeout))
    goto timeout;

  if (g_queue_is_empty (&encoder->sync_pictures)) {
    ret = GST_VAAPI_ENCODER_STATUS_UNKNOWN_ERR;
    goto end;
  }

  *sync_pic =
      (GstVaapiEncoderSyncPic *) g_queue_pop_head (&encoder->sync_pictures);
  g_assert (*sync_pic);
  ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
  goto end;

timeout:
  ret = GST_VAAPI_ENCODER_STATUS_TIMEOUT;

end:
  GST_VAAPI_ENCODER_UNLOCK (encoder);
  return ret;
}

GstVaapiEncoderStatus
gst_vaapi_encoder_put_frame (GstVaapiEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
  GstVaapiEncoderClass *klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);
  GstVaapiEncPicture *picture = NULL;
  GstVaapiCodedBufferProxy *coded_buf = NULL;
  GstVaapiEncoderSyncPic *sync_pic = NULL;

  if (!klass->reordering || !klass->encode)
    goto error;

again:
  picture = NULL;
  sync_pic = NULL;
  ret = klass->reordering (encoder, frame, FALSE, &picture);

  if (ret == GST_VAAPI_ENCODER_STATUS_FRAME_NOT_READY)
    return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  g_assert (picture);
  if (ret != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    goto error;
  if (!picture) {
    ret = GST_VAAPI_ENCODER_STATUS_PICTURE_ERR;
    goto error;
  }

  coded_buf = gst_vaapi_coded_buffer_proxy_new (encoder);
  if (!coded_buf) {
    ret = GST_VAAPI_ENCODER_STATUS_OBJECT_ERR;
    goto error;
  }

  ret = klass->encode (encoder, picture, coded_buf);
  if (ret != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    goto error;

  /* another thread would sync and get coded buffer */
  sync_pic = _create_sync_picture (picture, coded_buf);
  gst_vaapi_coded_buffer_proxy_replace (&coded_buf, NULL);
  gst_vaapi_enc_picture_replace (&picture, NULL);

  if (!gst_vaapi_encoder_push_sync_picture (encoder, sync_pic)) {
    ret = GST_VAAPI_ENCODER_STATUS_THREAD_ERR;
    goto error;
  }

  frame = NULL;
  goto again;

error:
  gst_vaapi_enc_picture_replace (&picture, NULL);
  gst_vaapi_coded_buffer_proxy_replace (&coded_buf, NULL);
  if (sync_pic)
    _free_sync_picture (encoder, sync_pic);
  GST_ERROR ("encoding failed, error:%d", ret);
  return ret;
}

GstVaapiEncoderStatus
gst_vaapi_encoder_get_buffer (GstVaapiEncoder * encoder,
    GstVideoCodecFrame ** frame,
    GstVaapiCodedBufferProxy ** codedbuf, gint64 us_of_timeout)
{
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
  GstVaapiEncoderSyncPic *sync_pic = NULL;
  GstVaapiSurfaceStatus surface_status;
  GstVaapiEncPicture *picture;

  ret = gst_vaapi_encoder_pop_sync_picture (encoder, &sync_pic, us_of_timeout);
  if (ret != GST_VAAPI_ENCODER_STATUS_SUCCESS)
    goto end;

  picture = sync_pic->picture;

  if (!picture->surface || !gst_vaapi_surface_sync (picture->surface)) {
    ret = GST_VAAPI_ENCODER_STATUS_PARAM_ERR;
    goto end;
  }
  if (!gst_vaapi_surface_query_status (picture->surface, &surface_status)) {
    ret = GST_VAAPI_ENCODER_STATUS_PICTURE_ERR;
    goto end;
  }
  if (frame)
    *frame = gst_video_codec_frame_ref (picture->frame);
  if (codedbuf)
    *codedbuf = gst_vaapi_coded_buffer_proxy_ref (sync_pic->buf);

end:
  if (sync_pic)
    _free_sync_picture (encoder, sync_pic);
  return ret;
}

GstVaapiEncoderStatus
gst_vaapi_encoder_flush (GstVaapiEncoder * encoder)
{
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);

  if (!klass->flush)
    goto error;

  ret = klass->flush (encoder);
  return ret;

error:
  GST_ERROR ("flush failed");
  return GST_VAAPI_ENCODER_STATUS_FUNC_PTR_ERR;
}

GstVaapiEncoderStatus
gst_vaapi_encoder_get_codec_data (GstVaapiEncoder * encoder,
    GstBuffer ** codec_data)
{
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);

  *codec_data = NULL;
  if (!klass->get_codec_data)
    return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  ret = klass->get_codec_data (encoder, codec_data);
  return ret;
}

static gboolean
gst_vaapi_encoder_ensure_context (GstVaapiEncoder * encoder)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);
  GstVaapiContextInfo info;
  GstVaapiContext *context;

  if (GST_VAAPI_ENCODER_CONTEXT (encoder))
    return TRUE;

  memset (&info, 0, sizeof (info));
  if (!klass->get_context_info || !klass->get_context_info (encoder, &info)) {
    return FALSE;
  }

  context = gst_vaapi_context_new_full (GST_VAAPI_ENCODER_DISPLAY (encoder),
      &info);
  if (!context)
    return FALSE;

  GST_VAAPI_ENCODER_CONTEXT (encoder) = context;
  GST_VAAPI_ENCODER_VA_CONTEXT (encoder) = gst_vaapi_context_get_id (context);
  return TRUE;
}

GstCaps *
gst_vaapi_encoder_set_format (GstVaapiEncoder * encoder,
    GstVideoCodecState * in_state, GstCaps * ref_caps)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);
  GstCaps *out_caps = NULL;

  if (!GST_VIDEO_INFO_WIDTH (&in_state->info) ||
      !GST_VIDEO_INFO_HEIGHT (&in_state->info)) {
    GST_WARNING ("encoder set format failed, width or height equal to 0.");
    return NULL;
  }
  GST_VAAPI_ENCODER_VIDEO_INFO (encoder) = in_state->info;

  if (!klass->set_format)
    goto error;

  out_caps = klass->set_format (encoder, in_state, ref_caps);
  if (!out_caps)
    goto error;

  if (GST_VAAPI_ENCODER_CAPS (encoder) &&
      gst_caps_is_equal (out_caps, GST_VAAPI_ENCODER_CAPS (encoder))) {
    gst_caps_unref (out_caps);
    return GST_VAAPI_ENCODER_CAPS (encoder);
  }
  gst_caps_replace (&GST_VAAPI_ENCODER_CAPS (encoder), out_caps);
  g_assert (GST_VAAPI_ENCODER_CONTEXT (encoder) == NULL);
  gst_vaapi_object_replace (&GST_VAAPI_ENCODER_CONTEXT (encoder), NULL);

  if (!gst_vaapi_encoder_ensure_context (encoder))
    goto error;

  encoder->buf_size = (GST_VAAPI_ENCODER_WIDTH (encoder) *
      GST_VAAPI_ENCODER_HEIGHT (encoder) * 400) / (16 * 16);

  if (!gst_vaapi_encoder_init_coded_buffer_queue (encoder, 5)) {
    GST_ERROR ("encoder init coded buffer failed");
    goto error;
  }

  return out_caps;

error:
  gst_caps_replace (&GST_VAAPI_ENCODER_CAPS (encoder), NULL);
  gst_caps_replace (&out_caps, NULL);
  GST_ERROR ("encoder set format failed");
  return NULL;
}

static gboolean
gst_vaapi_encoder_init (GstVaapiEncoder * encoder, GstVaapiDisplay * display)
{
  GstVaapiEncoderClass *kclass = GST_VAAPI_ENCODER_GET_CLASS (encoder);

  g_assert (kclass);
  g_assert (display);

  g_return_val_if_fail (display, FALSE);
  g_return_val_if_fail (encoder->display == NULL, FALSE);

  encoder->display = gst_vaapi_display_ref (display);
  encoder->va_display = gst_vaapi_display_get_display (display);
  encoder->context = NULL;
  encoder->va_context = VA_INVALID_ID;
  encoder->caps = NULL;

  gst_video_info_init (&encoder->video_info);

  encoder->buf_count = 0;
  encoder->max_buf_num = 10;
  encoder->buf_size = 0;

  g_mutex_init (&encoder->lock);
  g_cond_init (&encoder->codedbuf_free);
  g_cond_init (&encoder->surface_free);
  g_queue_init (&encoder->coded_buffers);
  g_queue_init (&encoder->sync_pictures);
  g_cond_init (&encoder->sync_ready);

  if (kclass->init)
    return kclass->init (encoder);
  return TRUE;
}

static void
gst_vaapi_encoder_destroy (GstVaapiEncoder * encoder)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);

  if (klass->destroy)
    klass->destroy (encoder);

  gst_vaapi_encoder_free_coded_buffers (encoder);
  gst_vaapi_encoder_free_sync_pictures (encoder);

  gst_vaapi_object_replace (&encoder->context, NULL);
  gst_vaapi_display_replace (&encoder->display, NULL);
  encoder->va_display = NULL;
  g_mutex_clear (&encoder->lock);
  g_cond_clear (&encoder->codedbuf_free);
  g_cond_clear (&encoder->surface_free);
  g_queue_clear (&encoder->coded_buffers);
  g_queue_clear (&encoder->sync_pictures);
  g_cond_clear (&encoder->sync_ready);
}

void
gst_vaapi_encoder_finalize (GstVaapiEncoder * encoder)
{
  gst_vaapi_encoder_destroy (encoder);
}

void
gst_vaapi_encoder_class_init (GstVaapiEncoderClass * klass)
{
  GstVaapiMiniObjectClass *const object_class =
      GST_VAAPI_MINI_OBJECT_CLASS (klass);

  object_class->size = sizeof (GstVaapiEncoder);
  object_class->finalize = (GDestroyNotify) gst_vaapi_encoder_finalize;
}

GstVaapiEncoder *
gst_vaapi_encoder_new (const GstVaapiEncoderClass * klass,
    GstVaapiDisplay * display)
{
  GstVaapiEncoder *encoder;

  encoder = (GstVaapiEncoder *)
      gst_vaapi_mini_object_new0 (GST_VAAPI_MINI_OBJECT_CLASS (klass));
  if (!encoder)
    return NULL;

  if (!gst_vaapi_encoder_init (encoder, display))
    goto error;
  return encoder;

error:
  gst_vaapi_encoder_unref (encoder);
  return NULL;
}
