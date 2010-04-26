/*
 * GStreamer
 * Copyright (C) 2010 Pierre Pouzol<pierre.pouzol@hotmail.fr>
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
 * SECTION:element-glfilterreflectedscreen
 *
 * Map Video Texture upon a screen, on a reflecting surface
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! glupload ! glfilterreflectedscreen active_graphic_mode=TRUE ! glimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include "gstglfilterreflectedscreen.h"

#define GST_CAT_DEFAULT gst_gl_filter_reflected_screen_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0,
  PROP_ACTIVE_GRAPHIC_MODE
};

#define DEBUG_INIT(bla)							\
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_reflected_screen_debug, "glfilterreflectedscreen", 0, "glfilterreflectedscreen element");

GST_BOILERPLATE_FULL (GstGLFilterReflectedScreen,
    gst_gl_filter_reflected_screen, GstGLFilter, GST_TYPE_GL_FILTER,
    DEBUG_INIT);

static void gst_gl_filter_reflected_screen_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_reflected_screen_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static gboolean gst_gl_filter_reflected_screen_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf);

static void gst_gl_filter_reflected_screen_draw_floor ();
static void gst_gl_filter_reflected_screen_draw_screen (GstGLFilter * filter,
    gint width, gint height, guint texture);

static void gst_gl_filter_reflected_screen_callback (gint width, gint height,
    guint texture, gpointer stuff);

static void
gst_gl_filter_reflected_screen_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details_simple (element_class,
      "OpenGL Reflected Screen filter", "Filter/Effect",
      "Reflected Screen Filter", "Pierre POUZOL <pierre.pouzol@hotmail.fr>");
}

static void
gst_gl_filter_reflected_screen_class_init (GstGLFilterReflectedScreenClass *
    klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_filter_reflected_screen_set_property;
  gobject_class->get_property = gst_gl_filter_reflected_screen_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filter_reflected_screen_filter;

  g_object_class_install_property (gobject_class, PROP_ACTIVE_GRAPHIC_MODE,
      g_param_spec_boolean ("active_graphic_mode",
          "Activate graphic mode",
          "Allow user to activate stencil buffer and blending.",
          TRUE, G_PARAM_READWRITE));
}

static void
gst_gl_filter_reflected_screen_init (GstGLFilterReflectedScreen * filter,
    GstGLFilterReflectedScreenClass * klass)
{
  filter->timestamp = 0;
  filter->active_graphic_mode = TRUE;
}

static void
gst_gl_filter_reflected_screen_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLFilterReflectedScreen *filter = GST_GL_FILTER_REFLECTED_SCREEN (object);

  switch (prop_id) {
    case PROP_ACTIVE_GRAPHIC_MODE:
    {
      filter->active_graphic_mode = g_value_get_boolean (value);
      break;
    }
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_reflected_screen_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLFilterReflectedScreen *filter = GST_GL_FILTER_REFLECTED_SCREEN (object);

  switch (prop_id) {
    case PROP_ACTIVE_GRAPHIC_MODE:
      g_value_set_boolean (value, filter->active_graphic_mode);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_filter_reflected_screen_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf)
{
  gpointer reflected_screen_filter = GST_GL_FILTER_REFLECTED_SCREEN (filter);
  GST_GL_FILTER_REFLECTED_SCREEN (reflected_screen_filter)->timestamp =
      GST_BUFFER_TIMESTAMP (inbuf);

  //blocking call, use a FBO
  gst_gl_display_use_fbo (filter->display, filter->width, filter->height,
      filter->fbo, filter->depthbuffer, outbuf->texture,
      gst_gl_filter_reflected_screen_callback, inbuf->width, inbuf->height,
      inbuf->texture, 80, (gdouble) filter->width / (gdouble) filter->height,
      1.0, 5000.0, GST_GL_DISPLAY_PROJECTION_PERSPECTIVE,
      (gpointer) reflected_screen_filter);

  return TRUE;
}

static gint64
get_time (void)
{
  static GTimeVal val;
  g_get_current_time (&val);

  return (val.tv_sec * G_USEC_PER_SEC) + val.tv_usec;
}

static void
gst_gl_filter_reflected_screen_draw_screen (GstGLFilter * filter,
    gint width, gint height, guint texture)
{
  //enable ARB Rectangular texturing
  //that's necessary to have the video displayed on our screen (with gstreamer)
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  //configure parameters for the texturing
  //the two first are used to specified how the texturing will be done if the screen is greater than the texture herself
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  //the next two specified how the texture will comport near the limits
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_S,
      GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_RECTANGLE_ARB, GL_TEXTURE_WRAP_T,
      GL_CLAMP_TO_EDGE);
  glTexEnvi (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

  //creating screen and setting the texture (depending on texture's height and width)
  glBegin (GL_QUADS);

  // right Face
  glTexCoord2f (0.0f, (gfloat) height);
  glVertex3f (-1.0f, 0.0f, -1.0f);
  glTexCoord2f (0.0f, 0.0f);
  glVertex3f (-1.0f, 1.0f, -1.0f);
  glTexCoord2f ((gfloat) width, 0.0f);
  glVertex3f (1.0f, 1.0f, -1.0f);
  glTexCoord2f ((gfloat) width, (gfloat) height);
  glVertex3f (1.0f, 0.0f, -1.0f);
  // Left Face
  glTexCoord2f ((gfloat) width, (gfloat) height);
  glVertex3f (-1.0f, 0.0f, -1.0f);
  glTexCoord2f (0.0f, (gfloat) height);
  glVertex3f (-1.0f, 0.0f, 1.0f);
  glTexCoord2f (0.0f, 0.0f);
  glVertex3f (-1.0f, 1.0f, 1.0f);
  glTexCoord2f ((gfloat) width, 0.0f);
  glVertex3f (-1.0f, 1.0f, -1.0f);

  glEnd ();

  //disable this kind of texturing (useless for the gluDisk)
  glDisable (GL_TEXTURE_RECTANGLE_ARB);
}

static void
gst_gl_filter_reflected_screen_draw_floor ()
{
  GLUquadricObj *q;
  //create a quadric for the floor's drawing
  q = gluNewQuadric ();
  //configure this quadric's parameter (for lighting and texturing)
  gluQuadricNormals (q, GL_SMOOTH);
  gluQuadricTexture (q, GL_FALSE);

  //drawing the disk. The texture are mapped thanks to the parameter we gave to the GLUquadric q
  gluDisk (q, 0.0, 2.0, 50, 1);
}

//opengl scene, params: input texture (not the output filter->texture)
static void
gst_gl_filter_reflected_screen_callback (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFilterReflectedScreen *reflected_screen_filter =
      GST_GL_FILTER_REFLECTED_SCREEN (stuff);

  glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

  //load identity befor tracing
  glLoadIdentity ();
  //camera translation
  glTranslatef (0.0f, 0.1f, -1.5f);
  //camera configuration
  gluLookAt (0.1, -0.2, 1.4, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);

  if (reflected_screen_filter->active_graphic_mode) {
    //Stencil buffer use start
    //creation of a black mask upon the entire screen. This mean that none of the red, blue, green and alpha color on the screen will be shown
    glColorMask (GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    //enable stencil buffer use
    glEnable (GL_STENCIL_TEST);
    //setting the stencil buffer. Each time a pixel will be drawn by now, this pixel value will be set to 1
    glStencilFunc (GL_ALWAYS, 1, 1);
    glStencilOp (GL_KEEP, GL_KEEP, GL_REPLACE);

    //disable the zbuffer
    glDisable (GL_DEPTH_TEST);
    //make a rotation of 90 degree on x axis. By default, gluDisk draw a disk on z axis
    glRotatef (-90.0f, 1.0, 0.0, 0.0);
    //draw the floor. Each pixel representing this floor will now have a value of 1 on stencil buffer
    gst_gl_filter_reflected_screen_draw_floor ();
    //make an anti-rotation of 90 degree to draw the rest of the scene on the right angle
    glRotatef (90.0f, 1.0, 0.0, 0.0);
    //enable zbuffer again
    glEnable (GL_DEPTH_TEST);
    //enable the drawing to be shown
    glColorMask (GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    //say that the next object have to be drawn ONLY where the stencil buffer's pixel's value is 1
    glStencilFunc (GL_EQUAL, 1, 1);
    glStencilOp (GL_KEEP, GL_KEEP, GL_KEEP);
    //save the actual matrix
    glPushMatrix ();
    //translate the object on z axis
    glTranslatef (0.0f, 0.0f, 1.3f);
    //rotate it (because the drawing method place the user behind the left part of the screen)
    glRotatef (-45.0f, 0.0, 1.0, 0.0);
    //draw the reflexion
    gst_gl_filter_reflected_screen_draw_screen (filter, width, height, texture);
    //return to the saved matrix position
    glPopMatrix ();
    //end of the stencil buffer uses
    glDisable (GL_STENCIL_TEST);
  }
  //enable the blending to mix the floor and reflexion color
  glEnable (GL_BLEND);
  glColor4f (1.0f, 1.0f, 1.0f, 0.8f);
  //configuration of the transparency function
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  //draw the floor (which will appear this time)
  //specified a white color (for the floor) with 20% transparency
  glRotatef (-90.0f, 1.0, 0.0, 0.0);
  gst_gl_filter_reflected_screen_draw_floor ();
  glRotatef (90.0f, 1.0, 0.0, 0.0);
  glDisable (GL_BLEND);
  //draw the real object
  //scale on y axis. The object must be drawn upside down (to suggest a reflexion)

  glScalef (1.0f, -1.0f, 1.0f);
  glTranslatef (0.0f, 0.0f, 1.3f);
  glRotatef (-45.0f, 0.0, 1.0, 0.0);
  gst_gl_filter_reflected_screen_draw_screen (filter, width, height, texture);
}
