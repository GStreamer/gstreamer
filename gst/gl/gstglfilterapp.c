/* 
 * GStreamer
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglfilterapp.h"

#define GST_CAT_DEFAULT gst_gl_filter_app_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details = 
    GST_ELEMENT_DETAILS ("OpenGL application filter",
        "Filter/Effect",
        "Use client callbacks to define the scene",
        "Julien Isorce <julien.isorce@gmail.com>");

enum
{
    PROP_0,
    PROP_GLCONTEXT_WIDTH,
    PROP_GLCONTEXT_HEIGHT,
    PROP_CLIENT_RESHAPE_CALLBACK,
    PROP_CLIENT_DRAW_CALLBACK
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_app_debug, "glfilterapp", 0, "glfilterapp element");

GST_BOILERPLATE_FULL (GstGLFilterApp, gst_gl_filter_app, GstGLFilter,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filter_app_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_app_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_filter_app_set_caps (GstGLFilter* filter, 
    GstCaps* incaps, GstCaps* outcaps);
static void gst_gl_filter_app_setClientCallbacks (GstGLFilter* filter);
static gboolean gst_gl_filter_app_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf);
static void gst_gl_filter_app_callback (guint width, guint height, guint texture);


static void
gst_gl_filter_app_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_filter_app_class_init (GstGLFilterAppClass * klass)
{
  GObjectClass* gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_filter_app_set_property;
  gobject_class->get_property = gst_gl_filter_app_get_property;

  GST_GL_FILTER_CLASS (klass)->set_caps = gst_gl_filter_app_set_caps;
  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filter_app_filter;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_filter_app_setClientCallbacks;

  g_object_class_install_property (gobject_class, PROP_GLCONTEXT_WIDTH,
      g_param_spec_int ("glcontext_width", "OpenGL context width",
          "Change the opengl context width", 0, INT_MAX, 0, 
          G_PARAM_WRITABLE));

    g_object_class_install_property (gobject_class, PROP_GLCONTEXT_HEIGHT,
      g_param_spec_int ("glcontext_height", "OpenGL context height",
          "Change the opengl context height", 0, INT_MAX, 0, 
          G_PARAM_WRITABLE));

    g_object_class_install_property (gobject_class, PROP_CLIENT_RESHAPE_CALLBACK,
      g_param_spec_pointer ("client_reshape_callback", "Client reshape callback",
          "Executed in next glut loop iteration when window size is changed", 
          G_PARAM_WRITABLE));

    g_object_class_install_property (gobject_class, PROP_CLIENT_DRAW_CALLBACK,
      g_param_spec_pointer ("client_draw_callback", "Client draw callback",
          "Executed in next glut loop iteration when glutPostRedisplay is called", 
          G_PARAM_WRITABLE));
}

static void
gst_gl_filter_app_init (GstGLFilterApp* filter,
    GstGLFilterAppClass* klass)
{
    filter->glcontext_width = 0;
    filter->glcontext_height = 0;
    filter->clientReshapeCallback = NULL;
    filter->clientDrawCallback = NULL;
}

static void
gst_gl_filter_app_set_property (GObject* object, guint prop_id,
    const GValue* value, GParamSpec* pspec)
{
    GstGLFilterApp* filter = GST_GL_FILTER_APP (object);

    switch (prop_id) {
    case PROP_GLCONTEXT_WIDTH:
    {
        filter->glcontext_width = g_value_get_int (value);
        break;
    }
    case PROP_GLCONTEXT_HEIGHT:
    {
        filter->glcontext_height = g_value_get_int (value);
        break;
    }
    case PROP_CLIENT_RESHAPE_CALLBACK:
    {
        filter->clientReshapeCallback = g_value_get_pointer (value);
        break;
    }
    case PROP_CLIENT_DRAW_CALLBACK:
    {
        filter->clientDrawCallback = g_value_get_pointer (value);
        break;
    }
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
        break;
    }
}

static void
gst_gl_filter_app_get_property (GObject* object, guint prop_id,
    GValue* value, GParamSpec* pspec)
{
    //GstGLFilterApp *filter = GST_GL_FILTER_APP (object);

    switch (prop_id) 
    {
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
            break;
    }
}

static gboolean
gst_gl_filter_app_set_caps (GstGLFilter* filter, GstCaps* incaps,
    GstCaps* outcaps)
{
    GstGLFilterApp* app_filter = GST_GL_FILTER_APP(filter);

    g_print ("app_filter: gst_gl_filter_set_caps\n");

    /*if (graphicmaker->glcontext_width != 0 && graphicmaker->glcontext_height != 0)
        {
            GValue value_w = { 0 };
            GValue value_h = { 0 };
            g_value_init (&value_w, G_TYPE_INT);
            g_value_init (&value_h, G_TYPE_INT);
            g_value_set_int (&value_w, graphicmaker->glcontext_width);
            g_value_set_int (&value_h, graphicmaker->glcontext_height);
            gst_structure_set_value (newstruct, "width", &value_w);
	        gst_structure_set_value (newstruct, "height", &value_h);
            g_value_unset (&value_w);
            g_value_unset (&value_h);
        }
        else*/
    return TRUE;
}

static void
gst_gl_filter_app_setClientCallbacks (GstGLFilter* filter)
{
    GstGLFilterApp* app_filter = GST_GL_FILTER_APP(filter);
    
    //set the client reshape callback
    gst_gl_display_setClientReshapeCallback (filter->display, 
        app_filter->clientReshapeCallback);
    
    //set the client draw callback
    gst_gl_display_setClientDrawCallback (filter->display, 
        app_filter->clientDrawCallback);

    if (app_filter->glcontext_width != 0 && app_filter->glcontext_height != 0)
        gst_gl_display_resetGLcontext (filter->display,
            app_filter->glcontext_width, app_filter->glcontext_height);
}

static gboolean
gst_gl_filter_app_filter (GstGLFilter* filter, GstGLBuffer* inbuf,
    GstGLBuffer* outbuf)
{
    GstGLFilterApp* app_filter = GST_GL_FILTER_APP(filter);

    outbuf->width = filter->width;
    outbuf->height = filter->height;
    outbuf->texture = inbuf->texture;
    outbuf->texture_u = inbuf->texture_u;
    outbuf->texture_v = inbuf->texture_v;
    outbuf->textureGL = inbuf->textureGL;

    return TRUE;
}
