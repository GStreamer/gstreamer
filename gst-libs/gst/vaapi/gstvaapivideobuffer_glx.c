/*
 *  gstvaapivideobuffer_glx.c - Gst VA video buffer
 *
 *  Copyright (C) 2011 Intel Corporation
 *  Copyright (C) 2011 Collabora Ltd.
 *    Author: Nicolas Dufresne <nicolas.dufresne@collabora.co.uk>
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

/**
 * SECTION:gstvaapivideobufferglx
 * @short_description: VA video buffer for GStreamer with GLX support
 */

#include "sysdeps.h"
#include "gstvaapivideobuffer_glx.h"
#include "gstvaapivideoconverter_glx.h"
#include "gstvaapivideopool.h"
#include "gstvaapivideobuffer_priv.h"
#include "gstvaapidisplay_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiVideoBufferGLX,
              gst_vaapi_video_buffer_glx,
              GST_VAAPI_TYPE_VIDEO_BUFFER);

static void
gst_vaapi_video_buffer_glx_class_init(GstVaapiVideoBufferGLXClass *klass)
{
    GstSurfaceBufferClass * const surface_class =
        GST_SURFACE_BUFFER_CLASS(klass);

    surface_class->create_converter = gst_vaapi_video_converter_glx_new;
}

static void
gst_vaapi_video_buffer_glx_init(GstVaapiVideoBufferGLX *buffer)
{
}

/**
 * gst_vaapi_video_buffer_glx_new:
 * @display: a #GstVaapiDisplayGLX
 *
 * Creates an empty #GstBuffer. The caller is responsible for completing
 * the initialization of the buffer with the gst_vaapi_video_buffer_set_*()
 * functions.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL or error
 */
GstBuffer *
gst_vaapi_video_buffer_glx_new(GstVaapiDisplayGLX *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_GLX(display), NULL);

    return gst_vaapi_video_buffer_typed_new(
        GST_VAAPI_TYPE_VIDEO_BUFFER_GLX, GST_VAAPI_DISPLAY_CAST(display));
}

/**
 * gst_vaapi_video_buffer_glx_new_from_pool:
 * @pool: a #GstVaapiVideoPool
 *
 * Creates a #GstBuffer with a video object allocated from a @pool.
 * Only #GstVaapiSurfacePool and #GstVaapiImagePool pools are supported.
 *
 * The buffer is destroyed through the last call to gst_buffer_unref()
 * and the video objects are pushed back to their respective pools.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL on error
 */
GstBuffer *
gst_vaapi_video_buffer_glx_new_from_pool(GstVaapiVideoPool *pool)
{
    return gst_vaapi_video_buffer_typed_new_from_pool(
        GST_VAAPI_TYPE_VIDEO_BUFFER_GLX, pool);
}

/**
 * gst_vaapi_video_buffer_glx_new_from_buffer:
 * @buffer: a #GstBuffer
 *
 * Creates a #GstBuffer with video objects bound to @buffer video
 * objects, if any.
 *
 * Return value: the newly allocated #GstBuffer, or %NULL on error
 */
GstBuffer *
gst_vaapi_video_buffer_glx_new_from_buffer(GstBuffer *buffer)
{
    return gst_vaapi_video_buffer_typed_new_from_buffer(
        GST_VAAPI_TYPE_VIDEO_BUFFER_GLX, buffer);
}
