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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-glimagesink
 *
 * glimagesink renders video frames to a drawable on a local or remote
 * display using OpenGL. This element can receive a Window ID from the
 * application through the XOverlay interface and will then render video
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
 * drawable scaling them on the fly. Using the #GstXvImageSink:force-aspect-ratio
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

#include <gst/interfaces/xoverlay.h>

#include "gstglimagesink.h"

GST_DEBUG_CATEGORY (gst_debug_glimage_sink);
#define GST_CAT_DEFAULT gst_debug_glimage_sink

static void gst_glimage_sink_init_interfaces (GType type);

static void gst_glimage_sink_finalize (GObject * object);
static void gst_glimage_sink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * param_spec);
static void gst_glimage_sink_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * param_spec);

static GstStateChangeReturn
gst_glimage_sink_change_state (GstElement * element, GstStateChange transition);

static void gst_glimage_sink_get_times (GstBaseSink * bsink, GstBuffer * buf,
    GstClockTime * start, GstClockTime * end);
static gboolean gst_glimage_sink_set_caps (GstBaseSink * bsink, GstCaps * caps);
static GstFlowReturn gst_glimage_sink_render (GstBaseSink * bsink,
    GstBuffer * buf);
static gboolean gst_glimage_sink_start (GstBaseSink * bsink);
static gboolean gst_glimage_sink_stop (GstBaseSink * bsink);

static void gst_glimage_sink_xoverlay_init (GstXOverlayClass * iface);
static void gst_glimage_sink_set_xwindow_id (GstXOverlay * overlay,
    gulong window_id);
static void gst_glimage_sink_expose (GstXOverlay * overlay);
static gboolean gst_glimage_sink_interface_supported (GstImplementsInterface *
    iface, GType type);
static void gst_glimage_sink_implements_init (GstImplementsInterfaceClass *
    klass);

static const GstElementDetails gst_glimage_sink_details =
    GST_ELEMENT_DETAILS ("OpenGL video sink",
        "Sink/Video",
        "A videosink based on OpenGL",
        "Julien Isorce <julien.isorce@gmail.com>");

static GstStaticPadTemplate gst_glimage_sink_template =
    GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_GL_VIDEO_CAPS ";"
        GST_VIDEO_CAPS_RGB ";" GST_VIDEO_CAPS_BGR ";"
        GST_VIDEO_CAPS_RGBx ";" GST_VIDEO_CAPS_BGRx ";"
        GST_VIDEO_CAPS_xRGB ";" GST_VIDEO_CAPS_xBGR ";"
        GST_VIDEO_CAPS_RGBA ";" GST_VIDEO_CAPS_BGRA ";"
        GST_VIDEO_CAPS_ARGB ";" GST_VIDEO_CAPS_ABGR ";"
        GST_VIDEO_CAPS_YUV ("{ I420, YV12, YUY2, UYVY, AYUV }"))
    );

enum
{
    ARG_0,
    ARG_DISPLAY,
    PROP_CLIENT_RESHAPE_CALLBACK,
    PROP_CLIENT_DRAW_CALLBACK
};

GST_BOILERPLATE_FULL (GstGLImageSink, gst_glimage_sink, GstVideoSink,
    GST_TYPE_VIDEO_SINK, gst_glimage_sink_init_interfaces);

static void
gst_glimage_sink_init_interfaces (GType type)
{

  static const GInterfaceInfo implements_info = {
    (GInterfaceInitFunc) gst_glimage_sink_implements_init,
    NULL,
    NULL
  };

   static const GInterfaceInfo xoverlay_info = {
    (GInterfaceInitFunc) gst_glimage_sink_xoverlay_init,
    NULL,
    NULL,
  };

  g_type_add_interface_static (type, GST_TYPE_IMPLEMENTS_INTERFACE,
      &implements_info);

  g_type_add_interface_static (type, GST_TYPE_X_OVERLAY, &xoverlay_info);

  GST_DEBUG_CATEGORY_INIT (gst_debug_glimage_sink, "glimagesink", 0,
    "OpenGL Video Sink");
}

static void
gst_glimage_sink_base_init (gpointer g_class)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);

  gst_element_class_set_details (element_class, &gst_glimage_sink_details);
  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&gst_glimage_sink_template));

}

