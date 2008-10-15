/*
 * GStreamer
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
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

#include <gstglfilter.h>
#include <gstgleffectssources.h>

#define GST_TYPE_GL_FILTERSOBEL            (gst_gl_filtersobel_get_type())
#define GST_GL_FILTERSOBEL(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_GL_FILTERSOBEL,GstGLFilterSobel))
#define GST_IS_GL_FILTERSOBEL(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_GL_FILTERSOBEL))
#define GST_GL_FILTERSOBEL_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) ,GST_TYPE_GL_FILTERSOBEL,GstGLFilterSobelClass))
#define GST_IS_GL_FILTERSOBEL_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) ,GST_TYPE_GL_FILTERSOBEL))
#define GST_GL_FILTERSOBEL_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) ,GST_TYPE_GL_FILTERSOBEL,GstGLFilterSobelClass))

typedef struct _GstGLFilterSobel GstGLFilterSobel;
typedef struct _GstGLFilterSobelClass GstGLFilterSobelClass;

struct _GstGLFilterSobel
{
  GstGLFilter filter;
  GstGLShader *shader0;

  gboolean invert;

  GLuint midtexture;
};

struct _GstGLFilterSobelClass
{
  GstGLFilterClass filter_class;
};

enum
{
  PROP_0,
  PROP_INVERT,
};

GType gst_gl_glfiltersobel_get_type (void);

#define GST_CAT_DEFAULT gst_gl_filtersobel_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static const GstElementDetails element_details =
GST_ELEMENT_DETAILS ("Gstreamer OpenGL Sobel",
    "Filter/Effect",
    "Sobel edge detection",
    "Filippo Argiolas <filippo.argiolas@gmail.com>");

#define DEBUG_INIT(bla)							\
  GST_DEBUG_CATEGORY_INIT (gst_gl_filtersobel_debug, "glfiltersobel", 0, "glfiltersobel element");

GST_BOILERPLATE_FULL (GstGLFilterSobel, gst_gl_filtersobel, GstGLFilter,
		      GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filtersobel_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filtersobel_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_gl_filter_filtersobel_reset (GstGLFilter* filter);
static void gst_gl_filtersobel_draw_texture (GstGLFilterSobel * filtersobel, GLuint tex);

static void gst_gl_filtersobel_init_shader (GstGLFilter* filter);
static gboolean gst_gl_filtersobel_filter (GstGLFilter * filter,
                                           GstGLBuffer * inbuf, GstGLBuffer * outbuf);
static void gst_gl_filtersobel_callback (gint width, gint height, guint texture, gpointer stuff);

static void
gst_gl_filtersobel_init_resources (GstGLFilter *filter)
{
  GstGLFilterSobel *filtersobel = GST_GL_FILTERSOBEL (filter);

  glGenTextures (1, &filtersobel->midtexture);
  glBindTexture(GL_TEXTURE_RECTANGLE_ARB, filtersobel->midtexture);
  glTexImage2D(GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA8,
	       filter->width, filter->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void
gst_gl_filtersobel_reset_resources (GstGLFilter *filter)
{
  GstGLFilterSobel *filtersobel = GST_GL_FILTERSOBEL (filter);

  glDeleteTextures (1, &filtersobel->midtexture);
}

static void
gst_gl_filtersobel_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_filtersobel_class_init (GstGLFilterSobelClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_filtersobel_set_property;
  gobject_class->get_property = gst_gl_filtersobel_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filtersobel_filter;
  GST_GL_FILTER_CLASS (klass)->display_init_cb = gst_gl_filtersobel_init_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb = gst_gl_filtersobel_reset_resources;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_filtersobel_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_filter_filtersobel_reset;

  g_object_class_install_property (
    gobject_class,
    PROP_INVERT,
    g_param_spec_boolean ("invert",
                          "Invert the colors",
                          "Invert colors to get dark edges on bright background",
                          FALSE,
                          G_PARAM_READWRITE));
}

static void
gst_gl_filtersobel_init (GstGLFilterSobel * filtersobel, GstGLFilterSobelClass * klass)
{
  filtersobel->shader0 = NULL;
  filtersobel->midtexture = 0;
  filtersobel->invert = FALSE;
}

static void
gst_gl_filter_filtersobel_reset (GstGLFilter* filter)
{
  GstGLFilterSobel* filtersobel = GST_GL_FILTERSOBEL(filter);

  //blocking call, wait the opengl thread has destroyed the shader
  gst_gl_display_del_shader (filter->display, filtersobel->shader0);
}

static void
gst_gl_filtersobel_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLFilterSobel *filtersobel = GST_GL_FILTERSOBEL (object);

  switch (prop_id) {
  case PROP_INVERT:
    filtersobel->invert = g_value_get_boolean (value);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_filtersobel_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLFilterSobel *filtersobel = GST_GL_FILTERSOBEL (object);

  switch (prop_id) {
  case PROP_INVERT:
    g_value_set_boolean (value, filtersobel->invert);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_filtersobel_init_shader (GstGLFilter* filter)
{
  GstGLFilterSobel* filtersobel = GST_GL_FILTERSOBEL (filter);

  //blocking call, wait the opengl thread has compiled the shader
  gst_gl_display_gen_shader (filter->display, 0, sobel_fragment_source, &filtersobel->shader0);
}

static void
gst_gl_filtersobel_draw_texture (GstGLFilterSobel * filtersobel, GLuint tex)
{
  GstGLFilter *filter = GST_GL_FILTER (filtersobel);

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, tex);

  glBegin (GL_QUADS);

  glTexCoord2f (0.0, 0.0);
  glVertex2f (-1.0, -1.0);
  glTexCoord2f ((gfloat)filter->width, 0.0);
  glVertex2f (1.0, -1.0);
  glTexCoord2f ((gfloat)filter->width, (gfloat)filter->height);
  glVertex2f (1.0, 1.0);
  glTexCoord2f (0.0, (gfloat)filter->height);
  glVertex2f (-1.0, 1.0);

  glEnd ();
}

/*static void
change_view (GstGLDisplay *display, gpointer data)
{
//  GstGLFilterSobel *filtersobel = GST_GL_FILTERSOBEL (data);

  const double mirrormatrix[16] = {
    -1.0, 0.0, 0.0, 0.0,
    0.0, 1.0, 0.0, 0.0,
    0.0, 0.0, 1.0, 0.0,
    0.0, 0.0, 0.0, 1.0
  };

  glMatrixMode (GL_MODELVIEW);
  glLoadMatrixd (mirrormatrix);
  }*/

