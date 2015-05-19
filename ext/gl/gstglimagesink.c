/*
 * GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005,2006,2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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
 * SECTION:element-glimagesink
 *
 * glimagesink renders video frames to a drawable on a local or remote
 * display using OpenGL. This element can receive a Window ID from the
 * application through the VideoOverlay interface and will then render video
 * frames in this drawable.
 * If no Window ID was provided by the application, the element will
 * create its own internal window and render into it.
 *
 * See the #GstGLDisplay documentation for a list of environment variables that
 * can override window/platform detection.
 *
 * <refsect2>
 * <title>Scaling</title>
 * <para>
 * Depends on the driver, OpenGL handles hardware accelerated
 * scaling of video frames. This means that the element will just accept
 * incoming video frames no matter their geometry and will then put them to the
 * drawable scaling them on the fly. Using the #GstGLImageSink:force-aspect-ratio
 * property it is possible to enforce scaling with a constant aspect ratio,
 * which means drawing black borders around the video frame.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Events</title>
 * <para>
 * Through the gl thread, glimagesink handle some events coming from the drawable
 * to manage its appearance even when the data is not flowing (GST_STATE_PAUSED).
 * That means that even when the element is paused, it will receive expose events
 * from the drawable and draw the latest frame with correct borders/aspect-ratio.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch-1.0 -v videotestsrc ! video/x-raw ! glimagesink
 * ]| A pipeline to test hardware scaling.
 * No special opengl extension is used in this pipeline, that's why it should work
 * with OpenGL >= 1.1. That's the case if you are using the MESA3D driver v1.3.
 * |[
 * gst-launch-1.0 -v videotestsrc ! video/x-raw,format=I420 ! glimagesink
 * ]| A pipeline to test hardware scaling and hardware colorspace conversion.
 * When your driver supports GLSL (OpenGL Shading Language needs OpenGL >= 2.1),
 * the 4 following format YUY2, UYVY, I420, YV12 and AYUV are converted to RGB32
 * through some fragment shaders and using one framebuffer (FBO extension OpenGL >= 1.4).
 * If your driver does not support GLSL but supports MESA_YCbCr extension then
 * the you can use YUY2 and UYVY. In this case the colorspace conversion is automatically
 * made when loading the texture and therefore no framebuffer is used.
 * |[
 * gst-launch-1.0 -v gltestsrc ! glimagesink
 * ]| A pipeline 100% OpenGL.
 * No special opengl extension is used in this pipeline, that's why it should work
 * with OpenGL >= 1.1. That's the case if you are using the MESA3D driver v1.3.
 * |[
 * gst-plugins-bas/tests/examples/gl/generic/cube
 * ]| The graphic FPS scene can be greater than the input video FPS.
 * The graphic scene can be written from a client code through the
 * two glfilterapp properties.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/videooverlay.h>
#include <gst/video/navigation.h>

#include "gstglimagesink.h"
#include "gstglsinkbin.h"

#if GST_GL_HAVE_PLATFORM_EGL
#include <gst/gl/egl/gsteglimagememory.h>
#endif

GST_DEBUG_CATEGORY (gst_debug_glimage_sink);
#define GST_CAT_DEFAULT gst_debug_glimage_sink

#define DEFAULT_SHOW_PREROLL_FRAME  TRUE
#define DEFAULT_SYNC                TRUE
#define DEFAULT_MAX_LATENESS        -1
#define DEFAULT_QOS                 FALSE
#define DEFAULT_ASYNC               TRUE
#define DEFAULT_TS_OFFSET           0
#define DEFAULT_BLOCKSIZE           4096
#define DEFAULT_RENDER_DELAY        0
#define DEFAULT_ENABLE_LAST_SAMPLE  TRUE
#define DEFAULT_THROTTLE_TIME       0
#define DEFAULT_MAX_BITRATE         0
#define DEFAULT_HANDLE_EVENTS       TRUE
#define DEFAULT_FORCE_ASPECT_RATIO  TRUE
#define DEFAULT_IGNORE_ALPHA        TRUE

typedef GstGLSinkBin GstGLImageSinkBin;
typedef GstGLSinkBinClass GstGLImageSinkBinClass;

G_DEFINE_TYPE (GstGLImageSinkBin, gst_gl_image_sink_bin, GST_TYPE_GL_SINK_BIN);

enum
{
  PROP_BIN_0,
  PROP_BIN_FORCE_ASPECT_RATIO,
  PROP_BIN_PIXEL_ASPECT_RATIO,
  PROP_BIN_HANDLE_EVENTS,
  PROP_BIN_CONTEXT,
  PROP_BIN_SHOW_PREROLL_FRAME,
  PROP_BIN_SYNC,
  PROP_BIN_MAX_LATENESS,
  PROP_BIN_QOS,
  PROP_BIN_ASYNC,
  PROP_BIN_TS_OFFSET,
  PROP_BIN_ENABLE_LAST_SAMPLE,
  PROP_BIN_LAST_SAMPLE,
  PROP_BIN_BLOCKSIZE,
  PROP_BIN_RENDER_DELAY,
  PROP_BIN_THROTTLE_TIME,
  PROP_BIN_MAX_BITRATE,
};

enum
{
  SIGNAL_BIN_0,
  SIGNAL_BIN_CLIENT_DRAW,
  SIGNAL_BIN_CLIENT_RESHAPE,
  SIGNAL_BIN_LAST,
};

static guint gst_gl_image_sink_bin_signals[SIGNAL_BIN_LAST] = { 0 };

static void
gst_gl_image_sink_bin_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec)
{
  g_object_set_property (G_OBJECT (GST_GL_SINK_BIN (object)->sink),
      param_spec->name, value);
}

static void
gst_gl_image_sink_bin_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec)
{
  g_object_get_property (G_OBJECT (GST_GL_SINK_BIN (object)->sink),
      param_spec->name, value);
}

static gboolean
_on_client_reshape (GstGLImageSink * sink, GstGLContext * context,
    guint width, guint height, gpointer data)
{
  gboolean ret;

  g_signal_emit (data, gst_gl_image_sink_bin_signals[SIGNAL_BIN_CLIENT_RESHAPE],
      0, context, width, height, &ret);

  return ret;
}

static gboolean
_on_client_draw (GstGLImageSink * sink, GstGLContext * context,
    GstSample * sample, gpointer data)
{
  gboolean ret;

  g_signal_emit (data, gst_gl_image_sink_bin_signals[SIGNAL_BIN_CLIENT_DRAW], 0,
      context, sample, &ret);

  return ret;
}

static void
gst_gl_image_sink_bin_init (GstGLImageSinkBin * self)
{
  GstGLImageSink *sink = g_object_new (GST_TYPE_GLIMAGE_SINK, NULL);

  g_signal_connect (sink, "client-reshape", (GCallback) _on_client_reshape,
      self);
  g_signal_connect (sink, "client-draw", (GCallback) _on_client_draw, self);

  gst_gl_sink_bin_finish_init_with_element (GST_GL_SINK_BIN (self),
      GST_ELEMENT (sink));
}

static void
gst_gl_image_sink_bin_class_init (GstGLImageSinkBinClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->get_property = gst_gl_image_sink_bin_get_property;
  gobject_class->set_property = gst_gl_image_sink_bin_set_property;

  /* gl sink */
  g_object_class_install_property (gobject_class, PROP_BIN_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_HANDLE_EVENTS,
      g_param_spec_boolean ("ignore-alpha", "Ignore Alpha",
          "When enabled, alpha will be ignored and converted to black",
          DEFAULT_IGNORE_ALPHA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_CONTEXT,
      g_param_spec_object ("context", "OpenGL context", "Get OpenGL context",
          GST_GL_TYPE_CONTEXT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", 0, 1, G_MAXINT, 1, 1, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /* video sink */
  g_object_class_install_property (gobject_class, PROP_BIN_SHOW_PREROLL_FRAME,
      g_param_spec_boolean ("show-preroll-frame", "Show preroll frame",
          "Whether to render video frames during preroll",
          DEFAULT_SHOW_PREROLL_FRAME,
          G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS));

  /* base sink */
  g_object_class_install_property (gobject_class, PROP_BIN_SYNC,
      g_param_spec_boolean ("sync", "Sync", "Sync on the clock", DEFAULT_SYNC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_MAX_LATENESS,
      g_param_spec_int64 ("max-lateness", "Max Lateness",
          "Maximum number of nanoseconds that a buffer can be late before it "
          "is dropped (-1 unlimited)", -1, G_MAXINT64, DEFAULT_MAX_LATENESS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_QOS,
      g_param_spec_boolean ("qos", "Qos",
          "Generate Quality-of-Service events upstream", DEFAULT_QOS,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_ASYNC,
      g_param_spec_boolean ("async", "Async",
          "Go asynchronously to PAUSED", DEFAULT_ASYNC,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_TS_OFFSET,
      g_param_spec_int64 ("ts-offset", "TS Offset",
          "Timestamp offset in nanoseconds", G_MININT64, G_MAXINT64,
          DEFAULT_TS_OFFSET, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_ENABLE_LAST_SAMPLE,
      g_param_spec_boolean ("enable-last-sample", "Enable Last Buffer",
          "Enable the last-sample property", DEFAULT_ENABLE_LAST_SAMPLE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_LAST_SAMPLE,
      g_param_spec_boxed ("last-sample", "Last Sample",
          "The last sample received in the sink", GST_TYPE_SAMPLE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_BLOCKSIZE,
      g_param_spec_uint ("blocksize", "Block size",
          "Size in bytes to pull per buffer (0 = default)", 0, G_MAXUINT,
          DEFAULT_BLOCKSIZE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_RENDER_DELAY,
      g_param_spec_uint64 ("render-delay", "Render Delay",
          "Additional render delay of the sink in nanoseconds", 0, G_MAXUINT64,
          DEFAULT_RENDER_DELAY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_THROTTLE_TIME,
      g_param_spec_uint64 ("throttle-time", "Throttle time",
          "The time to keep between rendered buffers (0 = disabled)", 0,
          G_MAXUINT64, DEFAULT_THROTTLE_TIME,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_BIN_MAX_BITRATE,
      g_param_spec_uint64 ("max-bitrate", "Max Bitrate",
          "The maximum bits per second to render (0 = disabled)", 0,
          G_MAXUINT64, DEFAULT_MAX_BITRATE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_gl_image_sink_bin_signals[SIGNAL_BIN_CLIENT_DRAW] =
      g_signal_new ("client-draw", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 2, GST_GL_TYPE_CONTEXT, GST_TYPE_SAMPLE);

  gst_gl_image_sink_bin_signals[SIGNAL_BIN_CLIENT_RESHAPE] =
      g_signal_new ("client-reshape", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 3, GST_GL_TYPE_CONTEXT, G_TYPE_UINT, G_TYPE_UINT);
}

#define GST_GLIMAGE_SINK_GET_LOCK(glsink) \
  (GST_GLIMAGE_SINK(glsink)->drawing_lock)
#define GST_GLIMAGE_SINK_LOCK(glsink) \
  (g_mutex_lock(&GST_GLIMAGE_SINK_GET_LOCK (glsink)))
#define GST_GLIMAGE_SINK_UNLOCK(glsink) \
  (g_mutex_unlock(&GST_GLIMAGE_SINK_GET_LOCK (glsink)))

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

#define SUPPORTED_GL_APIS GST_GL_API_OPENGL | GST_GL_API_GLES2 | GST_GL_API_OPENGL3

static void gst_glimage_sink_thread_init_redisplay (GstGLImageSink * gl_sink);
static void gst_glimage_sink_cleanup_glthread (GstGLImageSink * gl_sink);
static void gst_glimage_sink_on_close (GstGLImageSink * gl_sink);
static void gst_glimage_sink_on_resize (GstGLImageSink * gl_sink,
    gint width, gint height);
static void gst_glimage_sink_on_draw (GstGLImageSink * gl_sink);
static gboolean gst_glimage_sink_redisplay (GstGLImageSink * gl_sink);

static void gst_glimage_sink_finalize (GObject * object);
static void gst_glimage_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_glimage_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static gboolean gst_glimage_sink_query (GstBaseSink * bsink, GstQuery * query);
static void gst_glimage_sink_set_context (GstElement * element,
    GstContext * context);

static GstStateChangeReturn
gst_glimage_sink_change_state (GstElement * element, GstStateChange transition);

static void gst_glimage_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_glimage_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstCaps *gst_glimage_sink_get_caps (GstBaseSink * bsink,
    GstCaps * filter);
static GstFlowReturn gst_glimage_sink_prepare (GstBaseSink * bsink,
    GstBuffer * buf);
static GstFlowReturn gst_glimage_sink_show_frame (GstVideoSink * bsink,
    GstBuffer * buf);
static gboolean gst_glimage_sink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);

static void gst_glimage_sink_video_overlay_init (GstVideoOverlayInterface *
    iface);
static void gst_glimage_sink_set_window_handle (GstVideoOverlay * overlay,
    guintptr id);
static void gst_glimage_sink_expose (GstVideoOverlay * overlay);

static void
gst_glimage_sink_handle_events (GstVideoOverlay * overlay,
    gboolean handle_events);

static GstStaticPadTemplate gst_glimage_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
            "RGBA"))
    );

enum
{
  ARG_0,
  ARG_DISPLAY,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO,
  PROP_CONTEXT,
  PROP_HANDLE_EVENTS,
  PROP_IGNORE_ALPHA,
};

enum
{
  SIGNAL_0,
  CLIENT_DRAW_SIGNAL,
  CLIENT_RESHAPE_SIGNAL,
  LAST_SIGNAL
};

static guint gst_glimage_sink_signals[LAST_SIGNAL] = { 0 };

static void
gst_glimage_sink_navigation_send_event (GstNavigation * navigation, GstStructure
    * structure)
{
  GstGLImageSink *sink = GST_GLIMAGE_SINK (navigation);
  GstEvent *event = NULL;
  GstPad *pad = NULL;
  GstGLWindow *window;
  guint width, height;
  gdouble x, y, xscale, yscale;

  if (!sink->context)
    return;

  window = gst_gl_context_get_window (sink->context);
  g_return_if_fail (GST_GL_IS_WINDOW (window));

  width = GST_VIDEO_SINK_WIDTH (sink);
  height = GST_VIDEO_SINK_HEIGHT (sink);
  gst_gl_window_get_surface_dimensions (window, &width, &height);

  event = gst_event_new_navigation (structure);

  pad = gst_pad_get_peer (GST_VIDEO_SINK_PAD (sink));
  /* Converting pointer coordinates to the non scaled geometry */
  if (width != GST_VIDEO_SINK_WIDTH (sink) &&
      width != 0 && gst_structure_get_double (structure, "pointer_x", &x)) {
    xscale = (gdouble) GST_VIDEO_SINK_WIDTH (sink) / width;
    gst_structure_set (structure, "pointer_x", G_TYPE_DOUBLE,
        (gdouble) x * xscale, NULL);
  }
  if (height != GST_VIDEO_SINK_HEIGHT (sink) &&
      height != 0 && gst_structure_get_double (structure, "pointer_y", &y)) {
    yscale = (gdouble) GST_VIDEO_SINK_HEIGHT (sink) / height;
    gst_structure_set (structure, "pointer_y", G_TYPE_DOUBLE,
        (gdouble) y * yscale, NULL);
  }


  if (GST_IS_PAD (pad) && GST_IS_EVENT (event))
    gst_pad_send_event (pad, event);

  gst_object_unref (pad);
  gst_object_unref (window);
}

static void
gst_glimage_sink_navigation_interface_init (GstNavigationInterface * iface)
{
  iface->send_event = gst_glimage_sink_navigation_send_event;
}

#define gst_glimage_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLImageSink, gst_glimage_sink,
    GST_TYPE_VIDEO_SINK, G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_glimage_sink_video_overlay_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_NAVIGATION,
        gst_glimage_sink_navigation_interface_init);
    GST_DEBUG_CATEGORY_INIT (gst_debug_glimage_sink, "glimagesink", 0,
        "OpenGL Video Sink"));

static void
gst_glimage_sink_class_init (GstGLImageSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstVideoSinkClass *gstvideosink_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  gstvideosink_class = (GstVideoSinkClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_glimage_sink_set_property;
  gobject_class->get_property = gst_glimage_sink_get_property;

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio", "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio",
          DEFAULT_FORCE_ASPECT_RATIO,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", 0, 1, G_MAXINT, 1, 1, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CONTEXT,
      g_param_spec_object ("context", "OpenGL context", "Get OpenGL context",
          GST_GL_TYPE_CONTEXT, G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_HANDLE_EVENTS,
      g_param_spec_boolean ("handle-events", "Handle XEvents",
          "When enabled, XEvents will be selected and handled",
          DEFAULT_HANDLE_EVENTS, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_IGNORE_ALPHA,
      g_param_spec_boolean ("ignore-alpha", "Ignore Alpha",
          "When enabled, alpha will be ignored and converted to black",
          DEFAULT_IGNORE_ALPHA, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "OpenGL video sink",
      "Sink/Video", "A videosink based on OpenGL",
      "Julien Isorce <julien.isorce@gmail.com>");

  /**
   * GstGLImageSink::client-draw:
   * @object: the #GstGLImageSink
   * @texture: the #guint id of the texture.
   * @width: the #guint width of the texture.
   * @height: the #guint height of the texture.
   *
   * Will be emitted before actually drawing the texture.  The client should
   * redraw the surface/contents with the @texture, @width and @height and
   * and return %TRUE.
   *
   * Returns: whether the texture was redrawn by the signal.  If not, a
   *          default redraw will occur.
   */
  gst_glimage_sink_signals[CLIENT_DRAW_SIGNAL] =
      g_signal_new ("client-draw", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 2, GST_GL_TYPE_CONTEXT, GST_TYPE_SAMPLE);

  /**
   * GstGLImageSink::client-reshape:
   * @object: the #GstGLImageSink
   * @width: the #guint width of the texture.
   * @height: the #guint height of the texture.
   *
   * The client should resize the surface/window/viewport with the @width and
   * @height and return %TRUE.
   *
   * Returns: whether the content area was resized by the signal.  If not, a
   *          default viewport resize will occur.
   */
  gst_glimage_sink_signals[CLIENT_RESHAPE_SIGNAL] =
      g_signal_new ("client-reshape", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 3, GST_GL_TYPE_CONTEXT, G_TYPE_UINT, G_TYPE_UINT);

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_glimage_sink_template));

  gobject_class->finalize = gst_glimage_sink_finalize;

  gstelement_class->change_state = gst_glimage_sink_change_state;
  gstelement_class->set_context = gst_glimage_sink_set_context;
  gstbasesink_class->query = GST_DEBUG_FUNCPTR (gst_glimage_sink_query);
  gstbasesink_class->set_caps = gst_glimage_sink_set_caps;
  gstbasesink_class->get_caps = gst_glimage_sink_get_caps;
  gstbasesink_class->get_times = gst_glimage_sink_get_times;
  gstbasesink_class->prepare = gst_glimage_sink_prepare;
  gstbasesink_class->propose_allocation = gst_glimage_sink_propose_allocation;

  gstvideosink_class->show_frame =
      GST_DEBUG_FUNCPTR (gst_glimage_sink_show_frame);
}

static void
gst_glimage_sink_init (GstGLImageSink * glimage_sink)
{
  glimage_sink->window_id = 0;
  glimage_sink->new_window_id = 0;
  glimage_sink->display = NULL;
  glimage_sink->keep_aspect_ratio = TRUE;
  glimage_sink->par_n = 0;
  glimage_sink->par_d = 1;
  glimage_sink->pool = NULL;
  glimage_sink->stored_buffer = NULL;
  glimage_sink->redisplay_texture = 0;
  glimage_sink->handle_events = TRUE;
  glimage_sink->ignore_alpha = TRUE;

  g_mutex_init (&glimage_sink->drawing_lock);
}

static void
gst_glimage_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLImageSink *glimage_sink;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (object));

  glimage_sink = GST_GLIMAGE_SINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
    {
      glimage_sink->keep_aspect_ratio = g_value_get_boolean (value);
      break;
    }
    case PROP_PIXEL_ASPECT_RATIO:
    {
      glimage_sink->par_n = gst_value_get_fraction_numerator (value);
      glimage_sink->par_d = gst_value_get_fraction_denominator (value);
      break;
    }
    case PROP_HANDLE_EVENTS:
      gst_glimage_sink_handle_events (GST_VIDEO_OVERLAY (glimage_sink),
          g_value_get_boolean (value));
      break;
    case PROP_IGNORE_ALPHA:
      glimage_sink->ignore_alpha = g_value_get_boolean (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_glimage_sink_finalize (GObject * object)
{
  GstGLImageSink *glimage_sink;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (object));

  glimage_sink = GST_GLIMAGE_SINK (object);

  g_mutex_clear (&glimage_sink->drawing_lock);

  GST_DEBUG ("finalized");
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_glimage_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLImageSink *glimage_sink;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (object));

  glimage_sink = GST_GLIMAGE_SINK (object);

  switch (prop_id) {
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, glimage_sink->keep_aspect_ratio);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gst_value_set_fraction (value, glimage_sink->par_n, glimage_sink->par_d);
      break;
    case PROP_CONTEXT:
      g_value_set_object (value, glimage_sink->context);
      break;
    case PROP_HANDLE_EVENTS:
      g_value_set_boolean (value, glimage_sink->handle_events);
      break;
    case PROP_IGNORE_ALPHA:
      g_value_set_boolean (value, glimage_sink->ignore_alpha);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_glimage_sink_key_event_cb (GstGLWindow * window, char *event_name, char
    *key_string, GstGLImageSink * gl_sink)
{
  GST_DEBUG_OBJECT (gl_sink, "glimagesink event %s key %s pressed", event_name,
      key_string);
  gst_navigation_send_key_event (GST_NAVIGATION (gl_sink),
      event_name, key_string);
}

static void
gst_glimage_sink_mouse_event_cb (GstGLWindow * window, char *event_name,
    int button, double posx, double posy, GstGLImageSink * gl_sink)
{
  GST_DEBUG_OBJECT (gl_sink, "glimagesink event %s at %g, %g", event_name, posx,
      posy);
  gst_navigation_send_mouse_event (GST_NAVIGATION (gl_sink),
      event_name, button, posx, posy);
}

static gboolean
_ensure_gl_setup (GstGLImageSink * gl_sink)
{
  GError *error = NULL;

  GST_DEBUG_OBJECT (gl_sink, "Ensuring setup");

  if (!gl_sink->context) {
    GST_OBJECT_LOCK (gl_sink->display);
    do {
      GstGLContext *other_context;
      GstGLWindow *window;

      if (gl_sink->context)
        gst_object_unref (gl_sink->context);

      GST_DEBUG_OBJECT (gl_sink,
          "No current context, creating one for %" GST_PTR_FORMAT,
          gl_sink->display);

      gl_sink->context = gst_gl_context_new (gl_sink->display);
      if (!gl_sink->context) {
        GST_OBJECT_UNLOCK (gl_sink->display);
        goto context_creation_error;
      }

      window = gst_gl_context_get_window (gl_sink->context);

      GST_DEBUG_OBJECT (gl_sink, "got window %" GST_PTR_FORMAT, window);

      if (!gl_sink->window_id && !gl_sink->new_window_id)
        gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (gl_sink));

      GST_DEBUG_OBJECT (gl_sink,
          "window_id : %" G_GUINTPTR_FORMAT " , new_window_id : %"
          G_GUINTPTR_FORMAT, gl_sink->window_id, gl_sink->new_window_id);

      if (gl_sink->window_id != gl_sink->new_window_id) {
        gl_sink->window_id = gl_sink->new_window_id;
        GST_DEBUG_OBJECT (gl_sink, "Setting window handle on gl window");
        gst_gl_window_set_window_handle (window, gl_sink->window_id);
      }

      if (gl_sink->other_context) {
        other_context = gst_object_ref (gl_sink->other_context);
      } else {
        other_context =
            gst_gl_display_get_gl_context_for_thread (gl_sink->display, NULL);
      }

      GST_DEBUG_OBJECT (gl_sink,
          "creating context %" GST_PTR_FORMAT " from other context %"
          GST_PTR_FORMAT, gl_sink->context, other_context);

      if (!gst_gl_context_create (gl_sink->context, other_context, &error)) {
        if (other_context)
          gst_object_unref (other_context);
        gst_object_unref (window);
        GST_OBJECT_UNLOCK (gl_sink->display);
        goto context_error;
      }

      gst_gl_window_handle_events (window, gl_sink->handle_events);

      /* setup callbacks */
      gst_gl_window_set_resize_callback (window,
          GST_GL_WINDOW_RESIZE_CB (gst_glimage_sink_on_resize),
          gst_object_ref (gl_sink), (GDestroyNotify) gst_object_unref);
      gst_gl_window_set_draw_callback (window,
          GST_GL_WINDOW_CB (gst_glimage_sink_on_draw),
          gst_object_ref (gl_sink), (GDestroyNotify) gst_object_unref);
      gst_gl_window_set_close_callback (window,
          GST_GL_WINDOW_CB (gst_glimage_sink_on_close),
          gst_object_ref (gl_sink), (GDestroyNotify) gst_object_unref);
      gl_sink->key_sig_id = g_signal_connect (window, "key-event", G_CALLBACK
          (gst_glimage_sink_key_event_cb), gl_sink);
      gl_sink->mouse_sig_id =
          g_signal_connect (window, "mouse-event",
          G_CALLBACK (gst_glimage_sink_mouse_event_cb), gl_sink);

      if (other_context)
        gst_object_unref (other_context);
      gst_object_unref (window);
    } while (!gst_gl_display_add_context (gl_sink->display, gl_sink->context));
    GST_OBJECT_UNLOCK (gl_sink->display);
  } else
    GST_DEBUG_OBJECT (gl_sink, "Already have a context");

  return TRUE;

context_creation_error:
  {
    GST_ELEMENT_ERROR (gl_sink, RESOURCE, NOT_FOUND,
        ("Failed to create GL context"), (NULL));
    return FALSE;
  }

context_error:
  {
    GST_ELEMENT_ERROR (gl_sink, RESOURCE, NOT_FOUND, ("%s", error->message),
        (NULL));
    gst_object_unref (gl_sink->context);
    gl_sink->context = NULL;
    return FALSE;
  }
}

static gboolean
gst_glimage_sink_query (GstBaseSink * bsink, GstQuery * query)
{
  GstGLImageSink *glimage_sink = GST_GLIMAGE_SINK (bsink);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      const gchar *context_type;
      GstContext *context, *old_context;

      res = gst_gl_handle_context_query ((GstElement *) glimage_sink, query,
          &glimage_sink->display, &glimage_sink->other_context);
      if (glimage_sink->display)
        gst_gl_display_filter_gl_api (glimage_sink->display, SUPPORTED_GL_APIS);

      gst_query_parse_context_type (query, &context_type);

      if (g_strcmp0 (context_type, "gst.gl.local_context") == 0) {
        GstStructure *s;

        gst_query_parse_context (query, &old_context);

        if (old_context)
          context = gst_context_copy (old_context);
        else
          context = gst_context_new ("gst.gl.local_context", FALSE);

        s = gst_context_writable_structure (context);
        gst_structure_set (s, "context", GST_GL_TYPE_CONTEXT,
            glimage_sink->context, NULL);
        gst_query_set_context (query, context);
        gst_context_unref (context);

        res = glimage_sink->context != NULL;
      }
      GST_LOG_OBJECT (glimage_sink, "context query of type %s %i", context_type,
          res);

      if (res)
        return res;
      break;
    }
    case GST_QUERY_DRAIN:
    {
      GstBuffer *buf = NULL;

      GST_GLIMAGE_SINK_LOCK (glimage_sink);
      glimage_sink->redisplay_texture = 0;
      buf = glimage_sink->stored_buffer;
      glimage_sink->stored_buffer = NULL;
      GST_GLIMAGE_SINK_UNLOCK (glimage_sink);

      if (buf)
        gst_buffer_unref (buf);

      gst_buffer_replace (&glimage_sink->next_buffer, NULL);
      gst_buffer_replace (&glimage_sink->next_sync, NULL);

      res = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
    }
    default:
      res = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
  }

  return res;
}

static void
gst_glimage_sink_set_context (GstElement * element, GstContext * context)
{
  GstGLImageSink *gl_sink = GST_GLIMAGE_SINK (element);

  gst_gl_handle_set_context (element, context, &gl_sink->display,
      &gl_sink->other_context);

  if (gl_sink->display)
    gst_gl_display_filter_gl_api (gl_sink->display, SUPPORTED_GL_APIS);
}

static GstStateChangeReturn
gst_glimage_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstGLImageSink *glimage_sink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("changing state: %s => %s",
      gst_element_state_get_name (GST_STATE_TRANSITION_CURRENT (transition)),
      gst_element_state_get_name (GST_STATE_TRANSITION_NEXT (transition)));

  glimage_sink = GST_GLIMAGE_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      if (!gst_gl_ensure_element_data (glimage_sink, &glimage_sink->display,
              &glimage_sink->other_context))
        return GST_STATE_CHANGE_FAILURE;

      gst_gl_display_filter_gl_api (glimage_sink->display, SUPPORTED_GL_APIS);
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      if (!_ensure_gl_setup (glimage_sink))
        return GST_STATE_CHANGE_FAILURE;

      g_atomic_int_set (&glimage_sink->to_quit, 0);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
  if (ret == GST_STATE_CHANGE_FAILURE)
    return ret;

  switch (transition) {
    case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
    {
      /* mark the redisplay_texture as unavailable (=0)
       * to avoid drawing
       */
      GST_GLIMAGE_SINK_LOCK (glimage_sink);
      if (glimage_sink->stored_sync)
        gst_buffer_unref (glimage_sink->stored_sync);
      glimage_sink->stored_sync = NULL;

      glimage_sink->redisplay_texture = 0;
      if (glimage_sink->stored_buffer) {
        gst_buffer_unref (glimage_sink->stored_buffer);
        glimage_sink->stored_buffer = NULL;
      }
      gst_buffer_replace (&glimage_sink->next_buffer, NULL);
      gst_buffer_replace (&glimage_sink->next_sync, NULL);
      GST_GLIMAGE_SINK_UNLOCK (glimage_sink);

      glimage_sink->window_id = 0;
      /* but do not reset glimage_sink->new_window_id */

      GST_VIDEO_SINK_WIDTH (glimage_sink) = 1;
      GST_VIDEO_SINK_HEIGHT (glimage_sink) = 1;
      /* Clear cached caps */
      if (glimage_sink->caps) {
        gst_caps_unref (glimage_sink->caps);
        glimage_sink->caps = NULL;
      }

      /* we're losing the context, this pool is no use anymore */
      if (glimage_sink->pool) {
        gst_object_unref (glimage_sink->pool);
        glimage_sink->pool = NULL;
      }

      if (glimage_sink->context) {
        GstGLWindow *window = gst_gl_context_get_window (glimage_sink->context);

        gst_gl_window_send_message (window,
            GST_GL_WINDOW_CB (gst_glimage_sink_cleanup_glthread), glimage_sink);

        gst_gl_window_set_resize_callback (window, NULL, NULL, NULL);
        gst_gl_window_set_draw_callback (window, NULL, NULL, NULL);
        gst_gl_window_set_close_callback (window, NULL, NULL, NULL);

        if (glimage_sink->key_sig_id)
          g_signal_handler_disconnect (window, glimage_sink->key_sig_id);
        glimage_sink->key_sig_id = 0;
        if (glimage_sink->mouse_sig_id)
          g_signal_handler_disconnect (window, glimage_sink->mouse_sig_id);
        glimage_sink->mouse_sig_id = 0;

        gst_object_unref (window);
        gst_object_unref (glimage_sink->context);
        glimage_sink->context = NULL;
      }
      break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
      if (glimage_sink->other_context) {
        gst_object_unref (glimage_sink->other_context);
        glimage_sink->other_context = NULL;
      }

      if (glimage_sink->display) {
        gst_object_unref (glimage_sink->display);
        glimage_sink->display = NULL;
      }
      break;
    default:
      break;
  }

  return ret;
}

static void
gst_glimage_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end)
{
  GstGLImageSink *glimagesink;

  glimagesink = GST_GLIMAGE_SINK (bsink);

  if (GST_BUFFER_TIMESTAMP_IS_VALID (buf)) {
    *start = GST_BUFFER_TIMESTAMP (buf);
    if (GST_BUFFER_DURATION_IS_VALID (buf))
      *end = *start + GST_BUFFER_DURATION (buf);
    else {
      if (GST_VIDEO_INFO_FPS_N (&glimagesink->info) > 0) {
        *end = *start +
            gst_util_uint64_scale_int (GST_SECOND,
            GST_VIDEO_INFO_FPS_D (&glimagesink->info),
            GST_VIDEO_INFO_FPS_N (&glimagesink->info));
      }
    }
  }
}

static GstCaps *
gst_glimage_sink_get_caps (GstBaseSink * bsink, GstCaps * filter)
{
  GstCaps *tmp = NULL;
  GstCaps *result = NULL;

  tmp = gst_pad_get_pad_template_caps (GST_BASE_SINK_PAD (bsink));

  if (filter) {
    result = gst_caps_intersect_full (filter, tmp, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tmp);
  } else {
    result = tmp;
  }

  GST_DEBUG_OBJECT (bsink, "returning caps: %" GST_PTR_FORMAT, result);

  return result;
}

static gboolean
gst_glimage_sink_set_caps (GstBaseSink * bsink, GstCaps * caps)
{
  GstGLImageSink *glimage_sink;
  gint width;
  gint height;
  gboolean ok;
  gint par_n, par_d;
  gint display_par_n, display_par_d;
  guint display_ratio_num, display_ratio_den;
  GstVideoInfo vinfo;

  GST_DEBUG_OBJECT (bsink, "set caps with %" GST_PTR_FORMAT, caps);

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  ok = gst_video_info_from_caps (&vinfo, caps);
  if (!ok)
    return FALSE;

  width = GST_VIDEO_INFO_WIDTH (&vinfo);
  height = GST_VIDEO_INFO_HEIGHT (&vinfo);

  par_n = GST_VIDEO_INFO_PAR_N (&vinfo);
  par_d = GST_VIDEO_INFO_PAR_D (&vinfo);

  if (!par_n)
    par_n = 1;

  /* get display's PAR */
  if (glimage_sink->par_n != 0 && glimage_sink->par_d != 0) {
    display_par_n = glimage_sink->par_n;
    display_par_d = glimage_sink->par_d;
  } else {
    display_par_n = 1;
    display_par_d = 1;
  }

  ok = gst_video_calculate_display_ratio (&display_ratio_num,
      &display_ratio_den, width, height, par_n, par_d, display_par_n,
      display_par_d);

  if (!ok)
    return FALSE;

  GST_TRACE ("PAR: %u/%u DAR:%u/%u", par_n, par_d, display_par_n,
      display_par_d);

  if (height % display_ratio_den == 0) {
    GST_DEBUG ("keeping video height");
    GST_VIDEO_SINK_WIDTH (glimage_sink) = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    GST_VIDEO_SINK_HEIGHT (glimage_sink) = height;
  } else if (width % display_ratio_num == 0) {
    GST_DEBUG ("keeping video width");
    GST_VIDEO_SINK_WIDTH (glimage_sink) = width;
    GST_VIDEO_SINK_HEIGHT (glimage_sink) = (guint)
        gst_util_uint64_scale_int (width, display_ratio_den, display_ratio_num);
  } else {
    GST_DEBUG ("approximating while keeping video height");
    GST_VIDEO_SINK_WIDTH (glimage_sink) = (guint)
        gst_util_uint64_scale_int (height, display_ratio_num,
        display_ratio_den);
    GST_VIDEO_SINK_HEIGHT (glimage_sink) = height;
  }
  GST_DEBUG ("scaling to %dx%d", GST_VIDEO_SINK_WIDTH (glimage_sink),
      GST_VIDEO_SINK_HEIGHT (glimage_sink));

  glimage_sink->info = vinfo;
  glimage_sink->caps = gst_caps_ref (caps);

  if (!_ensure_gl_setup (glimage_sink))
    return FALSE;

  glimage_sink->caps_change = TRUE;

  return TRUE;
}

static GstFlowReturn
gst_glimage_sink_prepare (GstBaseSink * bsink, GstBuffer * buf)
{
  GstGLImageSink *glimage_sink;
  GstVideoFrame gl_frame;
  GstBuffer *next_sync, *old_sync, *old_buffer;
  GstGLSyncMeta *sync_meta;

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  GST_TRACE ("preparing buffer:%p", buf);

  if (GST_VIDEO_SINK_WIDTH (glimage_sink) < 1 ||
      GST_VIDEO_SINK_HEIGHT (glimage_sink) < 1) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (!_ensure_gl_setup (glimage_sink))
    return GST_FLOW_NOT_NEGOTIATED;

  if (!gst_video_frame_map (&gl_frame, &glimage_sink->info, buf,
          GST_MAP_READ | GST_MAP_GL)) {
    goto upload_failed;
  }

  next_sync = gst_buffer_new ();
  sync_meta = gst_buffer_add_gl_sync_meta (glimage_sink->context, next_sync);
  gst_gl_sync_meta_set_sync_point (sync_meta, glimage_sink->context);

  GST_GLIMAGE_SINK_LOCK (glimage_sink);
  glimage_sink->next_tex = *(guint *) gl_frame.data[0];

  old_buffer = glimage_sink->next_buffer;
  glimage_sink->next_buffer = gst_buffer_ref (buf);

  old_sync = glimage_sink->next_sync;
  glimage_sink->next_sync = next_sync;
  GST_GLIMAGE_SINK_UNLOCK (glimage_sink);

  if (old_buffer)
    gst_buffer_unref (old_buffer);
  if (old_sync)
    gst_buffer_unref (old_sync);

  gst_video_frame_unmap (&gl_frame);

  if (glimage_sink->window_id != glimage_sink->new_window_id) {
    GstGLWindow *window = gst_gl_context_get_window (glimage_sink->context);

    glimage_sink->window_id = glimage_sink->new_window_id;
    gst_gl_window_set_window_handle (window, glimage_sink->window_id);

    gst_object_unref (window);
  }

  return GST_FLOW_OK;

upload_failed:
  {
    GST_ELEMENT_ERROR (glimage_sink, RESOURCE, NOT_FOUND,
        ("%s", "Failed to upload buffer"), (NULL));
    return GST_FLOW_ERROR;
  }
}

static GstFlowReturn
gst_glimage_sink_show_frame (GstVideoSink * vsink, GstBuffer * buf)
{
  GstGLImageSink *glimage_sink;
  GstBuffer *stored_buffer, *old_sync;

  GST_TRACE ("rendering buffer:%p", buf);

  glimage_sink = GST_GLIMAGE_SINK (vsink);

  GST_TRACE ("redisplay texture:%u of size:%ux%u, window size:%ux%u",
      glimage_sink->next_tex, GST_VIDEO_INFO_WIDTH (&glimage_sink->info),
      GST_VIDEO_INFO_HEIGHT (&glimage_sink->info),
      GST_VIDEO_SINK_WIDTH (glimage_sink),
      GST_VIDEO_SINK_HEIGHT (glimage_sink));

  /* Avoid to release the texture while drawing */
  GST_GLIMAGE_SINK_LOCK (glimage_sink);
  glimage_sink->redisplay_texture = glimage_sink->next_tex;
  stored_buffer = glimage_sink->stored_buffer;
  glimage_sink->stored_buffer = gst_buffer_ref (glimage_sink->next_buffer);

  old_sync = glimage_sink->stored_sync;
  glimage_sink->stored_sync = gst_buffer_ref (glimage_sink->next_sync);

  GST_GLIMAGE_SINK_UNLOCK (glimage_sink);

  /* Ask the underlying window to redraw its content */
  if (!gst_glimage_sink_redisplay (glimage_sink))
    goto redisplay_failed;

  GST_TRACE ("post redisplay");

  if (stored_buffer)
    gst_buffer_unref (stored_buffer);
  if (old_sync)
    gst_buffer_unref (old_sync);

  if (g_atomic_int_get (&glimage_sink->to_quit) != 0) {
    GST_ELEMENT_ERROR (glimage_sink, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return GST_FLOW_ERROR;
  }

  return GST_FLOW_OK;

/* ERRORS */
redisplay_failed:
  {
    GST_ELEMENT_ERROR (glimage_sink, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return GST_FLOW_ERROR;
  }
}

static void
gst_glimage_sink_video_overlay_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_glimage_sink_set_window_handle;
  iface->handle_events = gst_glimage_sink_handle_events;
  iface->expose = gst_glimage_sink_expose;
}

static void
gst_glimage_sink_set_window_handle (GstVideoOverlay * overlay, guintptr id)
{
  GstGLImageSink *glimage_sink = GST_GLIMAGE_SINK (overlay);
  guintptr window_id = (guintptr) id;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (overlay));

  GST_DEBUG ("set_xwindow_id %" G_GUINT64_FORMAT, (guint64) window_id);

  glimage_sink->new_window_id = window_id;
}


static void
gst_glimage_sink_expose (GstVideoOverlay * overlay)
{
  GstGLImageSink *glimage_sink = GST_GLIMAGE_SINK (overlay);

  /* redisplay opengl scene */
  if (glimage_sink->display && glimage_sink->window_id) {

    if (glimage_sink->window_id != glimage_sink->new_window_id) {
      GstGLWindow *window = gst_gl_context_get_window (glimage_sink->context);

      glimage_sink->window_id = glimage_sink->new_window_id;
      gst_gl_window_set_window_handle (window, glimage_sink->window_id);

      gst_object_unref (window);
    }

    gst_glimage_sink_redisplay (glimage_sink);
  }
}

static void
gst_glimage_sink_handle_events (GstVideoOverlay * overlay,
    gboolean handle_events)
{
  GstGLImageSink *glimage_sink = GST_GLIMAGE_SINK (overlay);

  glimage_sink->handle_events = handle_events;
  if (G_LIKELY (glimage_sink->context)) {
    GstGLWindow *window;
    window = gst_gl_context_get_window (glimage_sink->context);
    gst_gl_window_handle_events (window, handle_events);
    gst_object_unref (window);
  }
}

static gboolean
gst_glimage_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstGLImageSink *glimage_sink = GST_GLIMAGE_SINK (bsink);
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;

  if (!_ensure_gl_setup (glimage_sink))
    return FALSE;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if (need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    /* the normal size of a frame */
    size = info.size;

    if (glimage_sink->pool) {
      GstCaps *pcaps;

      /* we had a pool, check caps */
      GST_DEBUG_OBJECT (glimage_sink, "check existing pool caps");
      config = gst_buffer_pool_get_config (glimage_sink->pool);
      gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

      if (!gst_caps_is_equal (caps, pcaps)) {
        GST_DEBUG_OBJECT (glimage_sink, "pool has different caps");
        /* different caps, we can't use this pool */
        gst_object_unref (glimage_sink->pool);
        glimage_sink->pool = NULL;
      }
      gst_structure_free (config);
    }

    if (glimage_sink->pool == NULL) {
      GST_DEBUG_OBJECT (glimage_sink, "create new pool");

      glimage_sink->pool = gst_gl_buffer_pool_new (glimage_sink->context);
      config = gst_buffer_pool_get_config (glimage_sink->pool);
      gst_buffer_pool_config_set_params (config, caps, size, 0, 0);

      if (!gst_buffer_pool_set_config (glimage_sink->pool, config))
        goto config_failed;
    }

    /* we need at least 2 buffer because we hold on to the last one */
    gst_query_add_allocation_pool (query, glimage_sink->pool, size, 2, 0);
  }

  if (glimage_sink->context->gl_vtable->FenceSync)
    gst_query_add_allocation_meta (query, GST_GL_SYNC_META_API_TYPE, 0);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_WARNING_OBJECT (bsink, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_WARNING_OBJECT (bsink, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_WARNING_OBJECT (bsink, "failed setting config");
    return FALSE;
  }
}

/* *INDENT-OFF* */
static const GLfloat vertices[] = {
     1.0f,  1.0f, 0.0f, 1.0f, 0.0f,
    -1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
    -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
     1.0f, -1.0f, 0.0f, 1.0f, 1.0f
};

static const GLushort indices[] = { 0, 1, 2, 0, 2, 3 };
/* *INDENT-ON* */

static void
_bind_buffer (GstGLImageSink * gl_sink)
{
  const GstGLFuncs *gl = gl_sink->context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, gl_sink->vbo_indices);
  gl->BindBuffer (GL_ARRAY_BUFFER, gl_sink->vertex_buffer);

  /* Load the vertex position */
  gl->VertexAttribPointer (gl_sink->attr_position, 3, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) 0);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (gl_sink->attr_texture, 2, GL_FLOAT, GL_FALSE,
      5 * sizeof (GLfloat), (void *) (3 * sizeof (GLfloat)));

  gl->EnableVertexAttribArray (gl_sink->attr_position);
  gl->EnableVertexAttribArray (gl_sink->attr_texture);
}

static void
_unbind_buffer (GstGLImageSink * gl_sink)
{
  const GstGLFuncs *gl = gl_sink->context->gl_vtable;

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);

  gl->DisableVertexAttribArray (gl_sink->attr_position);
  gl->DisableVertexAttribArray (gl_sink->attr_texture);
}

/* Called in the gl thread */
static void
gst_glimage_sink_thread_init_redisplay (GstGLImageSink * gl_sink)
{
  const GstGLFuncs *gl = gl_sink->context->gl_vtable;

  gl_sink->redisplay_shader = gst_gl_shader_new (gl_sink->context);

  if (!gst_gl_shader_compile_with_default_vf_and_check
      (gl_sink->redisplay_shader, &gl_sink->attr_position,
          &gl_sink->attr_texture))
    gst_glimage_sink_cleanup_glthread (gl_sink);

  if (gl->GenVertexArrays) {
    gl->GenVertexArrays (1, &gl_sink->vao);
    gl->BindVertexArray (gl_sink->vao);
  }

  if (!gl_sink->vertex_buffer) {
    gl->GenBuffers (1, &gl_sink->vertex_buffer);
    gl->BindBuffer (GL_ARRAY_BUFFER, gl_sink->vertex_buffer);
    gl->BufferData (GL_ARRAY_BUFFER, 4 * 5 * sizeof (GLfloat), vertices,
        GL_STATIC_DRAW);
  }

  if (!gl_sink->vbo_indices) {
    gl->GenBuffers (1, &gl_sink->vbo_indices);
    gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, gl_sink->vbo_indices);
    gl->BufferData (GL_ELEMENT_ARRAY_BUFFER, sizeof (indices), indices,
        GL_STATIC_DRAW);
  }

  if (gl->GenVertexArrays) {
    _bind_buffer (gl_sink);
    gl->BindVertexArray (0);
  }

  gl->BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
  gl->BindBuffer (GL_ARRAY_BUFFER, 0);
}

