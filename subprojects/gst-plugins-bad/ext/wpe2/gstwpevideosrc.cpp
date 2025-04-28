/* Copyright (C) <2018, 2025> Philippe Normand <philn@igalia.com>
 * Copyright (C) <2018> Žan Doberšek <zdobersek@igalia.com>
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
 * SECTION:element-wpevideosrc2
 * @title: wpevideosrc2
 *
 * The wpevideosrc2 element is used to produce a video texture representing a web page
 * rendered off-screen by WPE.
 *
 * Software rendering support is also available. This features allows
 * wpevideosrc2 to be used on machines without GPU, and/or for testing purpose.
 * To enable software rendering support, set the `LIBGL_ALWAYS_SOFTWARE=true`
 * environment variable and make sure `video/x-raw, format=BGRA` caps are
 * negotiated by the wpevideosrc2 element.
 *
 * As the webview loading is usually not instantaneous, the wpevideosrc2 element emits
 * messages indicating the load progress, in percent. The value is an estimate
 * based on the total number of bytes expected to be received for a document,
 * including all its possible subresources and child documents. The application
 * can handle these `element` messages synchronously for instance, in order to
 * display a progress bar or other visual load indicator. The load percent value
 * is stored in the message structure as a double value named
 * `estimated-load-progress` and the structure name is `wpe-stats`.
 *
 * ## Example launch lines
 *
 * ```shell
 * gst-launch-1.0 -v wpevideosrc2 location="https://gstreamer.freedesktop.org" ! queue ! glimagesink
 * ```
 * Shows the GStreamer website homepage
 *
 * ```shell
 * LIBGL_ALWAYS_SOFTWARE=true gst-launch-1.0 -v wpevideosrc2 num-buffers=50 location="https://gstreamer.freedesktop.org" \
 *   videoconvert ! pngenc ! multifilesink location=/tmp/snapshot-%05d.png
 * ```
 * Saves the first 50 video frames generated for the GStreamer website as PNG files in /tmp.
 *
 * ```shell
 * gst-play-1.0 --videosink gtkglsink web+https://gstreamer.freedesktop.org
 * ```
 * Shows the GStreamer website homepage as played with GstPlayer in a GTK+ window.
 *
 * ```shell
 * gst-launch-1.0  glvideomixer name=m sink_1::zorder=0 ! glimagesink wpevideosrc2 location="file:///tmp/asset.html" draw-background=0 \
 *   ! m. videotestsrc ! queue ! glupload ! glcolorconvert ! m.
 * ```
 * Composite WPE with a video stream in a single OpenGL scene.
 *
 * ```shell
 * gst-launch-1.0 glvideomixer name=m sink_1::zorder=0 sink_0::height=818 sink_0::width=1920 ! gtkglsink \
 *    wpevideosrc2 location="file:///tmp/asset.html" draw-background=0 ! m.
 *    uridecodebin uri="http://example.com/Sintel.2010.1080p.mkv" name=d d. ! queue ! glupload ! glcolorconvert ! m.
 * ```
 * Composite WPE with a video stream, sink_0 pad properties have to match the video dimensions.
 *
 * ```shell
 * weston -S $HOME/weston-sock -B headless-backend.so --use-gl &
 * WAYLAND_DISPLAY=$HOME/weston-sock gst-launch-1.0 wpevideosrc2 location=https://google.com ! queue ! fakevideosink
 * ```
 * Render Google.com with WPE in a headless Weston compositor. This can be useful for server-side WPE video processing.
 *
 * Since: 1.28
 */

