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

/**
 * gst_vaapi_encoder_ref:
 * @encoder: a #GstVaapiEncoder
 *
 * Atomically increases the reference count of the given @encoder by one.
 *
 * Returns: The same @encoder argument
 */
GstVaapiEncoder *
gst_vaapi_encoder_ref (GstVaapiEncoder * encoder)
{
  return gst_vaapi_object_ref (encoder);
}

/**
 * gst_vaapi_encoder_unref:
 * @encoder: a #GstVaapiEncoder
 *
 * Atomically decreases the reference count of the @encoder by one. If
 * the reference count reaches zero, the encoder will be free'd.
 */
void
gst_vaapi_encoder_unref (GstVaapiEncoder * encoder)
{
  gst_vaapi_object_unref (encoder);
}

/**
 * gst_vaapi_encoder_replace:
 * @old_encoder_ptr: a pointer to a #GstVaapiEncoder
 * @new_encoder: a #GstVaapiEncoder
 *
 * Atomically replaces the encoder encoder held in @old_encoder_ptr
 * with @new_encoder. This means that @old_encoder_ptr shall reference
 * a valid encoder. However, @new_encoder can be NULL.
 */
void
gst_vaapi_encoder_replace (GstVaapiEncoder ** old_encoder_ptr,
    GstVaapiEncoder * new_encoder)
{
  gst_vaapi_object_replace (old_encoder_ptr, new_encoder);
}

/* Notifies gst_vaapi_encoder_create_coded_buffer() that a new buffer is free */
static void
_coded_buffer_proxy_released_notify (GstVaapiEncoder * encoder)
{
  g_mutex_lock (&encoder->mutex);
  g_cond_signal (&encoder->codedbuf_free);
  g_mutex_unlock (&encoder->mutex);
}

/* Creates a new VA coded buffer object proxy, backed from a pool */
static GstVaapiCodedBufferProxy *
gst_vaapi_encoder_create_coded_buffer (GstVaapiEncoder * encoder)
{
  GstVaapiCodedBufferPool *const pool =
    GST_VAAPI_CODED_BUFFER_POOL (encoder->codedbuf_pool);
  GstVaapiCodedBufferProxy *codedbuf_proxy;

  g_mutex_lock (&encoder->mutex);
  do {
    codedbuf_proxy = gst_vaapi_coded_buffer_proxy_new_from_pool (pool);
    if (codedbuf_proxy)
      break;

    /* Wait for a free coded buffer to become available */
    g_cond_wait (&encoder->codedbuf_free, &encoder->mutex);
    codedbuf_proxy = gst_vaapi_coded_buffer_proxy_new_from_pool (pool);
  } while (0);
  g_mutex_unlock (&encoder->mutex);
  if (!codedbuf_proxy)
    return NULL;

  gst_vaapi_coded_buffer_proxy_set_destroy_notify (codedbuf_proxy,
      (GDestroyNotify)_coded_buffer_proxy_released_notify, encoder);
  return codedbuf_proxy;
}

/* Notifies gst_vaapi_encoder_create_surface() that a new surface is free */
static void
_surface_proxy_released_notify (GstVaapiEncoder * encoder)
{
  g_mutex_lock (&encoder->mutex);
  g_cond_signal (&encoder->surface_free);
  g_mutex_unlock (&encoder->mutex);
}

/* Creates a new VA surface object proxy, backed from a pool and
   useful to allocate reconstructed surfaces */
GstVaapiSurfaceProxy *
gst_vaapi_encoder_create_surface (GstVaapiEncoder * encoder)
{
  GstVaapiSurfaceProxy *proxy;

  g_return_val_if_fail (encoder->context != NULL, NULL);

  g_mutex_lock (&encoder->mutex);
  for (;;) {
    proxy = gst_vaapi_context_get_surface_proxy (encoder->context);
    if (proxy)
      break;

    /* Wait for a free surface proxy to become available */
    g_cond_wait (&encoder->surface_free, &encoder->mutex);
  }
  g_mutex_unlock (&encoder->mutex);

  gst_vaapi_surface_proxy_set_destroy_notify (proxy,
      (GDestroyNotify) _surface_proxy_released_notify, encoder);
  return proxy;
}

/**
 * gst_vaapi_encoder_put_frame:
 * @encoder: a #GstVaapiEncoder
 * @frame: a #GstVideoCodecFrame
 *
 * Queues a #GstVideoCodedFrame to the HW encoder. The encoder holds
 * an extra reference to the @frame.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_put_frame (GstVaapiEncoder * encoder,
    GstVideoCodecFrame * frame)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);
  GstVaapiEncoderStatus status;
  GstVaapiEncPicture *picture;
  GstVaapiCodedBufferProxy *codedbuf_proxy;

  for (;;) {
    picture = NULL;
    status = klass->reordering (encoder, frame, &picture);
    if (status == GST_VAAPI_ENCODER_STATUS_NO_SURFACE)
      break;
    if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
      goto error_reorder_frame;

    codedbuf_proxy = gst_vaapi_encoder_create_coded_buffer (encoder);
    if (!codedbuf_proxy)
      goto error_create_coded_buffer;

    status = klass->encode (encoder, picture, codedbuf_proxy);
    if (status != GST_VAAPI_ENCODER_STATUS_SUCCESS)
      goto error_encode;

    gst_vaapi_coded_buffer_proxy_set_user_data (codedbuf_proxy,
        picture, (GDestroyNotify) gst_vaapi_enc_picture_unref);
    g_async_queue_push (encoder->codedbuf_queue, codedbuf_proxy);

    /* Try again with any pending reordered frame now available for encoding */
    frame = NULL;
  }
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_reorder_frame:
  {
    GST_ERROR ("failed to process reordered frames");
    return status;
  }