static void
gst_glimage_sink_cleanup_glthread (GstGLImageSink * gl_sink)
{
  const GstGLFuncs *gl = gl_sink->context->gl_vtable;

  if (gl_sink->redisplay_shader) {
    gst_object_unref (gl_sink->redisplay_shader);
    gl_sink->redisplay_shader = NULL;
  }

  if (gl_sink->vao) {
    gl->DeleteVertexArrays (1, &gl_sink->vao);
    gl_sink->vao = 0;
  }

  if (gl_sink->vertex_buffer) {
    gl->DeleteBuffers (1, &gl_sink->vertex_buffer);
    gl_sink->vertex_buffer = 0;
  }

  if (gl_sink->vbo_indices) {
    gl->DeleteBuffers (1, &gl_sink->vbo_indices);
    gl_sink->vbo_indices = 0;
  }
}

static void
gst_glimage_sink_on_resize (GstGLImageSink * gl_sink, gint width, gint height)
{
  /* Here gl_sink members (ex:gl_sink->info) have a life time of set_caps.
   * It means that they cannot not change between two set_caps
   */
  const GstGLFuncs *gl = gl_sink->context->gl_vtable;
  gboolean do_reshape;

  GST_TRACE ("GL Window resized to %ux%u", width, height);

  /* check if a client reshape callback is registered */
  g_signal_emit (gl_sink, gst_glimage_sink_signals[CLIENT_RESHAPE_SIGNAL], 0,
      gl_sink->context, width, height, &do_reshape);

  width = MAX (1, width);
  height = MAX (1, height);

  gl_sink->window_width = width;
  gl_sink->window_height = height;

  /* default reshape */
  if (!do_reshape) {
    if (gl_sink->keep_aspect_ratio) {
      GstVideoRectangle src, dst, result;

      src.x = 0;
      src.y = 0;
      src.w = GST_VIDEO_SINK_WIDTH (gl_sink);
      src.h = GST_VIDEO_SINK_HEIGHT (gl_sink);

      dst.x = 0;
      dst.y = 0;
      dst.w = width;
      dst.h = height;

      gst_video_sink_center_rect (src, dst, &result, TRUE);
      gl->Viewport (result.x, result.y, result.w, result.h);
    } else {
      gl->Viewport (0, 0, width, height);
    }
  }
}

