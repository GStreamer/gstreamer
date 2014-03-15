/*
 * GStreamer
 * Copyright (C) 2003 Julien Moutte <julien@moutte.net>
 * Copyright (C) 2005,2006,2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
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
 * gst-launch -v videotestsrc ! "video/x-raw-rgb" ! glimagesink
 * ]| A pipeline to test hardware scaling.
 * No special opengl extension is used in this pipeline, that's why it should work
 * with OpenGL >= 1.1. That's the case if you are using the MESA3D driver v1.3.
 * |[
 * gst-launch -v videotestsrc ! "video/x-raw-yuv, format=(fourcc)I420" ! glimagesink
 * ]| A pipeline to test hardware scaling and hardware colorspace conversion.
 * When your driver supports GLSL (OpenGL Shading Language needs OpenGL >= 2.1),
 * the 4 following format YUY2, UYVY, I420, YV12 and AYUV are converted to RGB32
 * through some fragment shaders and using one framebuffer (FBO extension OpenGL >= 1.4).
 * If your driver does not support GLSL but supports MESA_YCbCr extension then
 * the you can use YUY2 and UYVY. In this case the colorspace conversion is automatically
 * made when loading the texture and therefore no framebuffer is used.
 * |[
 * gst-launch -v gltestsrc ! glimagesink
 * ]| A pipeline 100% OpenGL.
 * No special opengl extension is used in this pipeline, that's why it should work
 * with OpenGL >= 1.1. That's the case if you are using the MESA3D driver v1.3.
 * |[
 * gst-plugins-gl/tests/examples/generic/cube
 * ]| The graphic FPS scene can be greater than the input video FPS.
 * The graphic scene can be written from a client code through the
 * two glfilterapp properties.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/video/videooverlay.h>

#include "gstglimagesink.h"

GST_DEBUG_CATEGORY (gst_debug_glimage_sink);
#define GST_CAT_DEFAULT gst_debug_glimage_sink

#define GST_GLIMAGE_SINK_GET_LOCK(glsink) \
  (GST_GLIMAGE_SINK(glsink)->drawing_lock)
#define GST_GLIMAGE_SINK_LOCK(glsink) \
  (g_mutex_lock(&GST_GLIMAGE_SINK_GET_LOCK (glsink)))
#define GST_GLIMAGE_SINK_UNLOCK(glsink) \
  (g_mutex_unlock(&GST_GLIMAGE_SINK_GET_LOCK (glsink)))

#define USING_OPENGL(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL)
#define USING_OPENGL3(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL3)
#define USING_GLES(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES)
#define USING_GLES2(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES2)
#define USING_GLES3(context) (gst_gl_context_get_gl_api (context) & GST_GL_API_GLES3)

#if GST_GL_HAVE_GLES2
static void gst_glimage_sink_thread_init_redisplay (GstGLImageSink * gl_sink);
#endif
static void gst_glimage_sink_on_close (GstGLImageSink * gl_sink);
static void gst_glimage_sink_on_resize (const GstGLImageSink * gl_sink,
    gint width, gint height);
static void gst_glimage_sink_on_draw (const GstGLImageSink * gl_sink);
static gboolean gst_glimage_sink_redisplay (GstGLImageSink * gl_sink);

static void gst_glimage_sink_finalize (GObject * object);
static void gst_glimage_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_glimage_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static gboolean gst_glimage_sink_stop (GstBaseSink * bsink);

static gboolean gst_glimage_sink_query (GstBaseSink * bsink, GstQuery * query);
static void gst_glimage_sink_set_context (GstElement * element,
    GstContext * context);

static GstStateChangeReturn
gst_glimage_sink_change_state (GstElement * element, GstStateChange transition);

static void gst_glimage_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_glimage_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstFlowReturn gst_glimage_sink_render (GstBaseSink * bsink,
    GstBuffer * buf);
static gboolean gst_glimage_sink_propose_allocation (GstBaseSink * bsink,
    GstQuery * query);

static void gst_glimage_sink_video_overlay_init (GstVideoOverlayInterface *
    iface);
static void gst_glimage_sink_set_window_handle (GstVideoOverlay * overlay,
    guintptr id);
static void gst_glimage_sink_expose (GstVideoOverlay * overlay);


#if GST_GL_HAVE_GLES2
/* *INDENT-OFF* */
static const gchar *redisplay_vertex_shader_str_gles2 =
      "attribute vec4 a_position;   \n"
      "attribute vec2 a_texCoord;   \n"
      "varying vec2 v_texCoord;     \n"
      "void main()                  \n"
      "{                            \n"
      "   gl_Position = a_position; \n"
      "   v_texCoord = a_texCoord;  \n"
      "}                            \n";