error_create_coded_buffer:
  {
    GST_ERROR ("failed to allocate coded buffer");
    gst_vaapi_enc_picture_unref (picture);
    return GST_VAAPI_ENCODER_STATUS_ERROR_ALLOCATION_FAILED;
  }
error_encode:
  {
    GST_ERROR ("failed to encode frame (status = %d)", status);
    gst_vaapi_enc_picture_unref (picture);
    gst_vaapi_coded_buffer_proxy_unref (codedbuf_proxy);
    return status;
  }
}

/**
 * gst_vaapi_encoder_get_buffer_with_timeout:
 * @encoder: a #GstVaapiEncoder
 * @out_codedbuf_proxy_ptr: the next coded buffer as a #GstVaapiCodedBufferProxy
 * @timeout: the number of microseconds to wait for the coded buffer, at most
 *
 * Upon successful return, *@out_codedbuf_proxy_ptr contains the next
 * coded buffer as a #GstVaapiCodedBufferProxy. The caller owns this
 * object, so gst_vaapi_coded_buffer_proxy_unref() shall be called
 * after usage. Otherwise, @GST_VAAPI_DECODER_STATUS_ERROR_NO_BUFFER
 * is returned if no coded buffer is available so far (timeout).
 *
 * The parent frame is available as a #GstVideoCodecFrame attached to
 * the user-data anchor of the output coded buffer. Ownership of the
 * frame is transferred to the coded buffer.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_get_buffer_with_timeout (GstVaapiEncoder * encoder,
    GstVaapiCodedBufferProxy ** out_codedbuf_proxy_ptr, guint64 timeout)
{
  GstVaapiEncPicture *picture;
  GstVaapiCodedBufferProxy *codedbuf_proxy;

  codedbuf_proxy = g_async_queue_timeout_pop (encoder->codedbuf_queue, timeout);
  if (!codedbuf_proxy)
    return GST_VAAPI_ENCODER_STATUS_NO_BUFFER;

  /* Wait for completion of all operations and report any error that occurred */
  picture = gst_vaapi_coded_buffer_proxy_get_user_data (codedbuf_proxy);
  if (!gst_vaapi_surface_sync (picture->surface))
    goto error_invalid_buffer;

  gst_vaapi_coded_buffer_proxy_set_user_data (codedbuf_proxy,
      gst_video_codec_frame_ref (picture->frame),
      (GDestroyNotify) gst_video_codec_frame_unref);

  if (out_codedbuf_proxy_ptr)
    *out_codedbuf_proxy_ptr = gst_vaapi_coded_buffer_proxy_ref (codedbuf_proxy);
  gst_vaapi_coded_buffer_proxy_unref (codedbuf_proxy);
  return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  /* ERRORS */
error_invalid_buffer:
  {
    GST_ERROR ("failed to encode the frame");
    gst_vaapi_coded_buffer_proxy_unref (codedbuf_proxy);
    return GST_VAAPI_ENCODER_STATUS_ERROR_INVALID_SURFACE;
  }
}

/**
 * gst_vaapi_encoder_flush:
 * @encoder: a #GstVaapiEncoder
 *
 * Submits any pending (reordered) frame for encoding.
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_flush (GstVaapiEncoder * encoder)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);

  return klass->flush (encoder);
}

/**
 * gst_vaapi_encoder_get_codec_data:
 * @encoder: a #GstVaapiEncoder
 * @out_codec_data_ptr: the pointer to the resulting codec-data (#GstBuffer)
 *
 * Returns a codec-data buffer that best represents the encoded
 * bitstream. Upon successful return, and if the @out_codec_data_ptr
 * contents is not NULL, then the caller function shall deallocates
 * that buffer with gst_buffer_unref().
 *
 * Return value: a #GstVaapiEncoderStatus
 */
GstVaapiEncoderStatus
gst_vaapi_encoder_get_codec_data (GstVaapiEncoder * encoder,
    GstBuffer ** out_codec_data_ptr)
{
  GstVaapiEncoderStatus ret = GST_VAAPI_ENCODER_STATUS_SUCCESS;
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);

  *out_codec_data_ptr = NULL;
  if (!klass->get_codec_data)
    return GST_VAAPI_ENCODER_STATUS_SUCCESS;

  ret = klass->get_codec_data (encoder, out_codec_data_ptr);
  return ret;
}