/*
 * TODO:
 * - Better navigation events handling (would require a new GstNavigation API)
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "gstwpe.h"
#include "gstwpevideosrc.h"
#include <gst/gl/gl.h>
#include <gst/gl/egl/gstglmemoryegl.h>
#include <gst/gl/wayland/gstgldisplay_wayland.h>
#include <gst/video/video.h>
#include <xkbcommon/xkbcommon.h>

#include "gstwpethreadedview.h"
#include "gstwpedisplay.h"

#define DEFAULT_WIDTH 1920
#define DEFAULT_HEIGHT 1080
#define DEFAULT_FPS_N 30
#define DEFAULT_FPS_D 1
#define DEFAULT_DRAW_BACKGROUND TRUE

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_DRAW_BACKGROUND
};

enum
{
  SIGNAL_WPE_VIEW_CREATED,
  SIGNAL_CONFIGURE_WEB_VIEW,
  SIGNAL_LOAD_BYTES,
  SIGNAL_RUN_JAVASCRIPT,
  LAST_SIGNAL
};
static guint gst_wpe_video_src_signals[LAST_SIGNAL] = { 0 };

struct _GstWpeVideoSrc2
{
  GstGLBaseSrc parent;

  /* properties */
  gchar *location;
  gboolean draw_background;

  GBytes *bytes;
  gboolean gl_enabled;

  gint64 n_frames;              /* total frames sent */

  WPEDisplay *display;
  GstWPEThreadedView *view;

  GMutex lock;
};

#define WPE_LOCK(o) g_mutex_lock(&(o)->lock)
#define WPE_UNLOCK(o) g_mutex_unlock(&(o)->lock)

GST_DEBUG_CATEGORY_EXTERN (wpe_video_src_debug);
#define GST_CAT_DEFAULT wpe_video_src_debug

#define gst_wpe_video_src_parent_class parent_class
G_DEFINE_TYPE (GstWpeVideoSrc2, gst_wpe_video_src, GST_TYPE_GL_BASE_SRC);

#define WPE_RAW_CAPS "video/x-raw, "            \
  "format = (string) BGRA, "                    \
  "width = " GST_VIDEO_SIZE_RANGE ", "          \
  "height = " GST_VIDEO_SIZE_RANGE ", "         \
  "framerate = " GST_VIDEO_FPS_RANGE ", "       \
  "pixel-aspect-ratio = (fraction)1/1"

#define WPE_GL_CAPS "video/x-raw(memory:GLMemory), "    \
  "format = (string) RGBA, "                            \
  "width = " GST_VIDEO_SIZE_RANGE ", "                  \
  "height = " GST_VIDEO_SIZE_RANGE ", "                 \
  "framerate = " GST_VIDEO_FPS_RANGE ", "               \
  "pixel-aspect-ratio = (fraction)1/1, texture-target = (string)2D"

#define WPE_VIDEO_SRC_CAPS WPE_GL_CAPS "; " WPE_RAW_CAPS
#define WPE_VIDEO_SRC_DOC_CAPS WPE_GL_CAPS "; video/x-raw, format = (string) BGRA"

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (WPE_VIDEO_SRC_CAPS));

#define GST_ELEMENT_PROGRESS(el, type, code, text)                             \
  G_STMT_START {                                                               \
    gchar *__txt = _gst_element_error_printf text;                             \
    gst_element_post_message(                                                  \
        GST_ELEMENT_CAST(el),                                                  \
        gst_message_new_progress(GST_OBJECT_CAST(el),                          \
                                 GST_PROGRESS_TYPE_##type, code, __txt));      \
    g_free(__txt);                                                             \
  }                                                                            \
  G_STMT_END

static GstFlowReturn
gst_wpe_video_src_create (GstBaseSrc * bsrc, guint64 offset, guint length,
    GstBuffer ** buf)
{
  GstGLBaseSrc *gl_src = GST_GL_BASE_SRC (bsrc);
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (bsrc);
  GstFlowReturn ret = GST_FLOW_ERROR;
  GstBuffer *locked_buffer;
  GstClockTime next_time;
  gint64 ts_offset = 0;

  WPE_LOCK (src);
  if (src->gl_enabled) {
    WPE_UNLOCK (src);
    return GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_SRC_CLASS, create, (bsrc,
            offset, length, buf), ret);
  }

  locked_buffer = src->view->buffer ();
  if (locked_buffer == NULL) {
    WPE_UNLOCK (src);
    GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
        ("WPE View did not render a buffer"), (NULL));
    return ret;
  }
  *buf = gst_buffer_copy_deep (locked_buffer);

  g_object_get (gl_src, "timestamp-offset", &ts_offset, NULL);

  /* The following code mimics the behaviour of GLBaseSrc::fill */
  GST_BUFFER_TIMESTAMP (*buf) = ts_offset + gl_src->running_time;
  GST_BUFFER_OFFSET (*buf) = src->n_frames;
  src->n_frames++;
  GST_BUFFER_OFFSET_END (*buf) = src->n_frames;
  if (gl_src->out_info.fps_n) {
    next_time = gst_util_uint64_scale_int (src->n_frames * GST_SECOND,
        gl_src->out_info.fps_d, gl_src->out_info.fps_n);
    GST_BUFFER_DURATION (*buf) = next_time - gl_src->running_time;
  } else {
    next_time = ts_offset;
    GST_BUFFER_DURATION (*buf) = GST_CLOCK_TIME_NONE;
  }

  GST_LOG_OBJECT (src, "Created buffer from SHM %" GST_PTR_FORMAT, *buf);

  gl_src->running_time = next_time;

  ret = GST_FLOW_OK;
  WPE_UNLOCK (src);
  return ret;
}

