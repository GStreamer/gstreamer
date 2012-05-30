/*
 * GStreamer
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Inspired from http://www.mdk.org.pl/2007/11/17/gl-colorspace-conversions
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
 * SECTION:element-glfilterglass
 *
 * Map textures on moving glass.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch -v videotestsrc ! glupload ! glfilterglass ! glimagesink
 * ]| A pipeline inspired from http://www.mdk.org.pl/2007/11/17/gl-colorspace-conversions
 * FBO is required.
 * |[
 * gst-launch -v videotestsrc ! glupload ! glfilterglass ! "video/x-raw-gl, width=640, height=480" ! glimagesink
 * ]| The scene is greater than the input size.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <math.h>
#include "gstglfilterglass.h"

#define GST_CAT_DEFAULT gst_gl_filter_glass_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

enum
{
  PROP_0
};

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filter_glass_debug, "glfilterglass", 0, "glfilterglass element");

G_DEFINE_TYPE_WITH_CODE (GstGLFilterGlass, gst_gl_filter_glass,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filter_glass_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filter_glass_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_filter_glass_reset (GstGLFilter * filter);
static gboolean gst_gl_filter_glass_init_shader (GstGLFilter * filter);
static gboolean gst_gl_filter_glass_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf);

static void gst_gl_filter_glass_draw_background_gradient ();
static void gst_gl_filter_glass_draw_video_plane (GstGLFilter * filter,
    gint width, gint height, guint texture, gfloat center_x, gfloat center_y,
    gfloat start_alpha, gfloat stop_alpha, gboolean reversed);

static void gst_gl_filter_glass_callback (gint width, gint height,
    guint texture, gpointer stuff);

static const gchar *glass_fragment_source =
    "#extension GL_ARB_texture_rectangle : enable\n"
    "uniform sampler2DRect tex;"
    "uniform float width, height;"
    "void main () {"
    "  float p = 0.0525;"
    "  float L1 = p*width;"
    "  float L2 = width - L1;"
    "  float L3 = height - L1;"
    "  float w = 1.0;"
    "  float r = L1;"
    "  if (gl_TexCoord[0].x < L1 && gl_TexCoord[0].y < L1)"
    "      r = sqrt( (gl_TexCoord[0].x - L1) * (gl_TexCoord[0].x - L1) + (gl_TexCoord[0].y - L1) * (gl_TexCoord[0].y - L1) );"
    "  else if (gl_TexCoord[0].x > L2 && gl_TexCoord[0].y < L1)"
    "      r = sqrt( (gl_TexCoord[0].x - L2) * (gl_TexCoord[0].x - L2) + (gl_TexCoord[0].y - L1) * (gl_TexCoord[0].y - L1) );"
    "  else if (gl_TexCoord[0].x > L2 && gl_TexCoord[0].y > L3)"
    "      r = sqrt( (gl_TexCoord[0].x - L2) * (gl_TexCoord[0].x - L2) + (gl_TexCoord[0].y - L3) * (gl_TexCoord[0].y - L3) );"
    "  else if (gl_TexCoord[0].x < L1 && gl_TexCoord[0].y > L3)"
    "      r = sqrt( (gl_TexCoord[0].x - L1) * (gl_TexCoord[0].x - L1) + (gl_TexCoord[0].y - L3) * (gl_TexCoord[0].y - L3) );"
    "  if (r > L1)"
    "      w = 0.0;"
    "  vec4 color = texture2DRect (tex, gl_TexCoord[0].st);"
    "  gl_FragColor = vec4(color.rgb, gl_Color.a * w);" "}";

static void
gst_gl_filter_glass_class_init (GstGLFilterGlassClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_filter_glass_set_property;
  gobject_class->get_property = gst_gl_filter_glass_get_property;

  gst_element_class_set_details_simple (element_class, "OpenGL glass filter",
      "Filter/Effect", "Glass Filter",
      "Julien Isorce <julien.isorce@gmail.com>");

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filter_glass_filter;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_filter_glass_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_filter_glass_reset;
}

static void
gst_gl_filter_glass_init (GstGLFilterGlass * filter)
{
  filter->shader = NULL;
  filter->timestamp = 0;
}

static void
gst_gl_filter_glass_reset (GstGLFilter * filter)
{
  GstGLFilterGlass *glass_filter = GST_GL_FILTER_GLASS (filter);

  //blocking call, wait the opengl thread has destroyed the shader
  gst_gl_display_del_shader (filter->display, glass_filter->shader);
}

static void
gst_gl_filter_glass_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  //GstGLFilterGlass *filter = GST_GL_FILTER_GLASS (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filter_glass_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  //GstGLFilterGlass *filter = GST_GL_FILTER_GLASS (object);

  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_filter_glass_init_shader (GstGLFilter * filter)
{
  GstGLFilterGlass *glass_filter = GST_GL_FILTER_GLASS (filter);

  //blocking call, wait the opengl thread has compiled the shader
  return gst_gl_display_gen_shader (filter->display, 0, glass_fragment_source,
      &glass_filter->shader);
}

static gboolean
gst_gl_filter_glass_filter (GstGLFilter * filter, GstGLBuffer * inbuf,
    GstGLBuffer * outbuf)
{
  gpointer glass_filter = GST_GL_FILTER_GLASS (filter);
  GST_GL_FILTER_GLASS (glass_filter)->timestamp = GST_BUFFER_TIMESTAMP (inbuf);

  //blocking call, use a FBO
  gst_gl_display_use_fbo (filter->display, filter->width, filter->height,
      filter->fbo, filter->depthbuffer, outbuf->texture,
      gst_gl_filter_glass_callback, inbuf->width, inbuf->height, inbuf->texture,
      80, (gdouble) filter->width / (gdouble) filter->height, 1.0, 5000.0,
      GST_GL_DISPLAY_PROJECTION_PERSPECTIVE, (gpointer) glass_filter);

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
gst_gl_filter_glass_draw_background_gradient ()
{
  glMatrixMode (GL_PROJECTION);

  glPushMatrix ();
  glLoadIdentity ();
  glOrtho (-100, 100, -100, 100, -1000.0, 1000.0);

  glBegin (GL_QUADS);

  glColor4f (0.0f, 0.0f, 0.0f, 1.0f);
  glVertex2f (-100.0f, -100.0f);
  glVertex2f (100.0f, -100.0f);

  glColor4f (0.0f, 0.0f, 0.2f, 1.0f);
  glVertex2f (100.0f, 80.0f);
  glVertex2f (-100.0f, 80.0f);

  glVertex2f (100.0f, 80.0f);
  glVertex2f (-100.0f, 80.0f);

  glVertex2f (-100.0f, 100.0f);
  glVertex2f (100.0f, 100.0f);

  glEnd ();
  glPopMatrix ();

  glMatrixMode (GL_MODELVIEW);
}

static void
gst_gl_filter_glass_draw_video_plane (GstGLFilter * filter,
    gint width, gint height, guint texture,
    gfloat center_x, gfloat center_y,
    gfloat start_alpha, gfloat stop_alpha, gboolean reversed)
{
  GstGLFilterGlass *glass_filter = GST_GL_FILTER_GLASS (filter);

  gfloat topy;
  gfloat bottomy;
  if (reversed) {
    topy = center_y - 1.0f;
    bottomy = center_y + 1.0f;
  } else {
    topy = center_y + 1.0f;
    bottomy = center_y - 1.0f;
  }

  gst_gl_shader_use (glass_filter->shader);

  glActiveTextureARB (GL_TEXTURE0_ARB);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, texture);
  glDisable (GL_TEXTURE_RECTANGLE_ARB);

  gst_gl_shader_set_uniform_1i (glass_filter->shader, "tex", 0);
  gst_gl_shader_set_uniform_1f (glass_filter->shader, "width", (gfloat) width);
  gst_gl_shader_set_uniform_1f (glass_filter->shader, "height",
      (gfloat) height);

  glBegin (GL_QUADS);
  glColor4f (1.0f, 1.0f, 1.0f, start_alpha);
  glTexCoord2i (0, height);
  glVertex2f (center_x - 1.6f, topy);
  glTexCoord2i (width, height);
  glVertex2f (center_x + 1.6f, topy);

  glColor4f (1.0, 1.0, 1.0, stop_alpha);
  glTexCoord2i (width, 0);
  glVertex2f (center_x + 1.6f, bottomy);
  glTexCoord2i (0, 0);
  glVertex2f (center_x - 1.6f, bottomy);
  glEnd ();

  gst_gl_shader_use (0);
}

//opengl scene, params: input texture (not the output filter->texture)
static void
gst_gl_filter_glass_callback (gint width, gint height, guint texture,
    gpointer stuff)
{
  static gint64 start_time = 0;

  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFilterGlass *glass_filter = GST_GL_FILTER_GLASS (stuff);

  if (start_time == 0)
    start_time = get_time ();
  else {
    gint64 time_left =
        (glass_filter->timestamp / 1000) - (get_time () - start_time);
    time_left -= 1000000 / 25;
    if (time_left > 2000) {
      GST_LOG ("escape");
      return;
    }
  }

  glTranslatef (0.0f, 2.0f, -3.0f);

  gst_gl_filter_glass_draw_background_gradient ();


  //Rotation
  if (start_time != 0) {
    gint64 time_passed = get_time () - start_time;
    glRotated (sin (time_passed / 1200000.0) * 45.0, 0.0, 1.0, 0.0);
  }

  glEnable (GL_BLEND);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  //Reflection
  gst_gl_filter_glass_draw_video_plane (filter, width, height, texture,
      0.0f, 0.0f, 0.3f, 0.0f, TRUE);

  //Main video
  gst_gl_filter_glass_draw_video_plane (filter, width, height, texture,
      0.0f, -2.0f, 1.0f, 1.0f, FALSE);

  glDisable (GL_TEXTURE_RECTANGLE_ARB);
  glDisable (GL_BLEND);
}