static gboolean
gst_gl_filtersobel_filter (GstGLFilter* filter, GstGLBuffer* inbuf,
				GstGLBuffer* outbuf)
{
  GstGLFilterSobel* filtersobel = GST_GL_FILTERSOBEL(filter);

//  gst_gl_display_thread_add (filter->display, change_view, filtersobel);

  gst_gl_filter_render_to_target (filter, inbuf->texture, outbuf->texture,
				  gst_gl_filtersobel_callback, filtersobel);

  return TRUE;
}

static void
gst_gl_filtersobel_callback (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLFilterSobel* filtersobel = GST_GL_FILTERSOBEL (stuff);

  gfloat hkern[9] = { 
    1.0, 0.0, -1.0,
    2.0, 0.0, -2.0,
    1.0, 0.0, -1.0
  };
  
  gfloat vkern[9] = {
     1.0,  2.0,  1.0,
     0.0,  0.0,  0.0,
    -1.0, -2.0, -1.0
  };

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (filtersobel->shader0);

  glActiveTexture (GL_TEXTURE1);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (filtersobel->shader0, "tex", 1);

  gst_gl_shader_set_uniform_1fv (filtersobel->shader0, "hkern", 9, hkern);
  gst_gl_shader_set_uniform_1fv (filtersobel->shader0, "vkern", 9, vkern);

  gst_gl_shader_set_uniform_1i (filtersobel->shader0, "invert", filtersobel->invert);

  gst_gl_filtersobel_draw_texture (filtersobel, texture);
}