static void
gst_glimage_sink_on_draw (GstGLImageSink * gl_sink)
{
  /* Here gl_sink members (ex:gl_sink->info) have a life time of set_caps.
   * It means that they cannot not change between two set_caps as well as
   * for the redisplay_texture size.
   * Whereas redisplay_texture id changes every sink_render
   */

  const GstGLFuncs *gl = NULL;
  GstGLWindow *window = NULL;
  gboolean do_redisplay = FALSE;
  GstGLSyncMeta *sync_meta = NULL;
  GstSample *sample = NULL;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (gl_sink));

  gl = gl_sink->context->gl_vtable;

  GST_GLIMAGE_SINK_LOCK (gl_sink);

  /* check if texture is ready for being drawn */
  if (!gl_sink->redisplay_texture) {
    GST_GLIMAGE_SINK_UNLOCK (gl_sink);
    return;
  }

  window = gst_gl_context_get_window (gl_sink->context);
  window->is_drawing = TRUE;

  /* opengl scene */
  GST_TRACE ("redrawing texture:%u", gl_sink->redisplay_texture);

  if (gl_sink->caps_change && gl_sink->window_width > 0
      && gl_sink->window_height > 0) {
    GST_GLIMAGE_SINK_UNLOCK (gl_sink);
    gst_glimage_sink_on_resize (gl_sink, gl_sink->window_width,
        gl_sink->window_height);
    GST_GLIMAGE_SINK_LOCK (gl_sink);
    gl_sink->caps_change = FALSE;
  }

  sync_meta = gst_buffer_get_gl_sync_meta (gl_sink->stored_sync);
  if (sync_meta)
    gst_gl_sync_meta_wait (sync_meta, gst_gl_context_get_current ());

  /* make sure that the environnement is clean */
  gst_gl_context_clear_shader (gl_sink->context);
  gl->BindTexture (GL_TEXTURE_2D, 0);
