/*
 * GStreamer EGL/GLES Sink
 * Copyright (C) 2012 Collabora Ltd.
 *   @author: Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>
 *   @author: Sebastian Dröge <sebastian.droege@collabora.co.uk>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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

/**
 * SECTION:element-eglglessink
 *
 * EglGlesSink renders video frames on a EGL surface it sets up
 * from a window it either creates (on X11) or gets a handle to
 * through it's xOverlay interface. All the display/surface logic
 * in this sink uses EGL to interact with the native window system.
 * The rendering logic, in turn, uses OpenGL ES v2.
 *
 * This sink has been tested to work on X11/Mesa and on Android
 * (From Gingerbread on to Jelly Bean) and while it's currently
 * using an slow copy-over rendering path it has proven to be fast
 * enough on the devices we have tried it on. 
 *
 * <refsect2>
 * <title>Supported EGL/OpenGL ES versions</title>
 * <para>
 * This Sink uses EGLv1 and GLESv2
 * </para>
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink
 * ]|
 * </refsect2>
 *
 * <refsect2>
 * <title>Example launch line with internal window creation disabled</title>
 * <para>
 * By setting the can_create_window property to FALSE you can force the
 * sink to wait for a window handle through it's xOverlay interface even
 * if internal window creation is supported by the platform. Window creation
 * is only supported in X11 right now but it should be trivial to add support
 * for different platforms.
 * </para>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink can_create_window=FALSE
 * ]|
 * </refsect2>
 *
 * <refsect2>
 * <title>Scaling</title>
 * <para>
 * The sink will try it's best to consider the incoming frame's and display's
 * pixel aspect ratio and fill the corresponding surface without altering the
 * decoded frame's geometry when scaling. You can disable this logic by setting
 * the force_aspect_ratio property to FALSE, in which case the sink will just
 * fill the entire surface it has access to regardles of the PAR/DAR relationship.
 * </para>
 * <para>
 * Querying the display aspect ratio is only supported with EGL versions >= 1.2.
 * The sink will just assume the DAR to be 1/1 if it can't get access to this
 * information.
 * </para>
 * <para>
 * Here is an example launch line with the PAR/DAR aware scaling disabled:
 * </para>
 * |[
 * gst-launch -v -m videotestsrc ! eglglessink force_aspect_ratio=FALSE
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#define EGL_EGLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES

#include <string.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/video/video-frame.h>
#include <gst/video/gstvideosink.h>
#include <gst/video/gstvideometa.h>
#include <gst/video/gstvideopool.h>
#include <gst/video/videooverlay.h>

#include "gstegladaptation.h"

#ifdef USE_EGL_RPI
#include <bcm_host.h>
#endif

#include "gsteglglessink.h"

GST_DEBUG_CATEGORY_STATIC (gst_eglglessink_debug);
#define GST_CAT_DEFAULT gst_eglglessink_debug

GST_DEBUG_CATEGORY_EXTERN (GST_CAT_PERFORMANCE);

/* Input capabilities. */
static GstStaticPadTemplate gst_eglglessink_sink_template_factory =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_EGL_IMAGE,
            "{ " "RGBA, BGRA, ARGB, ABGR, " "RGBx, BGRx, xRGB, xBGR, "
            "AYUV, Y444, I420, YV12, " "NV12, NV21, Y42B, Y41B, "
            "RGB, BGR, RGB16 }") ";"
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META,
            "{ " "RGBA, BGRA, ARGB, ABGR, " "RGBx, BGRx, xRGB, xBGR, "
            "AYUV, Y444, I420, YV12, " "NV12, NV21, Y42B, Y41B, "
            "RGB, BGR, RGB16 }") ";" GST_VIDEO_CAPS_MAKE ("{ "
            "RGBA, BGRA, ARGB, ABGR, " "RGBx, BGRx, xRGB, xBGR, "
            "AYUV, Y444, I420, YV12, " "NV12, NV21, Y42B, Y41B, "
            "RGB, BGR, RGB16 }")));

/* Filter signals and args */
enum
{
  /* FILL ME */
  LAST_SIGNAL
};

enum
{
  PROP_0,
  PROP_CREATE_WINDOW,
  PROP_FORCE_ASPECT_RATIO,
};

static void gst_eglglessink_finalize (GObject * object);
static void gst_eglglessink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_eglglessink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static GstStateChangeReturn gst_eglglessink_change_state (GstElement * element,
    GstStateChange transition);
static void gst_eglglessink_set_context (GstElement * element,
    GstContext * context);
static GstFlowReturn gst_eglglessink_prepare (GstBaseSink * bsink,
    GstBuffer * buf);
static GstFlowReturn gst_eglglessink_show_frame (GstVideoSink * vsink,
    GstBuffer * buf);
static gboolean gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps *gst_eglglessink_getcaps (GstBaseSink * bsink, GstCaps * filter);
static gboolean gst_eglglessink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);
static gboolean gst_eglglessink_query (GstBaseSink * bsink, GstQuery * query);

/* VideoOverlay interface cruft */
static void gst_eglglessink_videooverlay_init (GstVideoOverlayInterface *
    iface);

/* Actual VideoOverlay interface funcs */
static void gst_eglglessink_expose (GstVideoOverlay * overlay);
static void gst_eglglessink_set_window_handle (GstVideoOverlay * overlay,
    guintptr id);
static void gst_eglglessink_set_render_rectangle (GstVideoOverlay * overlay,
    gint x, gint y, gint width, gint height);

/* Utility */
static gboolean gst_eglglessink_create_window (GstEglGlesSink *
    eglglessink, gint width, gint height);
static gboolean gst_eglglessink_setup_vbo (GstEglGlesSink * eglglessink);
static gboolean
gst_eglglessink_configure_caps (GstEglGlesSink * eglglessink, GstCaps * caps);
static GstFlowReturn gst_eglglessink_upload (GstEglGlesSink * sink,
    GstBuffer * buf);
static GstFlowReturn gst_eglglessink_render (GstEglGlesSink * sink);
static GstFlowReturn gst_eglglessink_queue_object (GstEglGlesSink * sink,
    GstMiniObject * obj);
static inline gboolean egl_init (GstEglGlesSink * eglglessink);

typedef GstBuffer *(*GstEGLImageBufferPoolSendBlockingAllocate) (GstBufferPool *
    pool, gpointer data);

/* EGLImage memory, buffer pool, etc */
typedef struct
{
  GstVideoBufferPool parent;

  GstAllocator *allocator;
  GstAllocationParams params;
  GstVideoInfo info;
  gboolean add_metavideo;
  gboolean want_eglimage;
  GstBuffer *last_buffer;
  GstEGLImageBufferPoolSendBlockingAllocate send_blocking_allocate_func;
  gpointer send_blocking_allocate_data;
  GDestroyNotify send_blocking_allocate_destroy;
} GstEGLImageBufferPool;

typedef GstVideoBufferPoolClass GstEGLImageBufferPoolClass;

#define GST_EGL_IMAGE_BUFFER_POOL(p) ((GstEGLImageBufferPool*)(p))

GType gst_egl_image_buffer_pool_get_type (void);

G_DEFINE_TYPE (GstEGLImageBufferPool, gst_egl_image_buffer_pool,
    GST_TYPE_VIDEO_BUFFER_POOL);

static GstBufferPool
    * gst_egl_image_buffer_pool_new (GstEGLImageBufferPoolSendBlockingAllocate
    blocking_allocate_func, gpointer blocking_allocate_data,
    GDestroyNotify destroy_func);

static void
gst_egl_image_buffer_pool_get_video_infos (GstEGLImageBufferPool * pool,
    GstVideoFormat * format, gint * width, gint * height)
{
  g_return_if_fail (pool != NULL);

  if (format)
    *format = pool->info.finfo->format;

  if (width)
    *width = pool->info.width;

  if (height)
    *height = pool->info.height;
}

static void
gst_egl_image_buffer_pool_replace_last_buffer (GstEGLImageBufferPool * pool,
    GstBuffer * buffer)
{
  g_return_if_fail (pool != NULL);

  gst_buffer_replace (&pool->last_buffer, buffer);
}

static GstBuffer *
gst_eglglessink_egl_image_buffer_pool_send_blocking (GstBufferPool * bpool,
    gpointer data)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstQuery *query = NULL;
  GstStructure *s = NULL;
  const GValue *v = NULL;
  GstBuffer *buffer = NULL;
  GstVideoFormat format = GST_VIDEO_FORMAT_UNKNOWN;
  gint width = 0;
  gint height = 0;

  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (data);

  gst_egl_image_buffer_pool_get_video_infos (pool, &format, &width, &height);

  s = gst_structure_new ("eglglessink-allocate-eglimage",
      "format", GST_TYPE_VIDEO_FORMAT, format,
      "width", G_TYPE_INT, width, "height", G_TYPE_INT, height, NULL);
  query = gst_query_new_custom (GST_QUERY_CUSTOM, s);

  ret =
      gst_eglglessink_queue_object (eglglessink, GST_MINI_OBJECT_CAST (query));

  if (ret == GST_FLOW_OK && gst_structure_has_field (s, "buffer")) {
    v = gst_structure_get_value (s, "buffer");
    buffer = GST_BUFFER_CAST (g_value_get_pointer (v));
  }

  gst_query_unref (query);

  return buffer;
}