/* Ensures the underlying VA context for encoding is created */
static gboolean
gst_vaapi_encoder_ensure_context (GstVaapiEncoder * encoder)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);
  GstVaapiContextInfo info;
  GstVaapiContext *context;

  if (GST_VAAPI_ENCODER_CONTEXT (encoder))
    return TRUE;

  memset (&info, 0, sizeof (info));
  if (!klass->get_context_info (encoder, &info))
    return FALSE;

  context = gst_vaapi_context_new_full (GST_VAAPI_ENCODER_DISPLAY (encoder),
      &info);
  if (!context)
    return FALSE;

  GST_VAAPI_ENCODER_CONTEXT (encoder) = context;
  GST_VAAPI_ENCODER_VA_CONTEXT (encoder) = gst_vaapi_context_get_id (context);
  return TRUE;
}

/**
 * gst_vaapi_encoder_set_format:
 * @encoder: a #GstVaapiEncoder
 * @state : a #GstVideoCodecState
 * @ref_caps: the set of reference caps (from pad template)
 *
 * Notifies the encoder of incoming data format (video resolution),
 * and additional information like framerate.
 *
 * Return value: the newly allocated set of caps
 */
GstCaps *
gst_vaapi_encoder_set_format (GstVaapiEncoder * encoder,
    GstVideoCodecState * state, GstCaps * ref_caps)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);
  GstCaps *out_caps = NULL;

  if (!GST_VIDEO_INFO_WIDTH (&state->info) ||
      !GST_VIDEO_INFO_HEIGHT (&state->info)) {
    GST_WARNING ("encoder set format failed, width or height equal to 0.");
    return NULL;
  }
  GST_VAAPI_ENCODER_VIDEO_INFO (encoder) = state->info;

  out_caps = klass->set_format (encoder, state, ref_caps);
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

  encoder->codedbuf_size = (GST_VAAPI_ENCODER_WIDTH (encoder) *
      GST_VAAPI_ENCODER_HEIGHT (encoder) * 400) / (16 * 16);

  encoder->codedbuf_pool = gst_vaapi_coded_buffer_pool_new (encoder,
      encoder->codedbuf_size);
  if (!encoder->codedbuf_pool) {
    GST_ERROR ("failed to initialized coded buffer pool");
    goto error;
  }
  gst_vaapi_video_pool_set_capacity (encoder->codedbuf_pool, 5);

  return out_caps;

error:
  gst_caps_replace (&GST_VAAPI_ENCODER_CAPS (encoder), NULL);
  gst_caps_replace (&out_caps, NULL);
  GST_ERROR ("encoder set format failed");
  return NULL;
}

/* Base encoder initialization (internal) */
static gboolean
gst_vaapi_encoder_init (GstVaapiEncoder * encoder, GstVaapiDisplay * display)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);

  g_return_val_if_fail (display != NULL, FALSE);

#define CHECK_VTABLE_HOOK(FUNC) do {            \
    if (!klass->FUNC)                           \
      goto error_invalid_vtable;                \
  } while (0)

  CHECK_VTABLE_HOOK (init);
  CHECK_VTABLE_HOOK (finalize);
  CHECK_VTABLE_HOOK (encode);
  CHECK_VTABLE_HOOK (reordering);
  CHECK_VTABLE_HOOK (flush);
  CHECK_VTABLE_HOOK (get_context_info);
  CHECK_VTABLE_HOOK (set_format);

#undef CHECK_VTABLE_HOOK

  encoder->display = gst_vaapi_display_ref (display);
  encoder->va_display = gst_vaapi_display_get_display (display);
  encoder->va_context = VA_INVALID_ID;

  gst_video_info_init (&encoder->video_info);

  g_mutex_init (&encoder->mutex);
  g_cond_init (&encoder->surface_free);
  g_cond_init (&encoder->codedbuf_free);

  encoder->codedbuf_queue = g_async_queue_new_full ((GDestroyNotify)
      gst_vaapi_coded_buffer_proxy_unref);
  if (!encoder->codedbuf_queue)
    return FALSE;

  return klass->init (encoder);

  /* ERRORS */
error_invalid_vtable:
  {
    GST_ERROR ("invalid subclass hook (internal error)");
    return FALSE;
  }
}

/* Base encoder cleanup (internal) */
void
gst_vaapi_encoder_finalize (GstVaapiEncoder * encoder)
{
  GstVaapiEncoderClass *const klass = GST_VAAPI_ENCODER_GET_CLASS (encoder);

  klass->finalize (encoder);

  gst_vaapi_object_replace (&encoder->context, NULL);
  gst_vaapi_display_replace (&encoder->display, NULL);
  encoder->va_display = NULL;

  gst_vaapi_video_pool_replace (&encoder->codedbuf_pool, NULL);
  if (encoder->codedbuf_queue) {
    g_async_queue_unref (encoder->codedbuf_queue);
    encoder->codedbuf_queue = NULL;
  }
  g_cond_clear (&encoder->surface_free);
  g_cond_clear (&encoder->codedbuf_free);
  g_mutex_clear (&encoder->mutex);
}

/* Helper function to create new GstVaapiEncoder instances (internal) */
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