static GQuark
_egl_image_quark (void)
{
  static GQuark quark = 0;

  if (!quark)
    quark = g_quark_from_static_string ("GstWPEEGLImage");
  return quark;
}

static gboolean
gst_wpe_video_src_fill_memory (GstGLBaseSrc * bsrc, GstGLMemory * memory)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (bsrc);
  const GstGLFuncs *gl;
  guint tex_id;
  GstEGLImage *locked_image;

  if (!gst_gl_context_check_feature (GST_GL_CONTEXT (bsrc->context),
          "EGL_KHR_image_base")) {
    GST_ERROR_OBJECT (src, "EGL_KHR_image_base is not supported");
    return FALSE;
  }

  WPE_LOCK (src);

  gl = bsrc->context->gl_vtable;
  tex_id = gst_gl_memory_get_texture_id (memory);
  locked_image = src->view->image ();

  if (!locked_image) {
    WPE_UNLOCK (src);
    return TRUE;
  }
  // The EGLImage is implicitely associated with the memory we're filling, so we
  // need to ensure their life cycles are tied.
  gst_mini_object_set_qdata (GST_MINI_OBJECT_CAST (memory), _egl_image_quark (),
      gst_egl_image_ref (locked_image), (GDestroyNotify) gst_egl_image_unref);

  gl->ActiveTexture (GL_TEXTURE0 + memory->plane);
  gl->BindTexture (GL_TEXTURE_2D, tex_id);
  gl->EGLImageTargetTexture2D (GL_TEXTURE_2D,
      gst_egl_image_get_image (locked_image));
  WPE_UNLOCK (src);
  return TRUE;
}

static gboolean
gst_wpe_video_src_start (GstWpeVideoSrc2 * src)
{
  GstGLContext *context = NULL;
  GstGLDisplay *display = NULL;
  GstGLBaseSrc *base_src = GST_GL_BASE_SRC (src);
  gboolean created_view = FALSE;
  GBytes *bytes;

  GST_ELEMENT_PROGRESS (src, START, "open", ("Starting up"));
  GST_INFO_OBJECT (src, "Starting up");
  WPE_LOCK (src);

  if (src->gl_enabled) {
    context = base_src->context;
    display = base_src->display;
  }

  GST_DEBUG_OBJECT (src, "Will %sfill GLMemories",
      src->gl_enabled ? "" : "NOT ");

  auto & thread = GstWPEContextThread::singleton ();

  if (!src->view) {
    GST_ELEMENT_PROGRESS (src, CONTINUE, "open", ("Creating WPE WebView"));

    GError *error = nullptr;
    wpe_display_gstreamer_set_gl (src->display, display, context);

    if (!wpe_display_connect (src->display, &error)) {
      WPE_UNLOCK (src);
      GST_ELEMENT_PROGRESS (src, ERROR, "open",
          ("WPE display initialisation failed"));
      GST_ELEMENT_ERROR (src, RESOURCE, FAILED,
          ("Display initialisation failed: %s", error->message), (NULL));
      g_error_free (error);
      return FALSE;
    }

    src->view =
        thread.createWPEView (src, context, display, src->display,
        GST_VIDEO_INFO_WIDTH (&base_src->out_info),
        GST_VIDEO_INFO_HEIGHT (&base_src->out_info));
    created_view = TRUE;
    GST_DEBUG_OBJECT (src, "created view %p", src->view);
    GST_ELEMENT_PROGRESS (src, CONTINUE, "open", ("WPE WebView is ready"));
  }

  if (!created_view) {
    GST_INFO_OBJECT (src,
        "Re-starting after re-negotiation, clearing cached SHM buffers");
    src->view->clearBuffers ();
  }

  GST_OBJECT_LOCK (src);
  bytes = src->bytes;
  src->bytes = NULL;
  GST_OBJECT_UNLOCK (src);

  if (bytes != NULL) {
    GST_ELEMENT_PROGRESS (src, CONTINUE, "open", ("Loading HTML data"));
    src->view->loadData (bytes);
    g_bytes_unref (bytes);
  }

  if (created_view) {
    src->n_frames = 0;
  }
  WPE_UNLOCK (src);
  GST_ELEMENT_PROGRESS (src, COMPLETE, "open", ("Ready to produce buffers"));
  return TRUE;
}