static void
gst_eglglessink_egl_image_buffer_pool_on_destroy (gpointer data)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (data);
  gst_object_unref (eglglessink);
}

static const gchar **
gst_egl_image_buffer_pool_get_options (GstBufferPool * bpool)
{
  static const gchar *options[] = { GST_BUFFER_POOL_OPTION_VIDEO_META, NULL
  };

  return options;
}

static gboolean
gst_egl_image_buffer_pool_set_config (GstBufferPool * bpool,
    GstStructure * config)
{
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);
  GstCaps *caps;
  GstVideoInfo info;

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  if (!GST_BUFFER_POOL_CLASS
      (gst_egl_image_buffer_pool_parent_class)->set_config (bpool, config))
    return FALSE;

  if (!gst_buffer_pool_config_get_params (config, &caps, NULL, NULL, NULL)
      || !caps)
    return FALSE;

  if (!gst_video_info_from_caps (&info, caps))
    return FALSE;

  if (!gst_buffer_pool_config_get_allocator (config, &pool->allocator,
          &pool->params))
    return FALSE;
  if (pool->allocator)
    gst_object_ref (pool->allocator);

  pool->add_metavideo =
      gst_buffer_pool_config_has_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_META);

  pool->want_eglimage = (pool->allocator
      && g_strcmp0 (pool->allocator->mem_type, GST_EGL_IMAGE_MEMORY_TYPE) == 0);

  pool->info = info;

  return TRUE;
}

static GstFlowReturn
gst_egl_image_buffer_pool_alloc_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);

  *buffer = NULL;

  if (!pool->add_metavideo || !pool->want_eglimage)
    return
        GST_BUFFER_POOL_CLASS
        (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
        buffer, params);

  if (!pool->allocator)
    return GST_FLOW_NOT_NEGOTIATED;

  switch (pool->info.finfo->format) {
    case GST_VIDEO_FORMAT_RGB:
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB16:
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:
    case GST_VIDEO_FORMAT_AYUV:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{

      if (pool->send_blocking_allocate_func)
        *buffer = pool->send_blocking_allocate_func (bpool,
            pool->send_blocking_allocate_data);

      if (!*buffer) {
        GST_WARNING ("Fallback memory allocation");
        return
            GST_BUFFER_POOL_CLASS
            (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
            buffer, params);
      }

      return GST_FLOW_OK;
      break;
    }
    default:
      return
          GST_BUFFER_POOL_CLASS
          (gst_egl_image_buffer_pool_parent_class)->alloc_buffer (bpool,
          buffer, params);
      break;
  }

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_egl_image_buffer_pool_acquire_buffer (GstBufferPool * bpool,
    GstBuffer ** buffer, GstBufferPoolAcquireParams * params)
{
  GstFlowReturn ret;
  GstEGLImageBufferPool *pool;

  ret =
      GST_BUFFER_POOL_CLASS
      (gst_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
      buffer, params);
  if (ret != GST_FLOW_OK || !*buffer)
    return ret;

  pool = GST_EGL_IMAGE_BUFFER_POOL (bpool);

  /* XXX: Don't return the memory we just rendered, glEGLImageTargetTexture2DOES()
   * keeps the EGLImage unmappable until the next one is uploaded
   */
  if (*buffer && *buffer == pool->last_buffer) {
    GstBuffer *oldbuf = *buffer;

    ret =
        GST_BUFFER_POOL_CLASS
        (gst_egl_image_buffer_pool_parent_class)->acquire_buffer (bpool,
        buffer, params);
    gst_object_replace ((GstObject **) & oldbuf->pool, (GstObject *) pool);
    gst_buffer_unref (oldbuf);
  }

  return ret;
}

static void
gst_egl_image_buffer_pool_finalize (GObject * object)
{
  GstEGLImageBufferPool *pool = GST_EGL_IMAGE_BUFFER_POOL (object);

  if (pool->allocator)
    gst_object_unref (pool->allocator);
  pool->allocator = NULL;

  gst_egl_image_buffer_pool_replace_last_buffer (pool, NULL);

  if (pool->send_blocking_allocate_destroy)
    pool->send_blocking_allocate_destroy (pool->send_blocking_allocate_data);
  pool->send_blocking_allocate_destroy = NULL;
  pool->send_blocking_allocate_data = NULL;

  G_OBJECT_CLASS (gst_egl_image_buffer_pool_parent_class)->finalize (object);
}

static void
gst_egl_image_buffer_pool_class_init (GstEGLImageBufferPoolClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstBufferPoolClass *gstbufferpool_class = (GstBufferPoolClass *) klass;

  gobject_class->finalize = gst_egl_image_buffer_pool_finalize;
  gstbufferpool_class->get_options = gst_egl_image_buffer_pool_get_options;
  gstbufferpool_class->set_config = gst_egl_image_buffer_pool_set_config;
  gstbufferpool_class->alloc_buffer = gst_egl_image_buffer_pool_alloc_buffer;
  gstbufferpool_class->acquire_buffer =
      gst_egl_image_buffer_pool_acquire_buffer;
}

static void
gst_egl_image_buffer_pool_init (GstEGLImageBufferPool * pool)
{
}

#define parent_class gst_eglglessink_parent_class
G_DEFINE_TYPE_WITH_CODE (GstEglGlesSink, gst_eglglessink, GST_TYPE_VIDEO_SINK,
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_eglglessink_videooverlay_init));

static inline gboolean
egl_init (GstEglGlesSink * eglglessink)
{
  GstCaps *caps;

  if (!gst_egl_adaptation_init_egl_display (eglglessink->egl_context)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't init EGL display");
    goto HANDLE_ERROR;
  }

  caps =
      gst_egl_adaptation_fill_supported_fbuffer_configs
      (eglglessink->egl_context);
  if (!caps) {
    GST_ERROR_OBJECT (eglglessink, "Display support NONE of our configs");
    goto HANDLE_ERROR;
  } else {
    GST_OBJECT_LOCK (eglglessink);
    gst_caps_replace (&eglglessink->sinkcaps, caps);
    GST_OBJECT_UNLOCK (eglglessink);
    gst_caps_unref (caps);
  }

  eglglessink->egl_started = TRUE;

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Failed to perform EGL init");
  return FALSE;
}