static void
gst_glimage_sink_class_init (GstGLImageSinkClass* klass)
{
    GObjectClass *gobject_class;
    GstElementClass *gstelement_class;
    GstBaseSinkClass *gstbasesink_class;

    gobject_class = (GObjectClass *) klass;
    gstelement_class = (GstElementClass *) klass;
    gstbasesink_class = (GstBaseSinkClass *) klass;

    gobject_class->set_property = gst_glimage_sink_set_property;
    gobject_class->get_property = gst_glimage_sink_get_property;

    g_object_class_install_property (gobject_class, ARG_DISPLAY,
      g_param_spec_string ("display", "Display", "Display name",
          NULL, G_PARAM_READWRITE));

    g_object_class_install_property (gobject_class, PROP_CLIENT_RESHAPE_CALLBACK,
      g_param_spec_pointer ("client_reshape_callback", "Client reshape callback",
          "Define a custom reshape callback in a client code",
          G_PARAM_WRITABLE));

    g_object_class_install_property (gobject_class, PROP_CLIENT_DRAW_CALLBACK,
      g_param_spec_pointer ("client_draw_callback", "Client draw callback",
          "Define a custom draw callback in a client code",
          G_PARAM_WRITABLE));

    gobject_class->finalize = gst_glimage_sink_finalize;

    gstelement_class->change_state = gst_glimage_sink_change_state;

    gstbasesink_class->set_caps = gst_glimage_sink_set_caps;
    gstbasesink_class->get_times = gst_glimage_sink_get_times;
    gstbasesink_class->preroll = gst_glimage_sink_render;
    gstbasesink_class->render = gst_glimage_sink_render;
    gstbasesink_class->start = gst_glimage_sink_start;
    gstbasesink_class->stop = gst_glimage_sink_stop;
}

static void
gst_glimage_sink_init (GstGLImageSink* glimage_sink,
    GstGLImageSinkClass* glimage_sink_class)
{
    glimage_sink->display_name = NULL;
    glimage_sink->window_id = 0;
    glimage_sink->display = NULL;
    glimage_sink->stored_buffer = NULL;
    glimage_sink->clientReshapeCallback = NULL;
    glimage_sink->clientDrawCallback = NULL;
}