#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (gl_sink->context))
    gl->Disable (GL_TEXTURE_2D);
#endif

  sample = gst_sample_new (gl_sink->stored_buffer,
      gl_sink->caps, &GST_BASE_SINK (gl_sink)->segment, NULL);

  g_signal_emit (gl_sink, gst_glimage_sink_signals[CLIENT_DRAW_SIGNAL], 0,
      gl_sink->context, sample, &do_redisplay);

  gst_sample_unref (sample);

  if (!do_redisplay) {
    gfloat alpha = gl_sink->ignore_alpha ? 1.0f : 0.0f;

    gl->ClearColor (0.0, 0.0, 0.0, alpha);
    gl->Clear (GL_COLOR_BUFFER_BIT);

    if (gl_sink->ignore_alpha) {
      gl->BlendColor (0.0, 0.0, 0.0, alpha);
      gl->BlendFunc (GL_SRC_ALPHA, GL_CONSTANT_COLOR);
      gl->BlendEquation (GL_FUNC_ADD);
      gl->Enable (GL_BLEND);
    }

    gst_gl_shader_use (gl_sink->redisplay_shader);

    if (gl->GenVertexArrays)
      gl->BindVertexArray (gl_sink->vao);
    else
      _bind_buffer (gl_sink);

    gl->ActiveTexture (GL_TEXTURE0);
    gl->BindTexture (GL_TEXTURE_2D, gl_sink->redisplay_texture);
    gst_gl_shader_set_uniform_1i (gl_sink->redisplay_shader, "tex", 0);

    gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, 0);

    gst_gl_context_clear_shader (gl_sink->context);

    if (gl->GenVertexArrays)
      gl->BindVertexArray (0);
    else
      _unbind_buffer (gl_sink);

    if (gl_sink->ignore_alpha)
      gl->Disable (GL_BLEND);
  }
  /* end default opengl scene */
  window->is_drawing = FALSE;
  gst_object_unref (window);

  GST_GLIMAGE_SINK_UNLOCK (gl_sink);
}