static gpointer
render_thread_func (GstEglGlesSink * eglglessink)
{
  GstMessage *message;
  GValue val = { 0 };
  GstDataQueueItem *item = NULL;
  GstFlowReturn last_flow = GST_FLOW_OK;

  g_value_init (&val, GST_TYPE_G_THREAD);
  g_value_set_boxed (&val, g_thread_self ());
  message = gst_message_new_stream_status (GST_OBJECT_CAST (eglglessink),
      GST_STREAM_STATUS_TYPE_ENTER, GST_ELEMENT_CAST (eglglessink));
  gst_message_set_stream_status_object (message, &val);
  GST_DEBUG_OBJECT (eglglessink, "posting ENTER stream status");
  gst_element_post_message (GST_ELEMENT_CAST (eglglessink), message);
  g_value_unset (&val);

  eglBindAPI (EGL_OPENGL_ES_API);

  while (gst_data_queue_pop (eglglessink->queue, &item)) {
    GstMiniObject *object = item->object;

    GST_DEBUG_OBJECT (eglglessink, "Handling object %" GST_PTR_FORMAT, object);

    if (GST_IS_CAPS (object)) {
      GstCaps *caps = GST_CAPS_CAST (object);

      if (caps != eglglessink->configured_caps) {
        if (!gst_eglglessink_configure_caps (eglglessink, caps)) {
          last_flow = GST_FLOW_NOT_NEGOTIATED;
        }
      }
    } else if (GST_IS_QUERY (object)) {
      GstQuery *query = GST_QUERY_CAST (object);
      GstStructure *s = (GstStructure *) gst_query_get_structure (query);

      if (gst_structure_has_name (s, "eglglessink-allocate-eglimage")) {
        GstBuffer *buffer;
        GstVideoFormat format;
        gint width, height;
        GValue v = { 0, };

        if (!gst_structure_get_enum (s, "format", GST_TYPE_VIDEO_FORMAT,
                (gint *) & format)
            || !gst_structure_get_int (s, "width", &width)
            || !gst_structure_get_int (s, "height", &height)) {
          g_assert_not_reached ();
        }

        buffer =
            gst_egl_image_allocator_alloc_eglimage (GST_EGL_IMAGE_BUFFER_POOL
            (eglglessink->pool)->allocator, eglglessink->egl_context->display,
            gst_egl_adaptation_context_get_egl_context
            (eglglessink->egl_context), format, width, height);
        g_value_init (&v, G_TYPE_POINTER);
        g_value_set_pointer (&v, buffer);
        gst_structure_set_value (s, "buffer", &v);
        g_value_unset (&v);
      } else {
        g_assert_not_reached ();
      }
      last_flow = GST_FLOW_OK;
    } else if (GST_IS_BUFFER (object)) {
      GstBuffer *buf = GST_BUFFER_CAST (item->object);

      if (eglglessink->configured_caps) {
        last_flow = gst_eglglessink_upload (eglglessink, buf);
      } else {
        last_flow = GST_FLOW_OK;
        GST_DEBUG_OBJECT (eglglessink,
            "No caps configured yet, not drawing anything");
      }
    } else if (!object) {
      if (eglglessink->configured_caps) {
        last_flow = gst_eglglessink_render (eglglessink);
      } else {
        last_flow = GST_FLOW_OK;
        GST_DEBUG_OBJECT (eglglessink,
            "No caps configured yet, not drawing anything");
      }
    } else {
      g_assert_not_reached ();
    }

    item->destroy (item);
    g_mutex_lock (&eglglessink->render_lock);
    eglglessink->last_flow = last_flow;
    eglglessink->dequeued_object = object;
    g_cond_broadcast (&eglglessink->render_cond);
    g_mutex_unlock (&eglglessink->render_lock);

    if (last_flow != GST_FLOW_OK)
      break;
    GST_DEBUG_OBJECT (eglglessink, "Successfully handled object");
  }

  if (last_flow == GST_FLOW_OK) {
    g_mutex_lock (&eglglessink->render_lock);
    eglglessink->last_flow = GST_FLOW_FLUSHING;
    eglglessink->dequeued_object = NULL;
    g_cond_broadcast (&eglglessink->render_cond);
    g_mutex_unlock (&eglglessink->render_lock);
  }

  GST_DEBUG_OBJECT (eglglessink, "Shutting down thread");

  /* EGL/GLES cleanup */
  gst_egl_adaptation_cleanup (eglglessink->egl_context);

  if (eglglessink->configured_caps) {
    gst_caps_unref (eglglessink->configured_caps);
    eglglessink->configured_caps = NULL;
  }

  g_value_init (&val, GST_TYPE_G_THREAD);
  g_value_set_boxed (&val, g_thread_self ());
  message = gst_message_new_stream_status (GST_OBJECT_CAST (eglglessink),
      GST_STREAM_STATUS_TYPE_LEAVE, GST_ELEMENT_CAST (eglglessink));
  gst_message_set_stream_status_object (message, &val);
  GST_DEBUG_OBJECT (eglglessink, "posting LEAVE stream status");
  gst_element_post_message (GST_ELEMENT_CAST (eglglessink), message);
  g_value_unset (&val);

  return NULL;
}

static gboolean
gst_eglglessink_start (GstEglGlesSink * eglglessink)
{
  GError *error = NULL;

  GST_DEBUG_OBJECT (eglglessink, "Starting");

  if (!eglglessink->egl_started) {
    GST_ERROR_OBJECT (eglglessink, "EGL uninitialized. Bailing out");
    goto HANDLE_ERROR;
  }

  /* Ask for a window to render to */
  if (!eglglessink->have_window)
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (eglglessink));

  if (!eglglessink->have_window && !eglglessink->create_window) {
    GST_ERROR_OBJECT (eglglessink, "Window handle unavailable and we "
        "were instructed not to create an internal one. Bailing out.");
    goto HANDLE_ERROR;
  }

  eglglessink->last_flow = GST_FLOW_OK;
  eglglessink->display_region.w = 0;
  eglglessink->display_region.h = 0;

  gst_data_queue_set_flushing (eglglessink->queue, FALSE);

#if !GLIB_CHECK_VERSION (2, 31, 0)
  eglglessink->thread =
      g_thread_create ((GThreadFunc) render_thread_func, eglglessink, TRUE,
      &error);
#else
  eglglessink->thread = g_thread_try_new ("eglglessink-render",
      (GThreadFunc) render_thread_func, eglglessink, &error);
#endif

  if (!eglglessink->thread || error != NULL)
    goto HANDLE_ERROR;

  GST_DEBUG_OBJECT (eglglessink, "Started");

  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Couldn't start");
  g_clear_error (&error);
  return FALSE;
}

static gboolean
gst_eglglessink_stop (GstEglGlesSink * eglglessink)
{
  GST_DEBUG_OBJECT (eglglessink, "Stopping");

  gst_data_queue_set_flushing (eglglessink->queue, TRUE);
  g_mutex_lock (&eglglessink->render_lock);
  g_cond_broadcast (&eglglessink->render_cond);
  g_mutex_unlock (&eglglessink->render_lock);

  if (eglglessink->thread) {
    g_thread_join (eglglessink->thread);
    eglglessink->thread = NULL;
  }
  eglglessink->last_flow = GST_FLOW_FLUSHING;

  if (eglglessink->pool)
    gst_egl_image_buffer_pool_replace_last_buffer (GST_EGL_IMAGE_BUFFER_POOL
        (eglglessink->pool), NULL);

  if (eglglessink->using_own_window) {
    gst_egl_adaptation_destroy_native_window (eglglessink->egl_context,
        &eglglessink->own_window_data);
    eglglessink->have_window = FALSE;
  }
  eglglessink->egl_context->used_window = 0;
  if (eglglessink->current_caps) {
    gst_caps_unref (eglglessink->current_caps);
    eglglessink->current_caps = NULL;
  }

  GST_DEBUG_OBJECT (eglglessink, "Stopped");

  return TRUE;
}

static void
gst_eglglessink_videooverlay_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_eglglessink_set_window_handle;
  iface->expose = gst_eglglessink_expose;
  iface->set_render_rectangle = gst_eglglessink_set_render_rectangle;
}

static gboolean
gst_eglglessink_create_window (GstEglGlesSink * eglglessink, gint width,
    gint height)
{
  gboolean window_created = FALSE;

  if (!eglglessink->create_window) {
    GST_ERROR_OBJECT (eglglessink, "This sink can't create a window by itself");
    return FALSE;
  } else
    GST_INFO_OBJECT (eglglessink, "Attempting internal window creation");

  window_created =
      gst_egl_adaptation_create_native_window (eglglessink->egl_context, width,
      height, &eglglessink->own_window_data);
  if (!window_created) {
    GST_ERROR_OBJECT (eglglessink, "Could not create window");
  }
  return window_created;
}

static void
gst_eglglessink_expose (GstVideoOverlay * overlay)
{
  GstEglGlesSink *eglglessink;
  GstFlowReturn ret;

  eglglessink = GST_EGLGLESSINK (overlay);
  GST_DEBUG_OBJECT (eglglessink, "Expose catched, redisplay");

  /* Render from last seen buffer */
  ret = gst_eglglessink_queue_object (eglglessink, NULL);
  if (ret == GST_FLOW_ERROR)
    GST_ERROR_OBJECT (eglglessink, "Redisplay failed");
}