static const gchar *redisplay_fragment_shader_str_gles2 =
      "precision mediump float;                            \n"
      "varying vec2 v_texCoord;                            \n"
      "uniform sampler2D s_texture;                        \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  gl_FragColor = texture2D( s_texture, v_texCoord );\n"
      "}                                                   \n";
/* *INDENT-ON* */
#endif

static GstStaticPadTemplate gst_glimage_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE (GST_GL_UPLOAD_FORMATS) "; "
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META,
            GST_GL_UPLOAD_FORMATS))
    );

enum
{
  ARG_0,
  ARG_DISPLAY,
  PROP_CLIENT_RESHAPE_CALLBACK,
  PROP_CLIENT_DRAW_CALLBACK,
  PROP_CLIENT_DATA,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO,
  PROP_OTHER_CONTEXT
};

#define gst_glimage_sink_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLImageSink, gst_glimage_sink,
    GST_TYPE_VIDEO_SINK, G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_glimage_sink_video_overlay_init);
    GST_DEBUG_CATEGORY_INIT (gst_debug_glimage_sink, "glimagesink", 0,
        "OpenGL Video Sink"));

static void
gst_glimage_sink_class_init (GstGLImageSinkClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;
  GstBaseSinkClass *gstbasesink_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;
  gstbasesink_class = (GstBaseSinkClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_glimage_sink_set_property;
  gobject_class->get_property = gst_glimage_sink_get_property;

  g_object_class_install_property (gobject_class, ARG_DISPLAY,
      g_param_spec_string ("display", "Display", "Display name",
          NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CLIENT_RESHAPE_CALLBACK,
      g_param_spec_pointer ("client-reshape-callback",
          "Client reshape callback",
          "Define a custom reshape callback in a client code",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CLIENT_DRAW_CALLBACK,
      g_param_spec_pointer ("client-draw-callback", "Client draw callback",
          "Define a custom draw callback in a client code",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_CLIENT_DATA,
      g_param_spec_pointer ("client-data", "Client data",
          "Pass data to the draw and reshape callbacks",
          G_PARAM_WRITABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FORCE_ASPECT_RATIO,
      g_param_spec_boolean ("force-aspect-ratio",
          "Force aspect ratio",
          "When enabled, scaling will respect original aspect ratio", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PIXEL_ASPECT_RATIO,
      gst_param_spec_fraction ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", 0, 1, G_MAXINT, 1, 0, 1,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "OpenGL video sink",
      "Sink/Video", "A videosink based on OpenGL",
      "Julien Isorce <julien.isorce@gmail.com>");

  g_object_class_install_property (gobject_class, PROP_OTHER_CONTEXT,
      g_param_spec_object ("other-context",
          "External OpenGL context",
          "Give an external OpenGL context with which to share textures",
          GST_GL_TYPE_CONTEXT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_glimage_sink_template));

  gobject_class->finalize = gst_glimage_sink_finalize;

  gstelement_class->change_state = gst_glimage_sink_change_state;
  gstelement_class->set_context = gst_glimage_sink_set_context;
  gstbasesink_class->query = GST_DEBUG_FUNCPTR (gst_glimage_sink_query);
  gstbasesink_class->set_caps = gst_glimage_sink_set_caps;
  gstbasesink_class->get_times = gst_glimage_sink_get_times;
  gstbasesink_class->preroll = gst_glimage_sink_render;
  gstbasesink_class->render = gst_glimage_sink_render;
  gstbasesink_class->propose_allocation = gst_glimage_sink_propose_allocation;
  gstbasesink_class->stop = gst_glimage_sink_stop;
}

static void
gst_glimage_sink_init (GstGLImageSink * glimage_sink)
{
  glimage_sink->display_name = NULL;
  glimage_sink->window_id = 0;
  glimage_sink->new_window_id = 0;
  glimage_sink->display = NULL;
  glimage_sink->clientReshapeCallback = NULL;
  glimage_sink->clientDrawCallback = NULL;
  glimage_sink->client_data = NULL;
  glimage_sink->keep_aspect_ratio = FALSE;
  glimage_sink->par_n = 0;
  glimage_sink->par_d = 1;
  glimage_sink->pool = NULL;
  glimage_sink->redisplay_texture = 0;

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
    case ARG_DISPLAY:
    {
      g_free (glimage_sink->display_name);
      glimage_sink->display_name = g_strdup (g_value_get_string (value));
      break;
    }
    case PROP_CLIENT_RESHAPE_CALLBACK:
    {
      glimage_sink->clientReshapeCallback = g_value_get_pointer (value);
      break;
    }
    case PROP_CLIENT_DRAW_CALLBACK:
    {
      glimage_sink->clientDrawCallback = g_value_get_pointer (value);
      break;
    }
    case PROP_CLIENT_DATA:
    {
      glimage_sink->client_data = g_value_get_pointer (value);
      break;
    }
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
    case PROP_OTHER_CONTEXT:
    {
      if (glimage_sink->other_context)
        gst_object_unref (glimage_sink->other_context);
      glimage_sink->other_context = g_value_dup_object (value);
      break;
    }
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

  g_free (glimage_sink->display_name);

  GST_DEBUG ("finalized");
}

static void
gst_glimage_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLImageSink *glimage_sink;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (object));

  glimage_sink = GST_GLIMAGE_SINK (object);

  switch (prop_id) {
    case ARG_DISPLAY:
      g_value_set_string (value, glimage_sink->display_name);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, glimage_sink->keep_aspect_ratio);
      break;
    case PROP_PIXEL_ASPECT_RATIO:
      gst_value_set_fraction (value, glimage_sink->par_n, glimage_sink->par_d);
      break;
    case PROP_OTHER_CONTEXT:
      g_value_set_object (value, glimage_sink->other_context);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
_ensure_gl_setup (GstGLImageSink * gl_sink)
{
  GError *error = NULL;

  if (!gst_gl_ensure_display (gl_sink, &gl_sink->display))
    return FALSE;

  if (!gl_sink->context) {
    gl_sink->context = gst_gl_context_new (gl_sink->display);
    if (!gst_gl_context_create (gl_sink->context, gl_sink->other_context,
            &error))
      goto context_error;
  }

  if (!gl_sink->upload) {
    gl_sink->upload = gst_gl_upload_new (gl_sink->context);
    if (!gst_gl_upload_init_format (gl_sink->upload, gl_sink->info,
            gl_sink->info))
      goto upload_error;
  }

  return TRUE;

upload_error:
  {
    GST_ELEMENT_ERROR (gl_sink, RESOURCE, NOT_FOUND, ("Failed to init upload"),
        (NULL));
    return FALSE;
  }
context_error:
  {
    GST_ELEMENT_ERROR (gl_sink, RESOURCE, NOT_FOUND, ("%s", error->message),
        (NULL));
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
      return gst_gl_handle_context_query ((GstElement *) glimage_sink, query,
          &glimage_sink->display);
      break;
    }
    default:
      res = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
  }

  return res;
}

static void
gst_glimage_sink_cleanup_glthread (GstGLImageSink * gl_sink)
{
#if GST_GL_HAVE_GLES2
  if (gl_sink->redisplay_shader) {
    gst_object_unref (gl_sink->redisplay_shader);
    gl_sink->redisplay_shader = NULL;
  }
#endif
}

/*
 * GstElement methods
 */

static gboolean
gst_glimage_sink_stop (GstBaseSink * bsink)
{
  GstGLImageSink *glimage_sink = GST_GLIMAGE_SINK (bsink);

  if (glimage_sink->pool) {
    gst_object_unref (glimage_sink->pool);
    glimage_sink->pool = NULL;
  }

  return TRUE;
}

static void
gst_glimage_sink_set_context (GstElement * element, GstContext * context)
{
  GstGLImageSink *gl_sink = GST_GLIMAGE_SINK (element);

  gst_gl_handle_set_context (element, context, &gl_sink->display);
}

static GstStateChangeReturn
gst_glimage_sink_change_state (GstElement * element, GstStateChange transition)
{
  GstGLImageSink *glimage_sink;
  GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

  GST_DEBUG ("change state");

  glimage_sink = GST_GLIMAGE_SINK (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      g_atomic_int_set (&glimage_sink->to_quit, 0);
      if (!glimage_sink->display) {
        GstGLWindow *window;
        GError *error = NULL;

        if (!gst_gl_ensure_display (glimage_sink, &glimage_sink->display))
          return GST_STATE_CHANGE_FAILURE;

        glimage_sink->context = gst_gl_context_new (glimage_sink->display);
        window = gst_gl_context_get_window (glimage_sink->context);

        if (!glimage_sink->window_id && !glimage_sink->new_window_id)
          gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY
              (glimage_sink));

        if (glimage_sink->window_id != glimage_sink->new_window_id) {
          glimage_sink->window_id = glimage_sink->new_window_id;
          gst_gl_window_set_window_handle (window, glimage_sink->window_id);
        }

        if (!gst_gl_context_create (glimage_sink->context,
                glimage_sink->other_context, &error)) {
          GST_ELEMENT_ERROR (glimage_sink, RESOURCE, NOT_FOUND, ("%s",
                  error->message), (NULL));

          if (glimage_sink->display) {
            gst_object_unref (glimage_sink->display);
            glimage_sink->display = NULL;
          }
          gst_object_unref (glimage_sink->context);
          gst_object_unref (window);

          return GST_STATE_CHANGE_FAILURE;
        }

        /* setup callbacks */
        gst_gl_window_set_resize_callback (window,
            GST_GL_WINDOW_RESIZE_CB (gst_glimage_sink_on_resize),
            gst_object_ref (glimage_sink), (GDestroyNotify) gst_object_unref);
        gst_gl_window_set_draw_callback (window,
            GST_GL_WINDOW_CB (gst_glimage_sink_on_draw),
            gst_object_ref (glimage_sink), (GDestroyNotify) gst_object_unref);
        gst_gl_window_set_close_callback (window,
            GST_GL_WINDOW_CB (gst_glimage_sink_on_close),
            gst_object_ref (glimage_sink), (GDestroyNotify) gst_object_unref);

        gst_object_unref (window);
      }
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
      glimage_sink->redisplay_texture = 0;
      GST_GLIMAGE_SINK_UNLOCK (glimage_sink);

      if (glimage_sink->upload) {
        gst_object_unref (glimage_sink->upload);
        glimage_sink->upload = NULL;
      }

      glimage_sink->window_id = 0;
      //but do not reset glimage_sink->new_window_id

      if (glimage_sink->pool) {
        gst_buffer_pool_set_active (glimage_sink->pool, FALSE);
        gst_object_unref (glimage_sink->pool);
        glimage_sink->pool = NULL;
      }

      GST_VIDEO_SINK_WIDTH (glimage_sink) = 1;
      GST_VIDEO_SINK_HEIGHT (glimage_sink) = 1;
      if (glimage_sink->context) {
        GstGLWindow *window = gst_gl_context_get_window (glimage_sink->context);

        gst_gl_window_send_message (window,
            GST_GL_WINDOW_CB (gst_glimage_sink_cleanup_glthread), glimage_sink);

        gst_gl_window_set_resize_callback (window, NULL, NULL, NULL);
        gst_gl_window_set_draw_callback (window, NULL, NULL, NULL);
        gst_gl_window_set_close_callback (window, NULL, NULL, NULL);

        gst_object_unref (window);
        gst_object_unref (glimage_sink->context);
        glimage_sink->context = NULL;
        gst_object_unref (glimage_sink->display);
        glimage_sink->display = NULL;
      }
      break;
    }
    case GST_STATE_CHANGE_READY_TO_NULL:
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
  GstStructure *structure;
  GstBufferPool *newpool, *oldpool;

  GST_DEBUG ("set caps with %" GST_PTR_FORMAT, caps);

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  ok = gst_video_info_from_caps (&vinfo, caps);
  if (!ok)
    return FALSE;

  width = GST_VIDEO_INFO_WIDTH (&vinfo);
  height = GST_VIDEO_INFO_HEIGHT (&vinfo);

  if (glimage_sink->tex_id)
    gst_gl_context_del_texture (glimage_sink->context, &glimage_sink->tex_id);
  //FIXME: this texture seems to be never deleted when going to STATE_NULL
  gst_gl_context_gen_texture (glimage_sink->context, &glimage_sink->tex_id,
      GST_VIDEO_INFO_FORMAT (&vinfo), width, height);

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

  newpool = gst_gl_buffer_pool_new (glimage_sink->context);
  structure = gst_buffer_pool_get_config (newpool);
  gst_buffer_pool_config_set_params (structure, caps, vinfo.size, 2, 0);
  gst_buffer_pool_set_config (newpool, structure);

  oldpool = glimage_sink->pool;
  /* we don't activate the pool yet, this will be done by downstream after it
   * has configured the pool. If downstream does not want our pool we will
   * activate it when we render into it */
  glimage_sink->pool = newpool;

  /* unref the old sink */
  if (oldpool) {
    /* we don't deactivate, some elements might still be using it, it will
     * be deactivated when the last ref is gone */
    gst_object_unref (oldpool);
  }

  return TRUE;
}

static GstFlowReturn
gst_glimage_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstGLImageSink *glimage_sink;
  guint tex_id;

  GST_TRACE ("rendering buffer:%p", buf);

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  if (GST_VIDEO_SINK_WIDTH (glimage_sink) < 1 ||
      GST_VIDEO_SINK_HEIGHT (glimage_sink) < 1) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (!_ensure_gl_setup (glimage_sink))
    return GST_FLOW_NOT_NEGOTIATED;

  if (!gst_gl_upload_perform_with_buffer (glimage_sink->upload, buf, &tex_id))
    goto upload_failed;

  if (glimage_sink->window_id != glimage_sink->new_window_id) {
    GstGLWindow *window = gst_gl_context_get_window (glimage_sink->context);

    glimage_sink->window_id = glimage_sink->new_window_id;
    gst_gl_window_set_window_handle (window, glimage_sink->window_id);

    gst_object_unref (window);
  }

  GST_TRACE ("redisplay texture:%u of size:%ux%u, window size:%ux%u", tex_id,
      GST_VIDEO_INFO_WIDTH (&glimage_sink->info),
      GST_VIDEO_INFO_HEIGHT (&glimage_sink->info),
      GST_VIDEO_SINK_WIDTH (glimage_sink),
      GST_VIDEO_SINK_HEIGHT (glimage_sink));

  /* Avoid to release the texture while drawing */
  GST_GLIMAGE_SINK_LOCK (glimage_sink);
  glimage_sink->redisplay_texture = tex_id;
  GST_GLIMAGE_SINK_UNLOCK (glimage_sink);

  /* Ask the underlying window to redraw its content */
  if (!gst_glimage_sink_redisplay (glimage_sink))
    goto redisplay_failed;

  GST_TRACE ("post redisplay");

  if (g_atomic_int_get (&glimage_sink->to_quit) != 0) {
    GST_ELEMENT_ERROR (glimage_sink, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    gst_gl_upload_release_buffer (glimage_sink->upload);
    return GST_FLOW_ERROR;
  }

  gst_gl_upload_release_buffer (glimage_sink->upload);
  return GST_FLOW_OK;

/* ERRORS */
redisplay_failed:
  {
    gst_gl_upload_release_buffer (glimage_sink->upload);
    GST_ELEMENT_ERROR (glimage_sink, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return GST_FLOW_ERROR;
  }

upload_failed:
  {
    GST_ELEMENT_ERROR (glimage_sink, RESOURCE, NOT_FOUND,
        ("%s", "Failed to upload format"), (NULL));
    return FALSE;
  }
}


static void
gst_glimage_sink_video_overlay_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_glimage_sink_set_window_handle;
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

static gboolean
gst_glimage_sink_propose_allocation (GstBaseSink * bsink, GstQuery * query)
{
  GstGLImageSink *glimage_sink = GST_GLIMAGE_SINK (bsink);
  GstBufferPool *pool;
  GstStructure *config;
  GstCaps *caps;
  guint size;
  gboolean need_pool;
  GstStructure *gl_context;
  gchar *platform, *gl_apis;
  gpointer handle;

  if (!_ensure_gl_setup (glimage_sink))
    return FALSE;

  gst_query_parse_allocation (query, &caps, &need_pool);

  if (caps == NULL)
    goto no_caps;

  if ((pool = glimage_sink->pool))
    gst_object_ref (pool);

  if (pool != NULL) {
    GstCaps *pcaps;

    /* we had a pool, check caps */
    GST_DEBUG_OBJECT (glimage_sink, "check existing pool caps");
    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_get_params (config, &pcaps, &size, NULL, NULL);

    if (!gst_caps_is_equal (caps, pcaps)) {
      GST_DEBUG_OBJECT (glimage_sink, "pool has different caps");
      /* different caps, we can't use this pool */
      gst_object_unref (pool);
      pool = NULL;
    }
    gst_structure_free (config);
  }

  if (pool == NULL && need_pool) {
    GstVideoInfo info;

    if (!gst_video_info_from_caps (&info, caps))
      goto invalid_caps;

    GST_DEBUG_OBJECT (glimage_sink, "create new pool");
    pool = gst_gl_buffer_pool_new (glimage_sink->context);

    /* the normal size of a frame */
    size = info.size;

    config = gst_buffer_pool_get_config (pool);
    gst_buffer_pool_config_set_params (config, caps, size, 0, 0);
    if (!gst_buffer_pool_set_config (pool, config))
      goto config_failed;
  }
  /* we need at least 2 buffer because we hold on to the last one */
  gst_query_add_allocation_pool (query, pool, size, 2, 0);

  /* we also support various metadata */
  gst_query_add_allocation_meta (query, GST_VIDEO_META_API_TYPE, 0);

  gst_object_unref (pool);

  gl_apis =
      gst_gl_api_to_string (gst_gl_context_get_gl_api (glimage_sink->context));
  platform =
      gst_gl_platform_to_string (gst_gl_context_get_gl_platform
      (glimage_sink->context));
  handle = (gpointer) gst_gl_context_get_gl_context (glimage_sink->context);

  gl_context =
      gst_structure_new ("GstVideoGLTextureUploadMeta", "gst.gl.GstGLContext",
      GST_GL_TYPE_CONTEXT, glimage_sink->context, "gst.gl.context.handle",
      G_TYPE_POINTER, handle, "gst.gl.context.type", G_TYPE_STRING, platform,
      "gst.gl.context.apis", G_TYPE_STRING, gl_apis, NULL);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, gl_context);

  g_free (gl_apis);
  g_free (platform);
  gst_structure_free (gl_context);

  return TRUE;

  /* ERRORS */
no_caps:
  {
    GST_DEBUG_OBJECT (bsink, "no caps specified");
    return FALSE;
  }
invalid_caps:
  {
    GST_DEBUG_OBJECT (bsink, "invalid caps specified");
    return FALSE;
  }
config_failed:
  {
    GST_DEBUG_OBJECT (bsink, "failed setting config");
    return FALSE;
  }
}

#if GST_GL_HAVE_GLES2
/* Called in the gl thread */
static void
gst_glimage_sink_thread_init_redisplay (GstGLImageSink * gl_sink)
{
  GError *error = NULL;
  gl_sink->redisplay_shader = gst_gl_shader_new (gl_sink->context);

  gst_gl_shader_set_vertex_source (gl_sink->redisplay_shader,
      redisplay_vertex_shader_str_gles2);
  gst_gl_shader_set_fragment_source (gl_sink->redisplay_shader,
      redisplay_fragment_shader_str_gles2);

  gst_gl_shader_compile (gl_sink->redisplay_shader, &error);
  if (error) {
    gst_gl_context_set_error (gl_sink->context, "%s", error->message);
    g_error_free (error);
    error = NULL;
    gst_gl_context_clear_shader (gl_sink->context);
  } else {
    gl_sink->redisplay_attr_position_loc =
        gst_gl_shader_get_attribute_location (gl_sink->redisplay_shader,
        "a_position");
    gl_sink->redisplay_attr_texture_loc =
        gst_gl_shader_get_attribute_location (gl_sink->redisplay_shader,
        "a_texCoord");
  }
}
#endif

static void
gst_glimage_sink_on_resize (const GstGLImageSink * gl_sink, gint width,
    gint height)
{
  /* Here gl_sink members (ex:gl_sink->info) have a life time of set_caps.
   * It means that they cannot not change between two set_caps
   */
  const GstGLFuncs *gl = gl_sink->context->gl_vtable;

  GST_TRACE ("GL Window resized to %ux%u", width, height);

  /* check if a client reshape callback is registered */
  if (gl_sink->clientReshapeCallback)
    gl_sink->clientReshapeCallback (width, height, gl_sink->client_data);

  /* default reshape */
  else {
    if (gl_sink->keep_aspect_ratio) {
      GstVideoRectangle src, dst, result;

      src.x = 0;
      src.y = 0;
      src.w = GST_VIDEO_INFO_WIDTH (&gl_sink->info);
      src.h = GST_VIDEO_INFO_HEIGHT (&gl_sink->info);

      dst.x = 0;
      dst.y = 0;
      dst.w = width;
      dst.h = height;

      gst_video_sink_center_rect (src, dst, &result, TRUE);
      gl->Viewport (result.x, result.y, result.w, result.h);
    } else {
      gl->Viewport (0, 0, width, height);
    }
#if GST_GL_HAVE_OPENGL
    if (USING_OPENGL (gl_sink->context)) {
      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();
      gluOrtho2D (0, width, 0, height);
      gl->MatrixMode (GL_MODELVIEW);
    }
#endif
  }
}


static void
gst_glimage_sink_on_draw (const GstGLImageSink * gl_sink)
{
  /* Here gl_sink members (ex:gl_sink->info) have a life time of set_caps.
   * It means that they cannot not change between two set_caps as well as
   * for the redisplay_texture size.
   * Whereas redisplay_texture id changes every sink_render
   */

  const GstGLFuncs *gl = NULL;
  GstGLWindow *window = NULL;

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

  /* make sure that the environnement is clean */
  gst_gl_context_clear_shader (gl_sink->context);

#if GST_GL_HAVE_OPENGL
  if (USING_OPENGL (gl_sink->context))
    gl->Disable (GL_TEXTURE_2D);
#endif

  gl->BindTexture (GL_TEXTURE_2D, 0);

  /* check if a client draw callback is registered */
  if (gl_sink->clientDrawCallback) {

    gboolean doRedisplay =
        gl_sink->clientDrawCallback (gl_sink->redisplay_texture,
        GST_VIDEO_INFO_WIDTH (&gl_sink->info),
        GST_VIDEO_INFO_HEIGHT (&gl_sink->info),
        gl_sink->client_data);

    if (doRedisplay)
      gst_gl_window_draw_unlocked (window,
          GST_VIDEO_INFO_WIDTH (&gl_sink->info),
          GST_VIDEO_INFO_HEIGHT (&gl_sink->info));
  }
  /* default opengl scene */
  else {
#if GST_GL_HAVE_OPENGL
    if (USING_OPENGL (gl_sink->context)) {
      GLfloat verts[8] = { 1.0f, 1.0f,
        -1.0f, 1.0f,
        -1.0f, -1.0f,
        1.0f, -1.0f
      };
      GLfloat texcoords[8] = { 1.0f, 0.0f,
        0.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 1.0f
      };
      gl->Clear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

      gl->MatrixMode (GL_PROJECTION);
      gl->LoadIdentity ();

      gl->Enable (GL_TEXTURE_2D);
      gl->BindTexture (GL_TEXTURE_2D, gl_sink->redisplay_texture);

      gl->EnableClientState (GL_VERTEX_ARRAY);
      gl->EnableClientState (GL_TEXTURE_COORD_ARRAY);
      gl->VertexPointer (2, GL_FLOAT, 0, &verts);
      gl->TexCoordPointer (2, GL_FLOAT, 0, &texcoords);

      gl->DrawArrays (GL_TRIANGLE_FAN, 0, 4);

      gl->DisableClientState (GL_VERTEX_ARRAY);
      gl->DisableClientState (GL_TEXTURE_COORD_ARRAY);

      gl->Disable (GL_TEXTURE_2D);
    }
#endif
#if GST_GL_HAVE_GLES2
    if (USING_GLES2 (gl_sink->context)) {
      const GLfloat vVertices[] = { 1.0f, 1.0f, 0.0f,
        1.0f, 0.0f,
        -1.0f, 1.0f, 0.0f,
        0.0f, 0.0f,
        -1.0f, -1.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, -1.0f, 0.0f,
        1.0f, 1.0f
      };

      GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

      gl->Clear (GL_COLOR_BUFFER_BIT);

      gst_gl_shader_use (gl_sink->redisplay_shader);

      /* Load the vertex position */
      gl->VertexAttribPointer (gl_sink->redisplay_attr_position_loc, 3,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), vVertices);

      /* Load the texture coordinate */
      gl->VertexAttribPointer (gl_sink->redisplay_attr_texture_loc, 2,
          GL_FLOAT, GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

      gl->EnableVertexAttribArray (gl_sink->redisplay_attr_position_loc);
      gl->EnableVertexAttribArray (gl_sink->redisplay_attr_texture_loc);

      gl->ActiveTexture (GL_TEXTURE0);
      gl->BindTexture (GL_TEXTURE_2D, gl_sink->redisplay_texture);
      gst_gl_shader_set_uniform_1i (gl_sink->redisplay_shader, "s_texture", 0);

      gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);
    }
#endif
  }                             /* end default opengl scene */

  window->is_drawing = FALSE;
  gst_object_unref (window);

  GST_GLIMAGE_SINK_UNLOCK (gl_sink);
}

static void
gst_glimage_sink_on_close (GstGLImageSink * gl_sink)
{
  gst_gl_context_set_error (gl_sink->context, "Output window was closed");

  g_atomic_int_set (&gl_sink->to_quit, 1);
}

static gboolean
gst_glimage_sink_redisplay (GstGLImageSink * gl_sink)
{
  GstGLWindow *window;
  gboolean alive;

  window = gst_gl_context_get_window (gl_sink->context);

  if (window && gst_gl_window_is_running (window)) {

#if GST_GL_HAVE_GLES2
    if (USING_GLES2 (gl_sink->context)) {
      if (!gl_sink->redisplay_shader) {
        gst_gl_window_send_message (window,
            GST_GL_WINDOW_CB (gst_glimage_sink_thread_init_redisplay), gl_sink);
      }
    }
#endif

    /* Drawing is asynchrone: gst_gl_window_draw is not blocking
     * It means that it does not wait for stuff being executed in other threads
     */
    gst_gl_window_draw (window, GST_VIDEO_SINK_WIDTH (gl_sink),
        GST_VIDEO_SINK_HEIGHT (gl_sink));
  }
  alive = gst_gl_window_is_running (window);
  gst_object_unref (window);

  return alive;
}