static gboolean
gst_wpe_video_src_decide_allocation (GstBaseSrc * base_src, GstQuery * query)
{
  GstGLBaseSrc *gl_src = GST_GL_BASE_SRC (base_src);
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (base_src);
  GstCapsFeatures *caps_features;

  WPE_LOCK (src);
  caps_features = gst_caps_get_features (gl_src->out_caps, 0);
  if (caps_features != NULL
      && gst_caps_features_contains (caps_features,
          GST_CAPS_FEATURE_MEMORY_GL_MEMORY)) {
    src->gl_enabled = TRUE;
  } else {
    src->gl_enabled = FALSE;
  }

  if (src->gl_enabled) {
    WPE_UNLOCK (src);
    return GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_SRC_CLASS, decide_allocation,
        (base_src, query), FALSE);
  }
  WPE_UNLOCK (src);
  return gst_wpe_video_src_start (src);
}

static gboolean
gst_wpe_video_src_gl_start (GstGLBaseSrc * base_src)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (base_src);
  return gst_wpe_video_src_start (src);
}

static void
gst_wpe_video_src_stop_unlocked (GstWpeVideoSrc2 * src)
{
  if (src->view) {
    GST_DEBUG_OBJECT (src, "deleting view %p", src->view);
    delete src->view;
    src->view = NULL;
  }
}

static void
gst_wpe_video_src_gl_stop (GstGLBaseSrc * base_src)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (base_src);

  WPE_LOCK (src);
  gst_wpe_video_src_stop_unlocked (src);
  WPE_UNLOCK (src);
}

static gboolean
gst_wpe_video_src_stop (GstBaseSrc * base_src)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (base_src);

  /* we can call this always, GstGLBaseSrc is smart enough to not crash if
   * gst_gl_base_src_gl_start() has not been called from chaining up
   * gst_wpe_video_src_decide_allocation() */
  if (!GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_SRC_CLASS, stop, (base_src),
          FALSE))
    return FALSE;

  WPE_LOCK (src);

  /* if gl-enabled, gst_wpe_video_src_stop_unlocked() would have already been called
   * inside gst_wpe_video_src_gl_stop() from the base class stopping the OpenGL
   * context */
  if (!src->gl_enabled)
    gst_wpe_video_src_stop_unlocked (src);

  WPE_UNLOCK (src);
  return TRUE;
}

static GstCaps *
gst_wpe_video_src_fixate (GstBaseSrc * base_src, GstCaps * combined_caps)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (base_src);
  GstStructure *structure;
  gint width, height;
  GstCaps *caps;

  /* In situation where software GL support is explicitly requested, select raw
   * caps, otherwise perform default caps negotiation. Unfortunately at this
   * point we don't know yet if a GL context will be usable or not, so we can't
   * check the element GstContext.
   */
  if (!g_strcmp0 (g_getenv ("LIBGL_ALWAYS_SOFTWARE"), "true")) {
    caps = gst_caps_from_string (WPE_RAW_CAPS);
  } else {
    caps = gst_caps_make_writable (combined_caps);
  }

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", DEFAULT_WIDTH);
  gst_structure_fixate_field_nearest_int (structure, "height", DEFAULT_HEIGHT);

  if (gst_structure_has_field (structure, "framerate"))
    gst_structure_fixate_field_nearest_fraction (structure, "framerate",
        DEFAULT_FPS_N, DEFAULT_FPS_D);
  else
    gst_structure_set (structure, "framerate", GST_TYPE_FRACTION, DEFAULT_FPS_N,
        DEFAULT_FPS_D, NULL);

  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (base_src, caps);
  GST_INFO_OBJECT (base_src, "Fixated caps to %" GST_PTR_FORMAT, caps);

  if (src->view) {
    gst_structure_get (structure, "width", G_TYPE_INT, &width, "height",
        G_TYPE_INT, &height, NULL);
    src->view->resize (width, height);
  }
  return caps;
}