static gboolean
gst_eglglessink_setup_vbo (GstEglGlesSink * eglglessink)
{
  gdouble render_width, render_height;
  gdouble texture_width, texture_height;
  gdouble x1, x2, y1, y2;
  gdouble tx1, tx2, ty1, ty2;

  GST_INFO_OBJECT (eglglessink, "VBO setup. have_vbo:%d",
      eglglessink->egl_context->have_vbo);

  if (eglglessink->egl_context->have_vbo) {
    glDeleteBuffers (1, &eglglessink->egl_context->position_buffer);
    glDeleteBuffers (1, &eglglessink->egl_context->index_buffer);
    eglglessink->egl_context->have_vbo = FALSE;
  }

  render_width = eglglessink->render_region.w;
  render_height = eglglessink->render_region.h;

  texture_width = eglglessink->configured_info.width;
  texture_height = eglglessink->configured_info.height;

  GST_DEBUG_OBJECT (eglglessink, "Performing VBO setup");

  x1 = (eglglessink->display_region.x / render_width) * 2.0 - 1;
  y1 = (eglglessink->display_region.y / render_height) * 2.0 - 1;
  x2 = ((eglglessink->display_region.x +
          eglglessink->display_region.w) / render_width) * 2.0 - 1;
  y2 = ((eglglessink->display_region.y +
          eglglessink->display_region.h) / render_height) * 2.0 - 1;

  tx1 = (eglglessink->crop.x / texture_width);
  tx2 = ((eglglessink->crop.x + eglglessink->crop.w) / texture_width);
  ty1 = (eglglessink->crop.y / texture_height);
  ty2 = ((eglglessink->crop.y + eglglessink->crop.h) / texture_height);

  /* X-normal, Y-normal orientation */
  eglglessink->egl_context->position_array[0].x = x2;
  eglglessink->egl_context->position_array[0].y = y2;
  eglglessink->egl_context->position_array[0].z = 0;
  eglglessink->egl_context->position_array[0].a = tx2;
  eglglessink->egl_context->position_array[0].b = ty1;

  eglglessink->egl_context->position_array[1].x = x2;
  eglglessink->egl_context->position_array[1].y = y1;
  eglglessink->egl_context->position_array[1].z = 0;
  eglglessink->egl_context->position_array[1].a = tx2;
  eglglessink->egl_context->position_array[1].b = ty2;

  eglglessink->egl_context->position_array[2].x = x1;
  eglglessink->egl_context->position_array[2].y = y2;
  eglglessink->egl_context->position_array[2].z = 0;
  eglglessink->egl_context->position_array[2].a = tx1;
  eglglessink->egl_context->position_array[2].b = ty1;

  eglglessink->egl_context->position_array[3].x = x1;
  eglglessink->egl_context->position_array[3].y = y1;
  eglglessink->egl_context->position_array[3].z = 0;
  eglglessink->egl_context->position_array[3].a = tx1;
  eglglessink->egl_context->position_array[3].b = ty2;

  /* X-normal, Y-flip orientation */
  eglglessink->egl_context->position_array[4 + 0].x = x2;
  eglglessink->egl_context->position_array[4 + 0].y = y2;
  eglglessink->egl_context->position_array[4 + 0].z = 0;
  eglglessink->egl_context->position_array[4 + 0].a = tx2;
  eglglessink->egl_context->position_array[4 + 0].b = ty2;

  eglglessink->egl_context->position_array[4 + 1].x = x2;
  eglglessink->egl_context->position_array[4 + 1].y = y1;
  eglglessink->egl_context->position_array[4 + 1].z = 0;
  eglglessink->egl_context->position_array[4 + 1].a = tx2;
  eglglessink->egl_context->position_array[4 + 1].b = ty1;

  eglglessink->egl_context->position_array[4 + 2].x = x1;
  eglglessink->egl_context->position_array[4 + 2].y = y2;
  eglglessink->egl_context->position_array[4 + 2].z = 0;
  eglglessink->egl_context->position_array[4 + 2].a = tx1;
  eglglessink->egl_context->position_array[4 + 2].b = ty2;

  eglglessink->egl_context->position_array[4 + 3].x = x1;
  eglglessink->egl_context->position_array[4 + 3].y = y1;
  eglglessink->egl_context->position_array[4 + 3].z = 0;
  eglglessink->egl_context->position_array[4 + 3].a = tx1;
  eglglessink->egl_context->position_array[4 + 3].b = ty1;


  if (eglglessink->display_region.x == 0) {
    /* Borders top/bottom */

    eglglessink->egl_context->position_array[8 + 0].x = 1;
    eglglessink->egl_context->position_array[8 + 0].y = 1;
    eglglessink->egl_context->position_array[8 + 0].z = 0;

    eglglessink->egl_context->position_array[8 + 1].x = x2;
    eglglessink->egl_context->position_array[8 + 1].y = y2;
    eglglessink->egl_context->position_array[8 + 1].z = 0;

    eglglessink->egl_context->position_array[8 + 2].x = -1;
    eglglessink->egl_context->position_array[8 + 2].y = 1;
    eglglessink->egl_context->position_array[8 + 2].z = 0;

    eglglessink->egl_context->position_array[8 + 3].x = x1;
    eglglessink->egl_context->position_array[8 + 3].y = y2;
    eglglessink->egl_context->position_array[8 + 3].z = 0;

    eglglessink->egl_context->position_array[12 + 0].x = 1;
    eglglessink->egl_context->position_array[12 + 0].y = y1;
    eglglessink->egl_context->position_array[12 + 0].z = 0;

    eglglessink->egl_context->position_array[12 + 1].x = 1;
    eglglessink->egl_context->position_array[12 + 1].y = -1;
    eglglessink->egl_context->position_array[12 + 1].z = 0;

    eglglessink->egl_context->position_array[12 + 2].x = x1;
    eglglessink->egl_context->position_array[12 + 2].y = y1;
    eglglessink->egl_context->position_array[12 + 2].z = 0;

    eglglessink->egl_context->position_array[12 + 3].x = -1;
    eglglessink->egl_context->position_array[12 + 3].y = -1;
    eglglessink->egl_context->position_array[12 + 3].z = 0;
  } else {
    /* Borders left/right */

    eglglessink->egl_context->position_array[8 + 0].x = x1;
    eglglessink->egl_context->position_array[8 + 0].y = 1;
    eglglessink->egl_context->position_array[8 + 0].z = 0;

    eglglessink->egl_context->position_array[8 + 1].x = x1;
    eglglessink->egl_context->position_array[8 + 1].y = -1;
    eglglessink->egl_context->position_array[8 + 1].z = 0;

    eglglessink->egl_context->position_array[8 + 2].x = -1;
    eglglessink->egl_context->position_array[8 + 2].y = 1;
    eglglessink->egl_context->position_array[8 + 2].z = 0;

    eglglessink->egl_context->position_array[8 + 3].x = -1;
    eglglessink->egl_context->position_array[8 + 3].y = -1;
    eglglessink->egl_context->position_array[8 + 3].z = 0;

    eglglessink->egl_context->position_array[12 + 0].x = 1;
    eglglessink->egl_context->position_array[12 + 0].y = 1;
    eglglessink->egl_context->position_array[12 + 0].z = 0;

    eglglessink->egl_context->position_array[12 + 1].x = 1;
    eglglessink->egl_context->position_array[12 + 1].y = -1;
    eglglessink->egl_context->position_array[12 + 1].z = 0;

    eglglessink->egl_context->position_array[12 + 2].x = x2;
    eglglessink->egl_context->position_array[12 + 2].y = y2;
    eglglessink->egl_context->position_array[12 + 2].z = 0;

    eglglessink->egl_context->position_array[12 + 3].x = x2;
    eglglessink->egl_context->position_array[12 + 3].y = -1;
    eglglessink->egl_context->position_array[12 + 3].z = 0;
  }

  eglglessink->egl_context->index_array[0] = 0;
  eglglessink->egl_context->index_array[1] = 1;
  eglglessink->egl_context->index_array[2] = 2;
  eglglessink->egl_context->index_array[3] = 3;

  glGenBuffers (1, &eglglessink->egl_context->position_buffer);
  glGenBuffers (1, &eglglessink->egl_context->index_buffer);
  if (got_gl_error ("glGenBuffers"))
    goto HANDLE_ERROR_LOCKED;

  glBindBuffer (GL_ARRAY_BUFFER, eglglessink->egl_context->position_buffer);
  if (got_gl_error ("glBindBuffer position_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBufferData (GL_ARRAY_BUFFER,
      sizeof (eglglessink->egl_context->position_array),
      eglglessink->egl_context->position_array, GL_STATIC_DRAW);
  if (got_gl_error ("glBufferData position_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBindBuffer (GL_ELEMENT_ARRAY_BUFFER,
      eglglessink->egl_context->index_buffer);
  if (got_gl_error ("glBindBuffer index_buffer"))
    goto HANDLE_ERROR_LOCKED;

  glBufferData (GL_ELEMENT_ARRAY_BUFFER,
      sizeof (eglglessink->egl_context->index_array),
      eglglessink->egl_context->index_array, GL_STATIC_DRAW);
  if (got_gl_error ("glBufferData index_buffer"))
    goto HANDLE_ERROR_LOCKED;

  eglglessink->egl_context->have_vbo = TRUE;

  GST_DEBUG_OBJECT (eglglessink, "VBO setup done");

  return TRUE;

HANDLE_ERROR_LOCKED:
  GST_ERROR_OBJECT (eglglessink, "Unable to perform VBO setup");
  return FALSE;
}

static void
gst_eglglessink_set_window_handle (GstVideoOverlay * overlay, guintptr id)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (overlay);

  g_return_if_fail (GST_IS_EGLGLESSINK (eglglessink));
  GST_DEBUG_OBJECT (eglglessink, "We got a window handle: %p", (gpointer) id);

  /* OK, we have a new window */
  GST_OBJECT_LOCK (eglglessink);
  eglglessink->egl_context->window = (EGLNativeWindowType) id;
  eglglessink->have_window = ((gpointer) id != NULL);
  GST_OBJECT_UNLOCK (eglglessink);

  return;
}

static void
gst_eglglessink_set_render_rectangle (GstVideoOverlay * overlay, gint x, gint y,
    gint width, gint height)
{
  GstEglGlesSink *eglglessink = GST_EGLGLESSINK (overlay);

  g_return_if_fail (GST_IS_EGLGLESSINK (eglglessink));

  GST_OBJECT_LOCK (eglglessink);
  eglglessink->render_region.x = x;
  eglglessink->render_region.y = y;
  eglglessink->render_region.w = width;
  eglglessink->render_region.h = height;
  eglglessink->render_region_changed = TRUE;
  eglglessink->render_region_user = (width != -1 && height != -1);
  GST_OBJECT_UNLOCK (eglglessink);

  return;
}

static void
queue_item_destroy (GstDataQueueItem * item)
{
  if (item->object && !GST_IS_QUERY (item->object))
    gst_mini_object_unref (item->object);
  g_slice_free (GstDataQueueItem, item);
}

static GstFlowReturn
gst_eglglessink_queue_object (GstEglGlesSink * eglglessink, GstMiniObject * obj)
{
  GstDataQueueItem *item;
  GstFlowReturn last_flow;

  g_mutex_lock (&eglglessink->render_lock);
  last_flow = eglglessink->last_flow;
  g_mutex_unlock (&eglglessink->render_lock);

  if (last_flow != GST_FLOW_OK)
    return last_flow;

  item = g_slice_new0 (GstDataQueueItem);

  if (obj == NULL)
    item->object = NULL;
  else if (GST_IS_QUERY (obj))
    item->object = obj;
  else
    item->object = gst_mini_object_ref (obj);
  item->size = 0;
  item->duration = GST_CLOCK_TIME_NONE;
  item->visible = TRUE;
  item->destroy = (GDestroyNotify) queue_item_destroy;

  GST_DEBUG_OBJECT (eglglessink, "Queueing object %" GST_PTR_FORMAT, obj);

  g_mutex_lock (&eglglessink->render_lock);
  if (!gst_data_queue_push (eglglessink->queue, item)) {
    item->destroy (item);
    g_mutex_unlock (&eglglessink->render_lock);
    GST_DEBUG_OBJECT (eglglessink, "Flushing");
    return GST_FLOW_FLUSHING;
  }

  GST_DEBUG_OBJECT (eglglessink, "Waiting for object to be handled");
  do {
    g_cond_wait (&eglglessink->render_cond, &eglglessink->render_lock);
  } while (eglglessink->dequeued_object != obj
      && eglglessink->last_flow != GST_FLOW_FLUSHING);
  GST_DEBUG_OBJECT (eglglessink, "Object handled: %s",
      gst_flow_get_name (eglglessink->last_flow));
  last_flow = eglglessink->last_flow;
  g_mutex_unlock (&eglglessink->render_lock);

  return (obj ? last_flow : GST_FLOW_OK);
}

static gboolean
gst_eglglessink_crop_changed (GstEglGlesSink * eglglessink,
    GstVideoCropMeta * crop)
{
  if (crop) {
    return (crop->x != eglglessink->crop.x ||
        crop->y != eglglessink->crop.y ||
        crop->width != eglglessink->crop.w ||
        crop->height != eglglessink->crop.h);
  }

  return (eglglessink->crop.x != 0 || eglglessink->crop.y != 0 ||
      eglglessink->crop.w != eglglessink->configured_info.width ||
      eglglessink->crop.h != eglglessink->configured_info.height);
}

static gboolean
gst_eglglessink_fill_texture (GstEglGlesSink * eglglessink, GstBuffer * buf)
{
  GstVideoFrame vframe;
#ifndef GST_DISABLE_GST_DEBUG
  gint w;
#endif
  gint h;

  memset (&vframe, 0, sizeof (vframe));

  if (!gst_video_frame_map (&vframe, &eglglessink->configured_info, buf,
          GST_MAP_READ)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't map frame");
    goto HANDLE_ERROR;
  }
#ifndef GST_DISABLE_GST_DEBUG
  w = GST_VIDEO_FRAME_WIDTH (&vframe);
#endif
  h = GST_VIDEO_FRAME_HEIGHT (&vframe);

  GST_DEBUG_OBJECT (eglglessink,
      "Got buffer %p: %dx%d size %" G_GSIZE_FORMAT, buf, w, h,
      gst_buffer_get_size (buf));

  switch (eglglessink->configured_info.finfo->format) {
    case GST_VIDEO_FORMAT_BGR:
    case GST_VIDEO_FORMAT_RGB:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w * 3) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w * 3 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width * 3) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width * 3 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, stride_width, h, 0, GL_RGB,
          GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_RGB16:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (c_w * 2 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (stride_width * 2 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGB, stride_width, h, 0, GL_RGB,
          GL_UNSIGNED_SHORT_5_6_5, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_RGBA:
    case GST_VIDEO_FORMAT_BGRA:
    case GST_VIDEO_FORMAT_ARGB:
    case GST_VIDEO_FORMAT_ABGR:
    case GST_VIDEO_FORMAT_RGBx:
    case GST_VIDEO_FORMAT_BGRx:
    case GST_VIDEO_FORMAT_xRGB:
    case GST_VIDEO_FORMAT_xBGR:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 4) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (c_w * 4 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (stride_width * 4 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, stride_width, h, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_AYUV:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_WIDTH (&vframe);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w * 4) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (c_w * 4 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width * 4) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (stride_width * 4 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, stride_width, h, 0,
          GL_RGBA, GL_UNSIGNED_BYTE, GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));
      break;
    }
    case GST_VIDEO_FORMAT_Y444:
    case GST_VIDEO_FORMAT_I420:
    case GST_VIDEO_FORMAT_YV12:
    case GST_VIDEO_FORMAT_Y42B:
    case GST_VIDEO_FORMAT_Y41B:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 0),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 0));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 1);

      glActiveTexture (GL_TEXTURE1);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[1] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 1),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 1));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 2);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 2);

      glActiveTexture (GL_TEXTURE2);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[2] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[2]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 2),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_COMP_DATA (&vframe, 2));
      break;
    }
    case GST_VIDEO_FORMAT_NV12:
    case GST_VIDEO_FORMAT_NV21:{
      gint stride;
      gint stride_width;
      gint c_w;

      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 0);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 0);

      glActiveTexture (GL_TEXTURE0);

      if (GST_ROUND_UP_8 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (GST_ROUND_UP_2 (c_w) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else if (c_w == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
      } else {
        stride_width = stride;

        if (GST_ROUND_UP_8 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (GST_ROUND_UP_2 (stride_width) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else if (stride_width == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[0] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[0]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 0),
          0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_PLANE_DATA (&vframe, 0));


      stride = GST_VIDEO_FRAME_PLANE_STRIDE (&vframe, 1);
      stride_width = c_w = GST_VIDEO_FRAME_COMP_WIDTH (&vframe, 1);

      glActiveTexture (GL_TEXTURE1);

      if (GST_ROUND_UP_8 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
      } else if (GST_ROUND_UP_4 (c_w * 2) == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
      } else if (c_w * 2 == stride) {
        glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
      } else {
        stride_width = stride / 2;

        if (GST_ROUND_UP_8 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 8);
        } else if (GST_ROUND_UP_4 (stride_width * 2) == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        } else if (stride_width * 2 == stride) {
          glPixelStorei (GL_UNPACK_ALIGNMENT, 2);
        } else {
          GST_ERROR_OBJECT (eglglessink, "Unsupported stride %d", stride);
          goto HANDLE_ERROR;
        }
      }
      if (got_gl_error ("glPixelStorei"))
        goto HANDLE_ERROR;

      eglglessink->stride[1] = ((gdouble) stride_width) / ((gdouble) c_w);

      glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[1]);
      glTexImage2D (GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA,
          stride_width,
          GST_VIDEO_FRAME_COMP_HEIGHT (&vframe, 1),
          0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE,
          GST_VIDEO_FRAME_PLANE_DATA (&vframe, 1));
      break;
    }
    default:
      g_assert_not_reached ();
      break;
  }

  if (got_gl_error ("glTexImage2D"))
    goto HANDLE_ERROR;

  gst_video_frame_unmap (&vframe);

  return TRUE;