static void
gst_glimage_sink_on_close (GstGLImageSink * gl_sink)
{
  GstGLWindow *window;

  gst_gl_context_set_error (gl_sink->context, "Output window was closed");

  window = gst_gl_context_get_window (gl_sink->context);

  if (gl_sink->key_sig_id)
    g_signal_handler_disconnect (window, gl_sink->key_sig_id);
  gl_sink->key_sig_id = 0;
  if (gl_sink->mouse_sig_id)
    g_signal_handler_disconnect (window, gl_sink->mouse_sig_id);
  gl_sink->mouse_sig_id = 0;

  g_atomic_int_set (&gl_sink->to_quit, 1);

  gst_object_unref (window);
}

static gboolean
gst_glimage_sink_redisplay (GstGLImageSink * gl_sink)
{
  GstGLWindow *window;
  gboolean alive;

  window = gst_gl_context_get_window (gl_sink->context);
  if (!window)
    return FALSE;

  if (gst_gl_window_is_running (window)) {
    gulong handler_id =
        g_signal_handler_find (GST_ELEMENT_PARENT (gl_sink), G_SIGNAL_MATCH_ID,
        gst_gl_image_sink_bin_signals[SIGNAL_BIN_CLIENT_DRAW], 0,
        NULL, NULL, NULL);

    if (G_UNLIKELY (!gl_sink->redisplay_shader) && (!handler_id
            || !gl_sink->other_context)) {
      gst_gl_window_send_message (window,
          GST_GL_WINDOW_CB (gst_glimage_sink_thread_init_redisplay), gl_sink);

      /* if the shader is still null it means it failed to be useable */
      if (G_UNLIKELY (!gl_sink->redisplay_shader)) {
        gst_object_unref (window);
        return FALSE;
      }

      gst_gl_window_set_preferred_size (window, GST_VIDEO_SINK_WIDTH (gl_sink),
          GST_VIDEO_SINK_HEIGHT (gl_sink));
      gst_gl_window_show (window);
    }

    /* Drawing is asynchronous: gst_gl_window_draw is not blocking
     * It means that it does not wait for stuff to be executed in other threads
     */
    gst_gl_window_draw (window);
  }
  alive = gst_gl_window_is_running (window);
  gst_object_unref (window);

  return alive;
}
