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

/**
 * SECTION:element-glfilterapp
 *
 * The resize and redraw callbacks can be set from a client code.
 *
 * <refsect2>
 * <title>CLient callbacks</title>
 * <para>
 * The graphic scene can be written from a client code through the 
 * two glfilterapp properties.
 * </para>
 * </refsect2>
 * <refsect2>
 * <title>Examples</title>
 * see gst-plugins-gl/tests/examples/generic/recordgraphic
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglfilterapp.h"

#define GST_CAT_DEFAULT gst_gl_filter_app_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_CLIENT_RESHAPE_CALLBACK,
  PROP_CLIENT_DRAW_CALLBACK,
  PROP_CLIENT_DATA
};

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_app_debug, "glfilterapp", 0, "glfilterapp element");

GST_BOILERPLATE_FULL (GstGLFilterApp, gst_gl_filter_app, GstGLFilter,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filter_app_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_app_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_gl_filter_app_set_caps (GstGLFilter * filter,
    GstCaps * incaps, GstCaps * outcaps);
static gboolean gst_gl_filter_app_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf);
static void gst_gl_filter_app_callback (gint width, gint height, guint texture,
    gpointer stuff);


static void
gst_gl_filter_app_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "OpenGL application filter", "Filter/Effect",
      "Use client callbacks to define the scene",
      "Julien Isorce <julien.isorce@gmail.com>");
}

static void
gst_gl_filter_app_class_init (GstGLFilterAppClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_filter_app_set_property;
  gobject_class->get_property = gst_gl_filter_app_get_property;

  GST_GL_FILTER_CLASS (klass)->set_caps = gst_gl_filter_app_set_caps;
  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filter_app_filter;

  g_object_class_install_property (gobject_class, PROP_CLIENT_RESHAPE_CALLBACK,
      g_param_spec_pointer ("client_reshape_callback",
          "Client reshape callback",
          "Define a custom reshape callback in a client code",
          G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_CLIENT_DRAW_CALLBACK,
      g_param_spec_pointer ("client_draw_callback", "Client draw callback",
          "Define a custom draw callback in a client code", G_PARAM_WRITABLE));

  g_object_class_install_property (gobject_class, PROP_CLIENT_DATA,
      g_param_spec_pointer ("client_data", "Client data",
          "Pass data to the draw and reshape callbacks", G_PARAM_WRITABLE));
}

static void
gst_gl_filter_app_init (GstGLFilterApp * filter, GstGLFilterAppClass * klass)
{
  filter->clientReshapeCallback = NULL;
  filter->clientDrawCallback = NULL;
  filter->client_data = NULL;
}

static void
gst_gl_filter_app_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLFilterApp *filter = GST_GL_FILTER_APP (object);

  switch (prop_id) {
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
    case PROP_CLIENT_DATA:
    {
      filter->client_data = g_value_get_pointer (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_app_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLFilterApp* filter = GST_GL_FILTER_APP (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_filter_app_set_caps (GstGLFilter * filter, GstCaps * incaps,
    GstCaps * outcaps)
{
  //GstGLFilterApp* app_filter = GST_GL_FILTER_APP(filter);

  return TRUE;
}

static gboolean
gst_gl_filter_app_filter (GstGLFilter * filter, GstGLBuffer * inbuf,
    GstGLBuffer * outbuf)
{
  GstGLFilterApp *app_filter = GST_GL_FILTER_APP (filter);

  if (app_filter->clientDrawCallback) {
    //blocking call, use a FBO
    gst_gl_display_use_fbo (filter->display, filter->width, filter->height,
        filter->fbo, filter->depthbuffer, outbuf->texture,
        app_filter->clientDrawCallback, inbuf->width, inbuf->height,
        inbuf->texture, 45, (gfloat) filter->width / (gfloat) filter->height,
        0.1, 100, GST_GL_DISPLAY_PROJECTION_PERSPECTIVE, app_filter->client_data);
  }
  //default
  else {
    //blocking call, use a FBO
    gst_gl_display_use_fbo (filter->display, filter->width, filter->height,
        filter->fbo, filter->depthbuffer, outbuf->texture,
        gst_gl_filter_app_callback, inbuf->width, inbuf->height, inbuf->texture,
        0, filter->width, 0, filter->height, GST_GL_DISPLAY_PROJECTION_ORTHO2D,
        NULL);
  }

  return TRUE;
}

//opengl scene, params: input texture (not the output filter->texture)
static void
gst_gl_filter_app_callback (gint width, gint height, guint texture,
    gpointer stuff)
{
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  glBegin (GL_QUADS);
  glTexCoord2i (0, 0);
  glVertex2f (-1.0f, -1.0f);
  glTexCoord2i (width, 0);
  glVertex2f (1.0f, -1.0f);
  glTexCoord2i (width, height);
  glVertex2f (1.0f, 1.0f);
  glTexCoord2i (0, height);
  glVertex2f (-1.0f, 1.0f);
  glEnd ();
}