HANDLE_ERROR:
  {
    if (vframe.buffer)
      gst_video_frame_unmap (&vframe);
    return FALSE;
  }
}

/* Rendering and display */
static GstFlowReturn
gst_eglglessink_upload (GstEglGlesSink * eglglessink, GstBuffer * buf)
{
  GstVideoCropMeta *crop = NULL;

  if (!buf) {
    GST_DEBUG_OBJECT (eglglessink, "Rendering previous buffer again");
  } else if (buf) {
    GstMemory *mem;
    GstVideoGLTextureUploadMeta *upload_meta;

    crop = gst_buffer_get_video_crop_meta (buf);

    upload_meta = gst_buffer_get_video_gl_texture_upload_meta (buf);

    if (gst_eglglessink_crop_changed (eglglessink, crop)) {
      if (crop) {
        eglglessink->crop.x = crop->x;
        eglglessink->crop.y = crop->y;
        eglglessink->crop.w = crop->width;
        eglglessink->crop.h = crop->height;
      } else {
        eglglessink->crop.x = 0;
        eglglessink->crop.y = 0;
        eglglessink->crop.w = eglglessink->configured_info.width;
        eglglessink->crop.h = eglglessink->configured_info.height;
      }
      eglglessink->crop_changed = TRUE;
    }

    if (upload_meta) {
      gint i;

      if (upload_meta->n_textures != eglglessink->egl_context->n_textures)
        goto HANDLE_ERROR;

      for (i = 0; i < eglglessink->egl_context->n_textures; i++) {
        if (i == 0)
          glActiveTexture (GL_TEXTURE0);
        else if (i == 1)
          glActiveTexture (GL_TEXTURE1);
        else if (i == 2)
          glActiveTexture (GL_TEXTURE2);

        glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[i]);
      }

      if (!gst_video_gl_texture_upload_meta_upload (upload_meta,
              eglglessink->egl_context->texture))
        goto HANDLE_ERROR;

      eglglessink->orientation = upload_meta->texture_orientation;
      eglglessink->stride[0] = 1;
      eglglessink->stride[1] = 1;
      eglglessink->stride[2] = 1;
    } else if (gst_buffer_n_memory (buf) >= 1 &&
        (mem = gst_buffer_peek_memory (buf, 0))
        && gst_is_egl_image_memory (mem)) {
      guint n, i;

      n = gst_buffer_n_memory (buf);

      for (i = 0; i < n; i++) {
        mem = gst_buffer_peek_memory (buf, i);

        g_assert (gst_is_egl_image_memory (mem));

        if (i == 0)
          glActiveTexture (GL_TEXTURE0);
        else if (i == 1)
          glActiveTexture (GL_TEXTURE1);
        else if (i == 2)
          glActiveTexture (GL_TEXTURE2);

        glBindTexture (GL_TEXTURE_2D, eglglessink->egl_context->texture[i]);
        glEGLImageTargetTexture2DOES (GL_TEXTURE_2D,
            gst_egl_image_memory_get_image (mem));
        if (got_gl_error ("glEGLImageTargetTexture2DOES"))
          goto HANDLE_ERROR;
        eglglessink->orientation = gst_egl_image_memory_get_orientation (mem);
        if (eglglessink->orientation !=
            GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL
            && eglglessink->orientation !=
            GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP) {
          GST_ERROR_OBJECT (eglglessink, "Unsupported EGLImage orientation");
          return GST_FLOW_ERROR;
        }
      }
      if (eglglessink->pool)
        gst_egl_image_buffer_pool_replace_last_buffer (GST_EGL_IMAGE_BUFFER_POOL
            (eglglessink->pool), buf);
      eglglessink->stride[0] = 1;
      eglglessink->stride[1] = 1;
      eglglessink->stride[2] = 1;
    } else {
      eglglessink->orientation =
          GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL;
      if (!gst_eglglessink_fill_texture (eglglessink, buf))
        goto HANDLE_ERROR;
    }
  }

  return GST_FLOW_OK;

