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

static void gst_glimage_sink_finalize (GObject * object);
static void gst_glimage_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_glimage_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static gboolean gst_glimage_sink_query (GstBaseSink * bsink, GstQuery * query);

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

static GstStaticPadTemplate gst_glimage_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_UPLOAD_VIDEO_CAPS)
    );

enum
{
  ARG_0,
  ARG_DISPLAY,
  PROP_CLIENT_RESHAPE_CALLBACK,
  PROP_CLIENT_DRAW_CALLBACK,
  PROP_CLIENT_DATA,
  PROP_FORCE_ASPECT_RATIO,
  PROP_PIXEL_ASPECT_RATIO
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
      g_param_spec_string ("pixel-aspect-ratio", "Pixel Aspect Ratio",
          "The pixel aspect ratio of the device", "1/1",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_details_simple (element_class, "OpenGL video sink",
      "Sink/Video", "A videosink based on OpenGL",
      "Julien Isorce <julien.isorce@gmail.com>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_glimage_sink_template));

  gobject_class->finalize = gst_glimage_sink_finalize;

  gstelement_class->change_state = gst_glimage_sink_change_state;

  gstbasesink_class->query = GST_DEBUG_FUNCPTR (gst_glimage_sink_query);
  gstbasesink_class->set_caps = gst_glimage_sink_set_caps;
  gstbasesink_class->get_times = gst_glimage_sink_get_times;
  gstbasesink_class->preroll = gst_glimage_sink_render;
  gstbasesink_class->render = gst_glimage_sink_render;
  gstbasesink_class->propose_allocation = gst_glimage_sink_propose_allocation;
}

static void
gst_glimage_sink_init (GstGLImageSink * glimage_sink)
{
  glimage_sink->display_name = NULL;
  glimage_sink->window_id = 0;
  glimage_sink->new_window_id = 0;
  glimage_sink->display = NULL;
  glimage_sink->stored_buffer = NULL;
  glimage_sink->clientReshapeCallback = NULL;
  glimage_sink->clientDrawCallback = NULL;
  glimage_sink->client_data = NULL;
  glimage_sink->keep_aspect_ratio = FALSE;
  glimage_sink->par = NULL;
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
      g_free (glimage_sink->par);
      glimage_sink->par = g_new0 (GValue, 1);
      g_value_init (glimage_sink->par, GST_TYPE_FRACTION);
      if (!g_value_transform (value, glimage_sink->par)) {
        g_warning ("Could not transform string to aspect ratio");
        gst_value_set_fraction (glimage_sink->par, 1, 1);
      }
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

  if (glimage_sink->par) {
    g_free (glimage_sink->par);
    glimage_sink->par = NULL;
  }

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
      if (!glimage_sink->par) {
        glimage_sink->par = g_new0 (GValue, 1);
        g_value_init (glimage_sink->par, GST_TYPE_FRACTION);
        gst_value_set_fraction (glimage_sink->par, 1, 1);
      }
      if (!g_value_transform (glimage_sink->par, value))
        g_warning ("Could not transform string to aspect ratio");
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_glimage_sink_query (GstBaseSink * bsink, GstQuery * query)
{
  GstGLImageSink *glimage_sink = GST_GLIMAGE_SINK (bsink);
  gboolean res = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CUSTOM:
    {
      GstStructure *structure = gst_query_writable_structure (query);
      if (gst_structure_has_name (structure, "gstgldisplay")) {
        gst_structure_set (structure, "gstgldisplay", G_TYPE_POINTER,
            glimage_sink->display, NULL);
        res = TRUE;
      } else
        res = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
    }
    default:
      res = GST_BASE_SINK_CLASS (parent_class)->query (bsink, query);
      break;
  }

  return res;
}


/*
 * GstElement methods
 */

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
      if (!glimage_sink->display) {
        gboolean ok = FALSE;

        GST_INFO ("Creating GstGLDisplay");
        glimage_sink->display = gst_gl_display_new ();

        /* init opengl context */
        ok = gst_gl_display_create_context (glimage_sink->display, 0);
        if (!ok) {
          GST_ELEMENT_ERROR (glimage_sink, RESOURCE, NOT_FOUND,
              GST_GL_DISPLAY_ERR_MSG (glimage_sink->display), (NULL));

          if (glimage_sink->display) {
            g_object_unref (glimage_sink->display);
            glimage_sink->display = NULL;
          }

          return GST_STATE_CHANGE_FAILURE;
        }
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
      if (glimage_sink->stored_buffer) {
        gst_buffer_unref (GST_BUFFER_CAST (glimage_sink->stored_buffer));
        glimage_sink->stored_buffer = NULL;
      }
      if (glimage_sink->display) {
        g_object_unref (glimage_sink->display);
        glimage_sink->display = NULL;
      }

      glimage_sink->window_id = 0;
      //but do not reset glimage_sink->new_window_id

      GST_VIDEO_SINK_WIDTH (glimage_sink) = 1;
      GST_VIDEO_SINK_HEIGHT (glimage_sink) = 1;
    }
      break;
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

  GST_DEBUG ("set caps with %" GST_PTR_FORMAT, caps);

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  ok = gst_video_info_from_caps (&vinfo, caps);
  if (!ok)
    return FALSE;

  width = GST_VIDEO_INFO_WIDTH (&vinfo);
  height = GST_VIDEO_INFO_HEIGHT (&vinfo);

  if (glimage_sink->tex_id)
    gst_gl_display_del_texture (glimage_sink->display, &glimage_sink->tex_id);
  gst_gl_display_gen_texture (glimage_sink->display, &glimage_sink->tex_id,
      GST_VIDEO_INFO_FORMAT (&vinfo), width, height);

  glimage_sink->upload = gst_gl_display_find_upload (glimage_sink->display,
      GST_VIDEO_INFO_FORMAT (&vinfo), width, height, width, height);

  gst_gl_upload_init_format (glimage_sink->upload,
      GST_VIDEO_INFO_FORMAT (&vinfo), width, height, width, height);

  gst_gl_display_set_client_reshape_callback (glimage_sink->display,
      glimage_sink->clientReshapeCallback);

  gst_gl_display_set_client_draw_callback (glimage_sink->display,
      glimage_sink->clientDrawCallback);

  gst_gl_display_set_client_data (glimage_sink->display,
      glimage_sink->client_data);

  par_n = GST_VIDEO_INFO_PAR_N (&vinfo);
  par_d = GST_VIDEO_INFO_PAR_D (&vinfo);

  if (!par_n)
    par_n = 1;

  /* get display's PAR */
  if (glimage_sink->par) {
    display_par_n = gst_value_get_fraction_numerator (glimage_sink->par);
    display_par_d = gst_value_get_fraction_denominator (glimage_sink->par);
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

  if (!glimage_sink->window_id && !glimage_sink->new_window_id)
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (glimage_sink));

  return TRUE;
}

static GstFlowReturn
gst_glimage_sink_render (GstBaseSink * bsink, GstBuffer * buf)
{
  GstGLImageSink *glimage_sink;
  guint tex_id;
  GstVideoFrame frame;

  GST_TRACE ("rendering buffer:%p", buf);

  glimage_sink = GST_GLIMAGE_SINK (bsink);

  if (GST_VIDEO_SINK_WIDTH (glimage_sink) < 1 ||
      GST_VIDEO_SINK_HEIGHT (glimage_sink) < 1) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (!gst_video_frame_map (&frame, &glimage_sink->info, buf,
          GST_MAP_READ | GST_MAP_GL)) {
    GST_WARNING ("Failed to map memory");
    return GST_FLOW_ERROR;
  }

  if (frame.map[0].memory && gst_is_gl_memory (frame.map[0].memory)) {
    tex_id = *(guint *) frame.data[0];
  } else {
    GST_INFO ("Input Buffer does not contain correct meta, "
        "attempting to wrap for upload");

    gst_gl_upload_perform_with_data (glimage_sink->upload,
        glimage_sink->tex_id, frame.data);

    tex_id = glimage_sink->tex_id;
  }

  if (glimage_sink->window_id != glimage_sink->new_window_id) {
    glimage_sink->window_id = glimage_sink->new_window_id;
    gst_gl_display_set_window_id (glimage_sink->display,
        glimage_sink->window_id);
  }
  //the buffer is cleared when an other comes in
  if (glimage_sink->stored_buffer) {
    gst_buffer_unref (glimage_sink->stored_buffer);
  }
  //store current buffer
  glimage_sink->stored_buffer = gst_buffer_ref (buf);

  GST_TRACE ("redisplay texture:%u of size:%ux%u, window size:%ux%u", tex_id,
      GST_VIDEO_INFO_WIDTH (&glimage_sink->info),
      GST_VIDEO_INFO_HEIGHT (&glimage_sink->info),
      GST_VIDEO_SINK_WIDTH (glimage_sink),
      GST_VIDEO_SINK_HEIGHT (glimage_sink));

  //redisplay opengl scene
  if (!gst_gl_display_redisplay (glimage_sink->display,
          tex_id, GST_VIDEO_INFO_WIDTH (&glimage_sink->info),
          GST_VIDEO_INFO_HEIGHT (&glimage_sink->info),
          GST_VIDEO_SINK_WIDTH (glimage_sink),
          GST_VIDEO_SINK_HEIGHT (glimage_sink),
          glimage_sink->keep_aspect_ratio))
    goto redisplay_failed;

  GST_TRACE ("post redisplay");

  gst_video_frame_unmap (&frame);

  return GST_FLOW_OK;

/* ERRORS */
redisplay_failed:
  {
    gst_video_frame_unmap (&frame);
    GST_ELEMENT_ERROR (glimage_sink, RESOURCE, NOT_FOUND,
        GST_GL_DISPLAY_ERR_MSG (glimage_sink->display), (NULL));
    return GST_FLOW_ERROR;
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
  gulong window_id = (gulong) id;

  g_return_if_fail (GST_IS_GLIMAGE_SINK (overlay));

  GST_DEBUG ("set_xwindow_id %ld", window_id);

  glimage_sink->new_window_id = window_id;
}


static void
gst_glimage_sink_expose (GstVideoOverlay * overlay)
{
  GstGLImageSink *glimage_sink = GST_GLIMAGE_SINK (overlay);

  //redisplay opengl scene
  if (glimage_sink->display && glimage_sink->window_id) {

    if (glimage_sink->window_id != glimage_sink->new_window_id) {
      glimage_sink->window_id = glimage_sink->new_window_id;
      gst_gl_display_set_window_id (glimage_sink->display,
          glimage_sink->window_id);
    }

    gst_gl_display_redisplay (glimage_sink->display, 0, 0, 0, 0, 0,
        glimage_sink->keep_aspect_ratio);
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
    pool = gst_gl_buffer_pool_new (glimage_sink->display);

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