static void
gst_glimage_sink_set_property (GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    GstGLImageSink *glimage_sink;

    g_return_if_fail (GST_IS_GLIMAGE_SINK (object));

    glimage_sink = GST_GLIMAGE_SINK (object);

    switch (prop_id)
    {
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
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static void
gst_glimage_sink_finalize (GObject* object)
{
    GstGLImageSink* glimage_sink;

    g_return_if_fail (GST_IS_GLIMAGE_SINK (object));

    glimage_sink = GST_GLIMAGE_SINK (object);

    if (glimage_sink->caps)
        gst_caps_unref (glimage_sink->caps);

    g_free (glimage_sink->display_name);

    GST_DEBUG ("finalized");
}

static void
gst_glimage_sink_get_property (GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec)
{
    GstGLImageSink *glimage_sink;

    g_return_if_fail (GST_IS_GLIMAGE_SINK (object));

    glimage_sink = GST_GLIMAGE_SINK (object);

    switch (prop_id)
    {
        case ARG_DISPLAY:
            g_value_set_string (value, glimage_sink->display_name);
            break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

/*
 * GstElement methods
 */

static GstStateChangeReturn
gst_glimage_sink_change_state (GstElement* element, GstStateChange transition)
{
    GstGLImageSink* glimage_sink;
    GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;

    GST_DEBUG ("change state");

    glimage_sink = GST_GLIMAGE_SINK (element);

    switch (transition)
    {
        case GST_STATE_CHANGE_NULL_TO_READY:
            break;
        case GST_STATE_CHANGE_READY_TO_PAUSED:
            break;
        case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
            break;
        default:
            break;
    }

    ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);
    if (ret == GST_STATE_CHANGE_FAILURE)
        return ret;

    switch (transition)
    {
        case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
            break;
        case GST_STATE_CHANGE_PAUSED_TO_READY:
            gst_glimage_sink_stop (GST_BASE_SINK (glimage_sink));
            glimage_sink->fps_n = 0;
            glimage_sink->fps_d = 1;
            GST_VIDEO_SINK_WIDTH (glimage_sink) = 0;
            GST_VIDEO_SINK_HEIGHT (glimage_sink) = 0;
            break;
        case GST_STATE_CHANGE_READY_TO_NULL:
            break;
        default:
            break;
    }

    return ret;
}

/*
 * GstBaseSink methods
 */

static gboolean
gst_glimage_sink_start (GstBaseSink* bsink)
{
    //GstGLImageSink* glimage_sink = GST_GLIMAGE_SINK (bsink);

    GST_DEBUG ("start");

    return TRUE;
}

static gboolean
gst_glimage_sink_stop (GstBaseSink* bsink)
{
    GstGLImageSink* glimage_sink;

    GST_DEBUG ("stop");

    glimage_sink = GST_GLIMAGE_SINK (bsink);

    if (glimage_sink->stored_buffer)
    {
        gst_buffer_unref (glimage_sink->stored_buffer);
        glimage_sink->stored_buffer = NULL;
    }
    if (glimage_sink->display)
    {
        g_object_unref (glimage_sink->display);
        glimage_sink->display = NULL;
    }

    return TRUE;
}

static void
gst_glimage_sink_get_times (GstBaseSink* bsink, GstBuffer* buf,
    GstClockTime* start, GstClockTime* end)
{
    GstGLImageSink* glimagesink;

    glimagesink = GST_GLIMAGE_SINK (bsink);

    if (GST_BUFFER_TIMESTAMP_IS_VALID (buf))
    {
        *start = GST_BUFFER_TIMESTAMP (buf);
        if (GST_BUFFER_DURATION_IS_VALID (buf))
            *end = *start + GST_BUFFER_DURATION (buf);
        else
        {
            if (glimagesink->fps_n > 0) {
            *end = *start +
                gst_util_uint64_scale_int (GST_SECOND, glimagesink->fps_d,
                glimagesink->fps_n);
            }
        }
    }
}

static gboolean
gst_glimage_sink_set_caps (GstBaseSink* bsink, GstCaps* caps)
{
    GstGLImageSink* glimage_sink;
    gint width;
    gint height;
    gboolean ok;
    gint fps_n, fps_d;
    gint par_n, par_d;
    GstVideoFormat format;
    GstStructure *structure;
    gboolean is_gl;

    GST_DEBUG ("set caps with %" GST_PTR_FORMAT, caps);

    glimage_sink = GST_GLIMAGE_SINK (bsink);

    structure = gst_caps_get_structure (caps, 0);
    if (gst_structure_has_name (structure, "video/x-raw-gl")) {
        is_gl = TRUE;
        format = GST_VIDEO_FORMAT_UNKNOWN;
        ok = gst_structure_get_int (structure, "width", &width);
        ok &= gst_structure_get_int (structure, "height", &height);
    } else {
        is_gl = FALSE;
        ok = gst_video_format_parse_caps (caps, &format, &width, &height);
    }
    ok &= gst_video_parse_caps_framerate (caps, &fps_n, &fps_d);
    ok &= gst_video_parse_caps_pixel_aspect_ratio (caps, &par_n, &par_d);

    if (!ok)
        return FALSE;

    GST_VIDEO_SINK_WIDTH (glimage_sink) = width;
    GST_VIDEO_SINK_HEIGHT (glimage_sink) = height;
    glimage_sink->is_gl = is_gl;
    glimage_sink->format = format;
    glimage_sink->width = width;
    glimage_sink->height = height;
    glimage_sink->fps_n = fps_n;
    glimage_sink->fps_d = fps_d;
    glimage_sink->par_n = par_n;
    glimage_sink->par_d = par_d;

    if (!glimage_sink->window_id)
        gst_x_overlay_prepare_xwindow_id (GST_X_OVERLAY (glimage_sink));

    return TRUE;
}

static GstFlowReturn
gst_glimage_sink_render (GstBaseSink* bsink, GstBuffer* buf)
{
    GstGLImageSink* glimage_sink = NULL;
    GstGLBuffer* gl_buffer = NULL;

    glimage_sink = GST_GLIMAGE_SINK (bsink);

    GST_INFO ("buffer size: %d", GST_BUFFER_SIZE (buf));

    //is gl
    if (glimage_sink->is_gl)
    {
        //increment gl buffer ref before storage
        gl_buffer = GST_GL_BUFFER (gst_buffer_ref (buf));

        //if glimagesink has not the display yet
        if (glimage_sink->display == NULL)
        {
            glimage_sink->display = g_object_ref (gl_buffer->display);

            if (glimage_sink->window_id)
                gst_gl_display_set_window_id (glimage_sink->display, glimage_sink->window_id);

            gst_gl_display_set_client_reshape_callback (glimage_sink->display,
                glimage_sink->clientReshapeCallback);

            gst_gl_display_set_client_draw_callback (glimage_sink->display,
                glimage_sink->clientDrawCallback);
        }
    }
    //is not gl
    else
    {
        //if glimagesink has not the display yet
        if (glimage_sink->display == NULL)
        {
            //create a display
            glimage_sink->display = gst_gl_display_new ();

            //init opengl context
            gst_gl_display_create_context (glimage_sink->display,
                glimage_sink->width, glimage_sink->height);

            if (glimage_sink->window_id)
                gst_gl_display_set_window_id (glimage_sink->display, glimage_sink->window_id);

            //init colorspace conversion if needed
            gst_gl_display_init_upload (glimage_sink->display, glimage_sink->format,
                glimage_sink->width, glimage_sink->height,
                glimage_sink->width, glimage_sink->height);

            gst_gl_display_set_client_reshape_callback (glimage_sink->display,
                glimage_sink->clientReshapeCallback);

            gst_gl_display_set_client_draw_callback (glimage_sink->display,
                glimage_sink->clientDrawCallback);
        }

        //blocking call
        gl_buffer = gst_gl_buffer_new (glimage_sink->display,
            glimage_sink->width, glimage_sink->height);

        //blocking call
        gst_gl_display_do_upload(glimage_sink->display, gl_buffer->texture,
          glimage_sink->width, glimage_sink->height, GST_BUFFER_DATA (buf));

        //gl_buffer is created in this block, so the gl buffer is already referenced
    }

    //the buffer is cleared when an other comes in
    if (glimage_sink->stored_buffer)
    {
        gst_buffer_unref (glimage_sink->stored_buffer);
        glimage_sink->stored_buffer = NULL;
    }

    //store current buffer
    glimage_sink->stored_buffer = gl_buffer;

    //redisplay opengl scene
    if (gl_buffer->texture &&
        gst_gl_display_redisplay (glimage_sink->display,
            gl_buffer->texture, gl_buffer->width, gl_buffer->height))
        return GST_FLOW_OK;
    else
        return GST_FLOW_UNEXPECTED;
}


static void
gst_glimage_sink_xoverlay_init (GstXOverlayClass* iface)
{
    iface->set_xwindow_id = gst_glimage_sink_set_xwindow_id;
    iface->expose = gst_glimage_sink_expose;
}


static void
gst_glimage_sink_set_xwindow_id (GstXOverlay* overlay, gulong window_id)
{
    GstGLImageSink* glimage_sink = GST_GLIMAGE_SINK (overlay);

    g_return_if_fail (GST_IS_GLIMAGE_SINK (overlay));

    GST_DEBUG ("set_xwindow_id %ld", window_id);

    if (glimage_sink->window_id == window_id)
      return;

    if (window_id)
        glimage_sink->window_id = window_id;
}


static void
gst_glimage_sink_expose (GstXOverlay* overlay)
{
    GstGLImageSink* glimage_sink = GST_GLIMAGE_SINK (overlay);

    //redisplay opengl scene
    if (glimage_sink->display && glimage_sink->window_id)
        gst_gl_display_redisplay (glimage_sink->display, 0, 0, 0);
}


static gboolean
gst_glimage_sink_interface_supported (GstImplementsInterface* iface,
    GType type)
{
    return TRUE;
}


static void
gst_glimage_sink_implements_init (GstImplementsInterfaceClass* klass)
{
    klass->supported = gst_glimage_sink_interface_supported;
}