void
gst_wpe_video_src_configure_web_view (GstWpeVideoSrc2 * src,
    WebKitWebView * webview)
{
  GValue args[2] = { {0}, {0} };

  g_value_init (&args[0], GST_TYPE_ELEMENT);
  g_value_set_object (&args[0], src);
  g_value_init (&args[1], G_TYPE_OBJECT);
  g_value_set_object (&args[1], webview);

  g_signal_emitv (args, gst_wpe_video_src_signals[SIGNAL_CONFIGURE_WEB_VIEW], 0,
      NULL);

  g_value_unset (&args[0]);
  g_value_unset (&args[1]);
}

static void
gst_wpe_video_src_run_javascript (GstWpeVideoSrc2 * src, const gchar * script)
{
  if (src->view && GST_STATE (GST_ELEMENT_CAST (src)) > GST_STATE_NULL) {
    GST_INFO_OBJECT (src, "running javascript");
    src->view->runJavascript (script);
  }
}

static void
gst_wpe_video_src_load_bytes (GstWpeVideoSrc2 * src, GBytes * bytes)
{
  if (src->view && GST_STATE (GST_ELEMENT_CAST (src)) > GST_STATE_NULL) {
    src->view->loadData (bytes);
  } else {
    GST_OBJECT_LOCK (src);
    if (src->bytes)
      g_bytes_unref (src->bytes);
    src->bytes = g_bytes_ref (bytes);
    GST_OBJECT_UNLOCK (src);
  }
}

static gboolean
gst_wpe_video_src_set_location (GstWpeVideoSrc2 * src, const gchar * location,
    GError ** error)
{
  GST_OBJECT_LOCK (src);
  g_free (src->location);
  src->location = g_strdup (location);
  GST_OBJECT_UNLOCK (src);

  if (src->view)
    src->view->loadUri (location);

  return TRUE;
}

static void
gst_wpe_video_src_set_draw_background (GstWpeVideoSrc2 * src,
    gboolean draw_background)
{
  GST_OBJECT_LOCK (src);
  src->draw_background = draw_background;
  GST_OBJECT_UNLOCK (src);

  if (src->view)
    src->view->setDrawBackground (draw_background);
}

static void
gst_wpe_video_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
    {
      const gchar *location;

      location = g_value_get_string (value);
      if (location == NULL) {
        GST_WARNING_OBJECT (src, "location property cannot be NULL");
        return;
      }

      if (!gst_wpe_video_src_set_location (src, location, NULL)) {
        GST_WARNING_OBJECT (src, "badly formatted location");
        return;
      }
      break;
    }
    case PROP_DRAW_BACKGROUND:
      gst_wpe_video_src_set_draw_background (src, g_value_get_boolean (value));
      break;
    default:
      break;
  }
}