HANDLE_ERROR:
  {
    GST_ERROR_OBJECT (eglglessink, "Failed to upload texture");
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_eglglessink_render (GstEglGlesSink * eglglessink)
{
  guint dar_n, dar_d;
  gint i;

  /* If no one has set a display rectangle on us initialize
   * a sane default. According to the docs on the xOverlay
   * interface we are supposed to fill the overlay 100%. We
   * do this trying to take PAR/DAR into account unless the
   * calling party explicitly ask us not to by setting
   * force_aspect_ratio to FALSE.
   */
  if (gst_egl_adaptation_update_surface_dimensions (eglglessink->egl_context) ||
      eglglessink->render_region_changed ||
      !eglglessink->display_region.w || !eglglessink->display_region.h ||
      eglglessink->crop_changed) {
    GST_OBJECT_LOCK (eglglessink);

    if (!eglglessink->render_region_user) {
      eglglessink->render_region.x = 0;
      eglglessink->render_region.y = 0;
      eglglessink->render_region.w = eglglessink->egl_context->surface_width;
      eglglessink->render_region.h = eglglessink->egl_context->surface_height;
    }
    eglglessink->render_region_changed = FALSE;
    eglglessink->crop_changed = FALSE;

    if (!eglglessink->force_aspect_ratio) {
      eglglessink->display_region.x = 0;
      eglglessink->display_region.y = 0;
      eglglessink->display_region.w = eglglessink->render_region.w;
      eglglessink->display_region.h = eglglessink->render_region.h;
    } else {
      GstVideoRectangle frame;

      frame.x = 0;
      frame.y = 0;

      if (!gst_video_calculate_display_ratio (&dar_n, &dar_d,
              eglglessink->crop.w, eglglessink->crop.h,
              eglglessink->configured_info.par_n,
              eglglessink->configured_info.par_d,
              eglglessink->egl_context->pixel_aspect_ratio_n,
              eglglessink->egl_context->pixel_aspect_ratio_d)) {
        GST_WARNING_OBJECT (eglglessink, "Could not compute resulting DAR");
        frame.w = eglglessink->crop.w;
        frame.h = eglglessink->crop.h;
      } else {
        /* Find suitable matching new size acording to dar & par
         * rationale for prefering leaving the height untouched
         * comes from interlacing considerations.
         * XXX: Move this to gstutils?
         */
        if (eglglessink->crop.h % dar_d == 0) {
          frame.w =
              gst_util_uint64_scale_int (eglglessink->crop.h, dar_n, dar_d);
          frame.h = eglglessink->crop.h;
        } else if (eglglessink->crop.w % dar_n == 0) {
          frame.h =
              gst_util_uint64_scale_int (eglglessink->crop.w, dar_d, dar_n);
          frame.w = eglglessink->crop.w;
        } else {
          /* Neither width nor height can be precisely scaled.
           * Prefer to leave height untouched. See comment above.
           */
          frame.w =
              gst_util_uint64_scale_int (eglglessink->crop.h, dar_n, dar_d);
          frame.h = eglglessink->crop.h;
        }
      }

      gst_video_sink_center_rect (frame, eglglessink->render_region,
          &eglglessink->display_region, TRUE);
    }

    glViewport (eglglessink->render_region.x,
        eglglessink->egl_context->surface_height -
        eglglessink->render_region.y -
        eglglessink->render_region.h,
        eglglessink->render_region.w, eglglessink->render_region.h);

    /* Clear the surface once if its content is preserved */
    if (eglglessink->egl_context->buffer_preserved) {
      glClearColor (0.0, 0.0, 0.0, 1.0);
      glClear (GL_COLOR_BUFFER_BIT);
    }

    if (!gst_eglglessink_setup_vbo (eglglessink)) {
      GST_OBJECT_UNLOCK (eglglessink);
      GST_ERROR_OBJECT (eglglessink, "VBO setup failed");
      goto HANDLE_ERROR;
    }
    GST_OBJECT_UNLOCK (eglglessink);
  }

  if (!eglglessink->egl_context->buffer_preserved) {
    /* Draw black borders */
    GST_DEBUG_OBJECT (eglglessink, "Drawing black border 1");
    glUseProgram (eglglessink->egl_context->glslprogram[1]);

    glVertexAttribPointer (eglglessink->egl_context->position_loc[1], 3,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (8 * sizeof (coord5)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;

    glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
    if (got_gl_error ("glDrawElements"))
      goto HANDLE_ERROR;

    GST_DEBUG_OBJECT (eglglessink, "Drawing black border 2");

    glVertexAttribPointer (eglglessink->egl_context->position_loc[1], 3,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (12 * sizeof (coord5)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;

    glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
    if (got_gl_error ("glDrawElements"))
      goto HANDLE_ERROR;
  }

  /* Draw video frame */
  GST_DEBUG_OBJECT (eglglessink, "Drawing video frame");

  glUseProgram (eglglessink->egl_context->glslprogram[0]);

  glUniform2f (eglglessink->egl_context->tex_scale_loc[0][0],
      eglglessink->stride[0], 1);
  glUniform2f (eglglessink->egl_context->tex_scale_loc[0][1],
      eglglessink->stride[1], 1);
  glUniform2f (eglglessink->egl_context->tex_scale_loc[0][2],
      eglglessink->stride[2], 1);

  for (i = 0; i < eglglessink->egl_context->n_textures; i++) {
    glUniform1i (eglglessink->egl_context->tex_loc[0][i], i);
    if (got_gl_error ("glUniform1i"))
      goto HANDLE_ERROR;
  }

  if (eglglessink->orientation ==
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_NORMAL) {
    glVertexAttribPointer (eglglessink->egl_context->position_loc[0], 3,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (0 * sizeof (coord5)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;

    glVertexAttribPointer (eglglessink->egl_context->texpos_loc[0], 2,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (3 * sizeof (gfloat)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;
  } else if (eglglessink->orientation ==
      GST_VIDEO_GL_TEXTURE_ORIENTATION_X_NORMAL_Y_FLIP) {
    glVertexAttribPointer (eglglessink->egl_context->position_loc[0], 3,
        GL_FLOAT, GL_FALSE, sizeof (coord5), (gpointer) (4 * sizeof (coord5)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;

    glVertexAttribPointer (eglglessink->egl_context->texpos_loc[0], 2,
        GL_FLOAT, GL_FALSE, sizeof (coord5),
        (gpointer) (4 * sizeof (coord5) + 3 * sizeof (gfloat)));
    if (got_gl_error ("glVertexAttribPointer"))
      goto HANDLE_ERROR;
  } else {
    g_assert_not_reached ();
  }

  glDrawElements (GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_SHORT, 0);
  if (got_gl_error ("glDrawElements"))
    goto HANDLE_ERROR;

  if (!gst_egl_adaptation_context_swap_buffers (eglglessink->egl_context)) {
    goto HANDLE_ERROR;
  }


  GST_DEBUG_OBJECT (eglglessink, "Succesfully rendered 1 frame");
  return GST_FLOW_OK;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Rendering disabled for this frame");

  return GST_FLOW_ERROR;
}

static GstFlowReturn
gst_eglglessink_prepare (GstBaseSink * bsink, GstBuffer * buf)
{
  GstEglGlesSink *eglglessink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  eglglessink = GST_EGLGLESSINK (bsink);
  GST_DEBUG_OBJECT (eglglessink, "Got buffer: %p", buf);

  return gst_eglglessink_queue_object (eglglessink, GST_MINI_OBJECT_CAST (buf));
}

static GstFlowReturn
gst_eglglessink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstEglGlesSink *eglglessink;

  g_return_val_if_fail (buf != NULL, GST_FLOW_ERROR);

  eglglessink = GST_EGLGLESSINK (vsink);
  GST_DEBUG_OBJECT (eglglessink, "Got buffer: %p", buf);

  return gst_eglglessink_queue_object (eglglessink, NULL);
}

static GstCaps *
gst_eglglessink_getcaps (GstBaseSink * bsink, GstCaps * filter)
{
  GstEglGlesSink *eglglessink;
  GstCaps *ret = NULL;

  eglglessink = GST_EGLGLESSINK (bsink);

  GST_OBJECT_LOCK (eglglessink);
  if (eglglessink->sinkcaps) {
    ret = gst_caps_ref (eglglessink->sinkcaps);
  } else {
    ret =
        gst_caps_copy (gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD
            (bsink)));
  }
  GST_OBJECT_UNLOCK (eglglessink);

  if (filter) {
    GstCaps *tmp =
        gst_caps_intersect_full (filter, ret, GST_CAPS_INTERSECT_FIRST);

    gst_caps_unref (ret);
    ret = tmp;
  }

  return ret;
}

static gboolean
gst_eglglessink_query (GstBaseSink * bsink, GstQuery * query)
{
  GstEglGlesSink *eglglessink;

  eglglessink = GST_EGLGLESSINK (bsink);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:{
      const gchar *context_type;

      if (gst_query_parse_context_type (query, &context_type) &&
          strcmp (context_type, GST_EGL_DISPLAY_CONTEXT_TYPE) &&
          eglglessink->egl_context->display) {
        GstContext *context;

        context =
            gst_context_new_egl_display (eglglessink->egl_context->display,
            FALSE);
        gst_query_set_context (query, context);
        gst_context_unref (context);

        return TRUE;
      } else {
        return GST_BASE_SINK_CLASS (gst_eglglessink_parent_class)->query (bsink,
            query);
      }
      break;
    }
    default:
      return GST_BASE_SINK_CLASS (gst_eglglessink_parent_class)->query (bsink,
          query);
      break;
  }
}

static void
gst_eglglessink_set_context (GstElement * element, GstContext * context)
{
  GstEglGlesSink *eglglessink;
  GstEGLDisplay *display = NULL;

  eglglessink = GST_EGLGLESSINK (element);

  if (gst_context_get_egl_display (context, &display)) {
    GST_OBJECT_LOCK (eglglessink);
    if (eglglessink->egl_context->set_display)
      gst_egl_display_unref (eglglessink->egl_context->set_display);
    eglglessink->egl_context->set_display = display;
    GST_OBJECT_UNLOCK (eglglessink);
  }
}

static gboolean
gst_eglglessink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstEglGlesSink *eglglessink;
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  GstVideoInfo info;
  gboolean need_pool;
  guint size;
  GstAllocator *allocator;
  GstAllocationParams params;

  eglglessink = GST_EGLGLESSINK (bsink);

  gst_allocation_params_init (&params);

  gst_query_parse_allocation (query, &caps, &need_pool);
  if (!caps) {
    GST_ERROR_OBJECT (eglglessink, "allocation query without caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (eglglessink, "allocation query with invalid caps");
    return FALSE;
  }

  GST_OBJECT_LOCK (eglglessink);
  pool = eglglessink->pool ? gst_object_ref (eglglessink->pool) : NULL;
  GST_OBJECT_UNLOCK (eglglessink);

  if (pool) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (eglglessink, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (eglglessink, "pool has different caps");
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  }

  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps)) {
      GST_ERROR_OBJECT (eglglessink, "allocation query has invalid caps %"
          GST_PTR_FORMAT, caps);
      return FALSE;
    }

    GST_DEBUG_OBJECT (eglglessink, "create new pool");
    pool =
        gst_egl_image_buffer_pool_new
        (gst_eglglessink_egl_image_buffer_pool_send_blocking,
        gst_object_ref (eglglessink),
        gst_eglglessink_egl_image_buffer_pool_on_destroy);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    /* we need at least 2 buffer because we hold on to the last one */
    gst_buffer_pool_config_set_params (config, caps, size, 2, 0);
    gst_buffer_pool_config_set_allocator (config, NULL, &params);
    if (!gst_buffer_pool_set_config (pool, config)) {
      gst_object_unref (pool);
      GST_ERROR_OBJECT (eglglessink, "failed to set pool configuration");
      return FALSE;
    }
  }

  if (pool) {
    /* we need at least 2 buffer because we hold on to the last one */
    gst_query_add_allocation_pool (query, pool, size, 2, 0);
    gst_object_unref (pool);
  }

  /* First the default allocator */
  if (!gst_egl_image_memory_is_mappable ()) {
    allocator = gst_allocator_find (NULL);
    gst_query_add_allocation_param (query, allocator, &params);
    gst_object_unref (allocator);
  }

  allocator = gst_egl_image_allocator_obtain ();
  if (!gst_egl_image_memory_is_mappable ())
    params.flags |= GST_MEMORY_FLAG_NOT_MAPPABLE;
  gst_query_add_allocation_param (query, allocator, &params);
  gst_object_unref (allocator);

  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, NULL);

  return TRUE;
}

static gboolean
gst_eglglessink_configure_caps (GstEglGlesSink * eglglessink, GstCaps * caps)
{
  gboolean ret = TRUE;
  GstVideoInfo info;

  gst_video_info_init (&info);
  if (!(ret = gst_video_info_from_caps (&info, caps))) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't parse caps");
    goto HANDLE_ERROR;
  }

  eglglessink->configured_info = info;
  GST_VIDEO_SINK_WIDTH (eglglessink) = info.width;
  GST_VIDEO_SINK_HEIGHT (eglglessink) = info.height;

  if (eglglessink->configured_caps) {
    GST_DEBUG_OBJECT (eglglessink, "Caps were already set");
    if (gst_caps_can_intersect (caps, eglglessink->configured_caps)) {
      GST_DEBUG_OBJECT (eglglessink, "Caps are compatible anyway");
      goto SUCCEED;
    }

    GST_DEBUG_OBJECT (eglglessink, "Caps are not compatible, reconfiguring");

    /* EGL/GLES cleanup */
    gst_egl_adaptation_cleanup (eglglessink->egl_context);

    gst_caps_unref (eglglessink->configured_caps);
    eglglessink->configured_caps = NULL;
  }

  if (!gst_egl_adaptation_choose_config (eglglessink->egl_context)) {
    GST_ERROR_OBJECT (eglglessink, "Couldn't choose EGL config");
    goto HANDLE_ERROR;
  }

  gst_caps_replace (&eglglessink->configured_caps, caps);

  /* By now the application should have set a window
   * if it meant to do so
   */
  GST_OBJECT_LOCK (eglglessink);
  if (!eglglessink->have_window) {

    GST_INFO_OBJECT (eglglessink,
        "No window. Will attempt internal window creation");
    if (!gst_eglglessink_create_window (eglglessink, info.width, info.height)) {
      GST_ERROR_OBJECT (eglglessink, "Internal window creation failed!");
      GST_OBJECT_UNLOCK (eglglessink);
      goto HANDLE_ERROR;
    }
    eglglessink->using_own_window = TRUE;
    eglglessink->have_window = TRUE;
  }
  GST_DEBUG_OBJECT (eglglessink, "Using window handle %p",
      (gpointer) eglglessink->egl_context->window);
  eglglessink->egl_context->used_window = eglglessink->egl_context->window;
  GST_OBJECT_UNLOCK (eglglessink);
  gst_video_overlay_got_window_handle (GST_VIDEO_OVERLAY (eglglessink),
      (guintptr) eglglessink->egl_context->used_window);

  if (!eglglessink->egl_context->have_surface) {
    if (!gst_egl_adaptation_init_egl_surface (eglglessink->egl_context,
            eglglessink->configured_info.finfo->format)) {
      GST_ERROR_OBJECT (eglglessink, "Couldn't init EGL surface from window");
      goto HANDLE_ERROR;
    }
  }

  gst_egl_adaptation_init_egl_exts (eglglessink->egl_context);

SUCCEED:
  GST_INFO_OBJECT (eglglessink, "Configured caps successfully");
  return TRUE;

HANDLE_ERROR:
  GST_ERROR_OBJECT (eglglessink, "Configuring caps failed");
  return FALSE;
}

static gboolean
gst_eglglessink_setcaps (GstBaseSink * bsink, GstCaps * caps)
{
  GstEglGlesSink *eglglessink;
  GstVideoInfo info;
  GstBufferPool *newpool, *oldpool;
  GstStructure *config;
  GstAllocationParams params = { 0, };

  eglglessink = GST_EGLGLESSINK (bsink);

  GST_DEBUG_OBJECT (eglglessink,
      "Current caps %" GST_PTR_FORMAT ", setting caps %"
      GST_PTR_FORMAT, eglglessink->current_caps, caps);

  if (gst_eglglessink_queue_object (eglglessink,
          GST_MINI_OBJECT_CAST (caps)) != GST_FLOW_OK) {
    GST_ERROR_OBJECT (eglglessink, "Failed to configure caps");
    return FALSE;
  }

  if (!gst_video_info_from_caps (&info, caps)) {
    GST_ERROR_OBJECT (eglglessink, "Invalid caps %" GST_PTR_FORMAT, caps);
    return FALSE;
  }

  newpool =
      gst_egl_image_buffer_pool_new
      (gst_eglglessink_egl_image_buffer_pool_send_blocking,
      gst_object_ref (eglglessink),
      gst_eglglessink_egl_image_buffer_pool_on_destroy);
  config = gst_buffer_pool_get_config (newpool);
  /* we need at least 2 buffer because we hold on to the last one */
  gst_buffer_pool_config_set_params (config, caps, info.size, 2, 0);
  gst_buffer_pool_config_set_allocator (config, NULL, &params);
  if (!gst_buffer_pool_set_config (newpool, config)) {
    gst_object_unref (newpool);
    GST_ERROR_OBJECT (eglglessink, "Failed to set buffer pool configuration");
    return FALSE;
  }

  GST_OBJECT_LOCK (eglglessink);
  oldpool = eglglessink->pool;
  eglglessink->pool = newpool;
  GST_OBJECT_UNLOCK (eglglessink);

  if (oldpool)
    gst_object_unref (oldpool);

  gst_caps_replace (&eglglessink->current_caps, caps);

  return TRUE;
}

static gboolean
gst_eglglessink_open (GstEglGlesSink * eglglessink)
{
  if (!egl_init (eglglessink)) {
    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_eglglessink_close (GstEglGlesSink * eglglessink)
{
  if (eglglessink->egl_context->display) {
    gst_egl_display_unref (eglglessink->egl_context->display);
    eglglessink->egl_context->display = NULL;
  }

  gst_caps_unref (eglglessink->sinkcaps);
  eglglessink->sinkcaps = NULL;
  eglglessink->egl_started = FALSE;

  GST_OBJECT_LOCK (eglglessink);
  if (eglglessink->pool)
    gst_object_unref (eglglessink->pool);
  eglglessink->pool = NULL;
  GST_OBJECT_UNLOCK (eglglessink);

  return TRUE;
}

static GstStateChangeReturn
gst_eglglessink_change_state (GstElement * element, GstStateChange transition)
{
  GstEglGlesSink *eglglessink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  eglglessink = GST_EGLGLESSINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_eglglessink_open (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!gst_eglglessink_start (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (!gst_eglglessink_close (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      if (!gst_eglglessink_stop (eglglessink)) {
        ret = GST_STATE_CHANGE_FAILURE;
        goto done;
      }
      break;
    default:
      break;
  }

done:
  return ret;
}

static void
gst_eglglessink_finalize (GObject * object)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  gst_egl_adaptation_context_free (eglglessink->egl_context);

  if (eglglessink->queue)
    g_object_unref (eglglessink->queue);
  eglglessink->queue = NULL;

  g_cond_clear (&eglglessink->render_cond);
  g_mutex_clear (&eglglessink->render_lock);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_eglglessink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  switch (prop_id) {
    case PROP_CREATE_WINDOW:
      eglglessink->create_window = g_value_get_boolean (value);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      eglglessink->force_aspect_ratio = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_eglglessink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstEglGlesSink *eglglessink;

  g_return_if_fail (GST_IS_EGLGLESSINK (object));

  eglglessink = GST_EGLGLESSINK (object);

  switch (prop_id) {
    case PROP_CREATE_WINDOW:
      g_value_set_boolean (value, eglglessink->create_window);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, eglglessink->force_aspect_ratio);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* initialize the eglglessink's class */
static void
gst_eglglessink_class_init (GstEglGlesSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;

  gobject_class->set_property = gst_eglglessink_set_property;
  gobject_class->get_property = gst_eglglessink_get_property;
  gobject_class->finalize = gst_eglglessink_finalize;

  gstelement_class->change_state = gst_eglglessink_change_state;
  gstelement_class->set_context = gst_eglglessink_set_context;

  gstbasesink_class->set_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_setcaps);
  gstbasesink_class->get_caps = GST_DEBUG_FUNCPTR (gst_eglglessink_getcaps);
  gstbasesink_class->propose_allocation =
      GST_DEBUG_FUNCPTR (gst_eglglessink_propose_allocation);
  gstbasesink_class->prepare = GST_DEBUG_FUNCPTR (gst_eglglessink_prepare);
  gstbasesink_class->query = GST_DEBUG_FUNCPTR (gst_eglglessink_query);

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_eglglessink_show_frame);

  g_object_class_install_property (gobject_class, PROP_CREATE_WINDOW,
      g_param_spec_boolean ("create-window", "Create Window",
          "If set to true, the sink will attempt to create it's own window to "
          "render to if none is provided. This is currently only supported "
          "when the sink is used under X11",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Respect aspect ratio when scaling",
          "If set to true, the sink will attempt to preserve the incoming "
          "frame's geometry while scaling, taking both the storage's and "
          "display's pixel aspect ratio into account",
          TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_static_metadata (gstelement_class,
      "EGL/GLES vout Sink",
      "Sink/Video",
      "An EGL/GLES Video Output Sink Implementing the VideoOverlay interface",
      "Reynaldo H. Verdejo Pinochet <reynaldo@collabora.com>, "
      "Sebastian Dröge <sebastian.droege@collabora.co.uk>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_eglglessink_sink_template_factory));
}

static gboolean
queue_check_full_func (GstDataQueue * queue, guint visible, guint bytes,
    guint64 time, gpointer checkdata)
{
  return visible != 0;
}

static void
gst_eglglessink_init (GstEglGlesSink * eglglessink)
{
  eglglessink->egl_context =
      gst_egl_adaptation_context_new (GST_ELEMENT_CAST (eglglessink));

  /* Init defaults */

  /** Flags */
  eglglessink->have_window = FALSE;
  eglglessink->egl_context->have_surface = FALSE;
  eglglessink->egl_context->have_vbo = FALSE;
  eglglessink->egl_context->have_texture = FALSE;
  eglglessink->egl_started = FALSE;
  eglglessink->using_own_window = FALSE;

  /** Props */
  eglglessink->create_window = TRUE;
  eglglessink->force_aspect_ratio = TRUE;

  g_mutex_init (&eglglessink->render_lock);
  g_cond_init (&eglglessink->render_cond);
  eglglessink->queue =
      gst_data_queue_new (queue_check_full_func, NULL, NULL, NULL);
  eglglessink->last_flow = GST_FLOW_FLUSHING;

  eglglessink->render_region.x = 0;
  eglglessink->render_region.y = 0;
  eglglessink->render_region.w = -1;
  eglglessink->render_region.h = -1;
  eglglessink->render_region_changed = TRUE;
  eglglessink->render_region_user = FALSE;
}

static GstBufferPool *
gst_egl_image_buffer_pool_new (GstEGLImageBufferPoolSendBlockingAllocate
    blocking_allocate_func, gpointer blocking_allocate_data,
    GDestroyNotify destroy_func)
{
  GstEGLImageBufferPool *pool;

  pool = g_object_new (gst_egl_image_buffer_pool_get_type (), NULL);
  pool->last_buffer = NULL;
  pool->send_blocking_allocate_func = blocking_allocate_func;
  pool->send_blocking_allocate_data = blocking_allocate_data;
  pool->send_blocking_allocate_destroy = destroy_func;

  return (GstBufferPool *) pool;
}

/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
eglglessink_plugin_init (GstPlugin * plugin)
{
  /* debug category for fltering log messages */
  GST_DEBUG_CATEGORY_INIT (gst_eglglessink_debug, "eglglessink",
      0, "Simple EGL/GLES Sink");

  gst_egl_adaption_init ();

#ifdef USE_EGL_RPI
  GST_DEBUG ("Initialize BCM host");
  bcm_host_init ();
#endif

  return gst_element_register (plugin, "eglglessink", GST_RANK_SECONDARY,
      GST_TYPE_EGLGLESSINK);
}

/* gstreamer looks for this structure to register eglglessinks */
GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    eglglessink,
    "EGL/GLES sink",
    eglglessink_plugin_init,
    VERSION, GST_LICENSE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN)
