/*
 *  gstvaapicodedbufferpool.c - VA coded buffer pool
 *
 *  Copyright (C) 2013-2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
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
#include "gstvaapicodedbufferpool.h"
#include "gstvaapicodedbuffer_priv.h"
#include "gstvaapivideopool_priv.h"
#include "gstvaapiencoder_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

/**
 * GstVaapiCodedBufferPool:
 *
 * A pool of lazily allocated #GstVaapiCodedBuffer objects.
 */
struct _GstVaapiCodedBufferPool
{
  /*< private > */
  GstVaapiVideoPool parent_instance;

  GstVaapiContext *context;
  gsize buf_size;
};

static void
coded_buffer_pool_init (GstVaapiCodedBufferPool * pool,
    GstVaapiContext * context, gsize buf_size)
{
  pool->context = gst_vaapi_context_ref (context);
  pool->buf_size = buf_size;
}

static void
coded_buffer_pool_finalize (GstVaapiCodedBufferPool * pool)
{
  gst_vaapi_video_pool_finalize (GST_VAAPI_VIDEO_POOL (pool));
  gst_vaapi_context_unref (pool->context);
  pool->context = NULL;
}

static gpointer
coded_buffer_pool_alloc_object (GstVaapiVideoPool * base_pool)
{
  GstVaapiCodedBufferPool *const pool = GST_VAAPI_CODED_BUFFER_POOL (base_pool);

  return gst_vaapi_coded_buffer_new (pool->context, pool->buf_size);
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_coded_buffer_pool_class (void)
{
  static const GstVaapiVideoPoolClass GstVaapiCodedBufferPoolClass = {
    {sizeof (GstVaapiCodedBufferPool),
        (GDestroyNotify) coded_buffer_pool_finalize}
    ,
    .alloc_object = coded_buffer_pool_alloc_object
  };
  return GST_VAAPI_MINI_OBJECT_CLASS (&GstVaapiCodedBufferPoolClass);
}

/**
 * gst_vaapi_coded_buffer_pool_new:
 * @encoder: a #GstVaapiEncoder
 * @buf_size: the max size of #GstVaapiCodedBuffer objects, in bytes
 *
 * Creates a new #GstVaapiVideoPool of #GstVaapiCodedBuffer objects
 * with the supplied maximum size in bytes, and bound to the specified
 * @encoder object.
 *
 * Return value: the newly allocated #GstVaapiVideoPool
 */
GstVaapiVideoPool *
gst_vaapi_coded_buffer_pool_new (GstVaapiEncoder * encoder, gsize buf_size)
{
  GstVaapiVideoPool *pool;
  GstVaapiContext *context;

  g_return_val_if_fail (encoder != NULL, NULL);
  g_return_val_if_fail (buf_size > 0, NULL);

  context = GST_VAAPI_ENCODER_CONTEXT (encoder);
  g_return_val_if_fail (context != NULL, NULL);

  pool = (GstVaapiVideoPool *)
      gst_vaapi_mini_object_new (gst_vaapi_coded_buffer_pool_class ());
  if (!pool)
    return NULL;

  gst_vaapi_video_pool_init (pool, GST_VAAPI_CONTEXT_DISPLAY (context),
      GST_VAAPI_VIDEO_POOL_OBJECT_TYPE_CODED_BUFFER);
  coded_buffer_pool_init (GST_VAAPI_CODED_BUFFER_POOL (pool),
      context, buf_size);
  return pool;
}

/**
 * gst_vaapi_coded_buffer_pool_get_buffer_size:
 * @pool: a #GstVaapiCodedBufferPool
 *
 * Determines the maximum size of each #GstVaapiCodedBuffer held in
 * the @pool.
 *
 * Return value: size of a #GstVaapiCodedBuffer in @pool
 */
gsize
gst_vaapi_coded_buffer_pool_get_buffer_size (GstVaapiCodedBufferPool * pool)
{
  g_return_val_if_fail (pool != NULL, 0);

  return pool->buf_size;
}