static void
gst_wpe_video_src_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (object);

  switch (prop_id) {
    case PROP_LOCATION:
      GST_OBJECT_LOCK (src);
      g_value_set_string (value, src->location);
      GST_OBJECT_UNLOCK (src);
      break;
    case PROP_DRAW_BACKGROUND:
      GST_OBJECT_LOCK (src);
      g_value_set_boolean (value, src->draw_background);
      GST_OBJECT_UNLOCK (src);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_wpe_video_src_event (GstBaseSrc * base_src, GstEvent * event)
{
  gboolean ret = FALSE;
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (base_src);

  if (src->view && GST_EVENT_TYPE (event) == GST_EVENT_NAVIGATION) {
    GST_DEBUG_OBJECT (src, "Processing event %" GST_PTR_FORMAT, event);
    switch (gst_navigation_event_get_type (event)) {
      case GST_NAVIGATION_EVENT_KEY_PRESS:
      case GST_NAVIGATION_EVENT_KEY_RELEASE:
        ret = src->view->dispatchKeyboardEvent (event);
        break;
      case GST_NAVIGATION_EVENT_MOUSE_BUTTON_PRESS:
      case GST_NAVIGATION_EVENT_MOUSE_BUTTON_RELEASE:
        ret = src->view->dispatchPointerEvent (event);
        break;
      case GST_NAVIGATION_EVENT_MOUSE_MOVE:
        ret = src->view->dispatchPointerMoveEvent (event);
        break;
      case GST_NAVIGATION_EVENT_MOUSE_SCROLL:
        ret = src->view->dispatchAxisEvent (event);
        break;
      case GST_NAVIGATION_EVENT_TOUCH_DOWN:
      case GST_NAVIGATION_EVENT_TOUCH_MOTION:
      case GST_NAVIGATION_EVENT_TOUCH_UP:
        ret = src->view->dispatchTouchEvent (event);
        break;
      case GST_NAVIGATION_EVENT_TOUCH_FRAME:
      case GST_NAVIGATION_EVENT_TOUCH_CANCEL:
        break;
      default:
        break;
    }
  }

  if (!ret) {
    ret =
        GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_SRC_CLASS, event, (base_src,
            event), FALSE);
  }
  return ret;
}

static gboolean
gst_wpe_video_src_query (GstBaseSrc * base_src, GstQuery * query)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (base_src);
  GstGLBaseSrc *gl_src = GST_GL_BASE_SRC (base_src);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_LATENCY:{
      GST_OBJECT_LOCK (src);
      if (gl_src->out_info.fps_n > 0) {
        GstClockTime latency;

        latency = gst_util_uint64_scale (GST_SECOND, gl_src->out_info.fps_d,
            gl_src->out_info.fps_n);
        GST_OBJECT_UNLOCK (src);
        gst_query_set_latency (query,
            gst_base_src_is_live (GST_BASE_SRC_CAST (src)), latency,
            GST_CLOCK_TIME_NONE);
        GST_DEBUG_OBJECT (src, "Reporting latency of %" GST_TIME_FORMAT,
            GST_TIME_ARGS (latency));
        ret = TRUE;
      } else {
        GST_OBJECT_UNLOCK (src);
      }

      break;
    }
    default:
      ret = GST_CALL_PARENT_WITH_DEFAULT (GST_BASE_SRC_CLASS, query,
          (base_src, query), FALSE);
      break;
  }
  return ret;
}

static void
on_view_created (WPEDisplayGStreamer *, WPEView * view, gpointer user_data)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (user_data);
  GValue args[2] = { {0}, {0}
  };

  g_value_init (&args[0], GST_TYPE_WPE_VIDEO_SRC);
  g_value_set_object (&args[0], src);

  g_value_init (&args[1], WPE_TYPE_VIEW);
  g_value_set_object (&args[1], view);

  g_signal_emitv (args, gst_wpe_video_src_signals[SIGNAL_WPE_VIEW_CREATED], 0,
      NULL);

  g_value_unset (&args[0]);
  g_value_unset (&args[1]);
}

static void
gst_wpe_video_src_init (GstWpeVideoSrc2 * src)
{
  src->draw_background = DEFAULT_DRAW_BACKGROUND;
  src->location = g_strdup (DEFAULT_LOCATION);
  src->display = wpe_display_gstreamer_new ();

  g_signal_connect (src->display, "wpe-view-created",
      G_CALLBACK (on_view_created), src);

  gst_base_src_set_live (GST_BASE_SRC_CAST (src), TRUE);

  g_mutex_init (&src->lock);
}

static void
gst_wpe_video_src_finalize (GObject * object)
{
  GstWpeVideoSrc2 *src = GST_WPE_VIDEO_SRC (object);

  g_free (src->location);
  g_clear_pointer (&src->bytes, g_bytes_unref);
  g_mutex_clear (&src->lock);
  g_clear_pointer (&src->display, g_object_unref);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_wpe_video_src_class_init (GstWpeVideoSrc2Class * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstGLBaseSrcClass *gl_base_src_class = GST_GL_BASE_SRC_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);
  GstPadTemplate *tmpl;
  GstCaps *doc_caps;

  gobject_class->set_property = gst_wpe_video_src_set_property;
  gobject_class->get_property = gst_wpe_video_src_get_property;
  gobject_class->finalize = gst_wpe_video_src_finalize;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "location",
          "The URL to display",
          DEFAULT_LOCATION,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DRAW_BACKGROUND,
      g_param_spec_boolean ("draw-background", "Draws the background",
          "Whether to draw the WebView background", DEFAULT_DRAW_BACKGROUND,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));

  gst_element_class_set_static_metadata (gstelement_class,
      "WPE source", "Source/Video",
      "Creates a video stream from a WPE browser",
      "Philippe Normand <philn@igalia.com>, Žan Doberšek <zdobersek@igalia.com>");

  tmpl = gst_static_pad_template_get (&src_factory);
  gst_element_class_add_pad_template (gstelement_class, tmpl);

  base_src_class->fixate = GST_DEBUG_FUNCPTR (gst_wpe_video_src_fixate);
  base_src_class->create = GST_DEBUG_FUNCPTR (gst_wpe_video_src_create);
  base_src_class->decide_allocation =
      GST_DEBUG_FUNCPTR (gst_wpe_video_src_decide_allocation);
  base_src_class->stop = GST_DEBUG_FUNCPTR (gst_wpe_video_src_stop);
  base_src_class->event = GST_DEBUG_FUNCPTR (gst_wpe_video_src_event);
  base_src_class->query = GST_DEBUG_FUNCPTR (gst_wpe_video_src_query);

  gl_base_src_class->supported_gl_api =
      static_cast < GstGLAPI >
      (GST_GL_API_OPENGL | GST_GL_API_OPENGL3 | GST_GL_API_GLES2);
  gl_base_src_class->gl_start = GST_DEBUG_FUNCPTR (gst_wpe_video_src_gl_start);
  gl_base_src_class->gl_stop = GST_DEBUG_FUNCPTR (gst_wpe_video_src_gl_stop);
  gl_base_src_class->fill_gl_memory =
      GST_DEBUG_FUNCPTR (gst_wpe_video_src_fill_memory);

  doc_caps = gst_caps_from_string (WPE_VIDEO_SRC_DOC_CAPS);
  gst_pad_template_set_documentation_caps (tmpl, doc_caps);
  gst_clear_caps (&doc_caps);

  /**
   * GstWpeVideoSrc2::wpe-view-created:
   * @src: the object which received the signal
   * @view: the #WPEView that was created
   *
   * This signal can be used to hook into the WPEView signals as soon as it was
   * created.
   */
  gst_wpe_video_src_signals[SIGNAL_WPE_VIEW_CREATED] =
      g_signal_new ("wpe-view-created", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, WPE_TYPE_VIEW);

  /**
   * GstWpeVideoSrc2::configure-web-view:
   * @src: the object which received the signal
   * @webview: the webView
   *
   * Allow application to configure the webView settings.
   */
  gst_wpe_video_src_signals[SIGNAL_CONFIGURE_WEB_VIEW] =
      g_signal_new ("configure-web-view", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_OBJECT);

  /**
   * GstWpeVideoSrc2::load-bytes:
   * @src: the object which received the signal
   * @bytes: the GBytes data to load
   *
   * Load the specified bytes into the internal webView.
   */
  gst_wpe_video_src_signals[SIGNAL_LOAD_BYTES] =
      g_signal_new_class_handler ("load-bytes", G_TYPE_FROM_CLASS (klass),
      static_cast < GSignalFlags > (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (gst_wpe_video_src_load_bytes), NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_BYTES);

  /**
   * GstWpeVideoSrc2::run-javascript:
   * @src: the object which received the signal
   * @script: the script to run
   *
   * Asynchronously run script in the context of the current page on the
   * internal webView.
   *
   */
  gst_wpe_video_src_signals[SIGNAL_RUN_JAVASCRIPT] =
      g_signal_new_class_handler ("run-javascript", G_TYPE_FROM_CLASS (klass),
      static_cast < GSignalFlags > (G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION),
      G_CALLBACK (gst_wpe_video_src_run_javascript), NULL, NULL, NULL,
      G_TYPE_NONE, 1, G_TYPE_STRING);
}
