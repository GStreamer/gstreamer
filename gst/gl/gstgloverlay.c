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

/**
 * SECTION:element-gloverlay
 *
 * Overlay GL video texture with a PNG image
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch  videotestsrc ! "video/x-raw-rgb" ! glupload ! gloverlay location=imagefile ! glimagesink
 * ]|
 * FBO (Frame Buffer Object) is required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <png.h>
#include "gstgloverlay.h"
#include <gstgleffectssources.h>

#if PNG_LIBPNG_VER >= 10400
#define int_p_NULL         NULL
#define png_infopp_NULL    NULL
#endif

#define GST_CAT_DEFAULT gst_gl_overlay_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT(bla)							\
  GST_DEBUG_CATEGORY_INIT (gst_gl_overlay_debug, "gloverlay", 0, "gloverlay element");

GST_BOILERPLATE_FULL (GstGLOverlay, gst_gl_overlay, GstGLFilter,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_overlay_init_resources (GstGLFilter * filter);
static void gst_gl_overlay_reset_resources (GstGLFilter * filter);

static gboolean gst_gl_overlay_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf);

static gboolean gst_gl_overlay_loader (GstGLFilter * filter);

static const GstElementDetails element_details =
GST_ELEMENT_DETAILS ("Gstreamer OpenGL Overlay",
    "Filter/Effect",
    "Overlay GL video texture with a PNG image",
    "Filippo Argiolas <filippo.argiolas@gmail.com>");

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_XPOS_PNG,
  PROP_YPOS_PNG,
  PROP_SIZE_PNG,
  PROP_XPOS_VIDEO,
  PROP_YPOS_VIDEO,
  PROP_SIZE_VIDEO,
  PROP_VIDEOTOP,
  PROP_ROTATE_PNG,
  PROP_ROTATE_VIDEO,
  PROP_ANGLE_PNG,
  PROP_ANGLE_VIDEO
};


/* init resources that need a gl context */
static void
gst_gl_overlay_init_gl_resources (GstGLFilter * filter)
{
//  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);
}

/* free resources that need a gl context */
static void
gst_gl_overlay_reset_gl_resources (GstGLFilter * filter)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);

  glDeleteTextures (1, &overlay->pbuftexture);
}

static void
gst_gl_overlay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_overlay_class_init (GstGLOverlayClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_overlay_set_property;
  gobject_class->get_property = gst_gl_overlay_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_overlay_filter;
  GST_GL_FILTER_CLASS (klass)->display_init_cb =
      gst_gl_overlay_init_gl_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb =
      gst_gl_overlay_reset_gl_resources;
  GST_GL_FILTER_CLASS (klass)->onStart = gst_gl_overlay_init_resources;
  GST_GL_FILTER_CLASS (klass)->onStop = gst_gl_overlay_reset_resources;

  g_object_class_install_property (gobject_class,
      PROP_LOCATION,
      g_param_spec_string ("location",
          "Location of the image",
          "Location of the image", NULL, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_XPOS_PNG,
      g_param_spec_int ("xpos-png",
          "X position of overlay image in percents",
          "X position of overlay image in percents",
          0, 100, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_YPOS_PNG,
      g_param_spec_int ("ypos-png",
          "Y position of overlay image in percents",
          "Y position of overlay image in percents",
          0, 100, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_SIZE_PNG,
      g_param_spec_int ("proportion-png",
          "Relative size of overlay image, in percents",
          "Relative size of iverlay image, in percents",
          0, 100, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_XPOS_VIDEO,
      g_param_spec_int ("xpos-video",
          "X position of overlay video in percents",
          "X position of overlay video in percents",
          0, 100, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_YPOS_VIDEO,
      g_param_spec_int ("ypos-video",
          "Y position of overlay video in percents",
          "Y position of overlay video in percents",
          0, 100, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_SIZE_VIDEO,
      g_param_spec_int ("proportion-video",
          "Relative size of overlay video, in percents",
          "Relative size of iverlay video, in percents",
          0, 100, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_VIDEOTOP,
      g_param_spec_boolean ("video-top",
          "Video-top", "Video is over png image", FALSE, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_ROTATE_PNG,
      g_param_spec_int ("rotate_png",
          "choose rotation axis for the moment only Y axis is implemented",
          "choose rotation axis for the moment only Y axis is implemented",
          0, 3, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_ROTATE_VIDEO,
      g_param_spec_int ("rotate_video",
          "choose rotation axis for the moment only Y axis is implemented",
          "choose rotation axis for the moment only Y axis is implemented",
          0, 3, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_ANGLE_PNG,
      g_param_spec_int ("angle_png",
          "choose angle in axis to choosen between -90 and 90",
          "choose angle in axis to choosen between -90 and 90",
          -90, 90, 0, G_PARAM_READWRITE));

  g_object_class_install_property (gobject_class,
      PROP_ANGLE_VIDEO,
      g_param_spec_int ("angle_video",
          "choose angle in axis to choosen between -90 and 90",
          "choose angle in axis to choosen between -90 and 90",
          -90, 90, 0, G_PARAM_READWRITE));
  /*
     g_object_class_install_property (gobject_class,
     PROP_STRETCH,
     g_param_spec_boolean ("stretch",
     "Stretch the image to texture size",
     "Stretch the image to fit video texture size",
     TRUE, G_PARAM_READWRITE));
   */
}

static void
gst_gl_overlay_draw_texture_video_on_png (GstGLOverlay * overlay, GLuint tex)
{
  GstGLFilter *filter = GST_GL_FILTER (overlay);
  gfloat posx = 0.0;
  gfloat posy = 0.0;
  gfloat size = 0.0;
  gfloat width = (gfloat) filter->width;
  gfloat height = (gfloat) filter->height;
  gfloat ratio = 0.0;
  gfloat translate = 0.0;

  //  if (overlay->stretch) {
  //  width = (gfloat) overlay->width;
  // height = (gfloat) overlay->height;
  //  }
  if (overlay->pbuftexture != 0) {
    size = (overlay->size_png) / 50.0f;
    posx = (overlay->pos_x_png - 50.0f) / 50.0f;
    posx =
        (posx - (size / 2) < -1.00) ? (-1.0f + size / 2) : (posx + (size / 2) >
        1.00) ? (1.0f - size / 2) : posx;
    posy = (overlay->pos_y_png - 50.0f) / 50.0f;
    posy =
        (posy - (size / 2) < -1.00) ? (-1.0f + size / 2) : (posy + (size / 2) >
        1.00) ? (1.0f - size / 2) : posy;
    ratio = ((posx + (size / 2)) - (posx - (size / 2))) * height / width;
    translate = overlay->size_png / 400.0f;
    glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable (GL_TEXTURE_RECTANGLE_ARB);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, overlay->pbuftexture);
    glLoadIdentity ();
    glTranslatef (0.0f, translate, -1.0f);
    if (overlay->rotate_png == 2)
      glRotatef (overlay->angle_png, 0, 1, 0);
    glBegin (GL_QUADS);
    glTexCoord3f (0.0f, 0.0f, 0.0f);
    glVertex3f (posx - (size / 2), posy - (size / 2), 0.0f);
    glTexCoord3f (width, 0.0, 0.0);
    glVertex3f (posx + (size / 2), posy - (size / 2), 0.0f);
    glTexCoord3f (width, height, 0.0);
    glVertex3f (posx + (size / 2), posy - (size / 2) + ratio, 0.0f);
    glTexCoord3f (0.0, height, 0.0);
    glVertex3f (posx - (size / 2), posy - (size / 2) + ratio, 0.0f);
    glEnd ();
  }
  size = (overlay->size_video) / 50.0f;
  posx = (overlay->pos_x_video - 50.0f) / 50.0f;
  posx =
      (posx - (size / 2) < -1.00) ? (-1.0f + size / 2) : (posx + (size / 2) >
      1.00) ? (1.0f - size / 2) : posx;
  posy = (overlay->pos_y_video - 50.0f) / 50.0f;
  posy =
      (posy - (size / 2) < -1.00) ? (-1.0f + size / 2) : (posy + (size / 2) >
      1.00) ? (1.0f - size / 2) : posy;
  ratio = ((posx + (size / 2)) - (posx - (size / 2))) * height / width;
  translate = overlay->size_video / 400.0f;
  glActiveTexture (GL_TEXTURE0);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, tex);
  glLoadIdentity ();
  glTranslatef (0.0f, translate, -1.0f);
  if (overlay->rotate_video)
    glRotatef (overlay->angle_video, 0, 1, 0);
  glBegin (GL_QUADS);
  glTexCoord3f (0.0f, 0.0f, 0.0f);
  glVertex3f (posx - (size / 2), posy - (size / 2), 0.0f);
  glTexCoord3f (width, 0.0f, 0.0f);
  glVertex3f (posx + (size / 2), posy - (size / 2), 0.0f);
  glTexCoord3f (width, height, 0.0);
  glVertex3f (posx + (size / 2), posy - (size / 2) + ratio, 0.0f);
  glTexCoord3f (0.0f, height, 0.0f);
  glVertex3f (posx - (size / 2), posy - (size / 2) + ratio, 0.0f);
  glEnd ();
  glFlush ();
}

static void
gst_gl_overlay_draw_texture_png_on_video (GstGLOverlay * overlay, GLuint tex)
{

  GstGLFilter *filter = GST_GL_FILTER (overlay);
  gfloat posx = 0.0;
  gfloat posy = 0.0;
  gfloat size = 0.0;
  gfloat width = (gfloat) filter->width;
  gfloat height = (gfloat) filter->height;
  gfloat ratio = 0.0;
  gfloat translate = 0.0;

  size = (overlay->size_video) / 50.0f;
  posx = (overlay->pos_x_video - 50.0f) / 50.0f;
  posx =
      (posx - (size / 2) < -1.00) ? (-1.0f + size / 2) : (posx + (size / 2) >
      1.00) ? (1.0f - size / 2) : posx;
  posy = (overlay->pos_y_video - 50.0f) / 50.0f;
  posy =
      (posy - (size / 2) < -1.00) ? (-1.0f + size / 2) : (posy + (size / 2) >
      1.00) ? (1.0f - size / 2) : posy;
  ratio = ((posx + (size / 2)) - (posx - (size / 2))) * height / width;
  translate = overlay->size_video / 400.0f;
  glActiveTexture (GL_TEXTURE0);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, tex);
  glLoadIdentity ();
  glTranslatef (0.0f, translate, -1.0f);
  if (overlay->rotate_video)
    glRotatef (overlay->angle_video, 0, 1, 0);
  glBegin (GL_QUADS);
  glTexCoord3f (0.0f, 0.0f, 0.0f);
  glVertex3f (posx - (size / 2), posy - (size / 2), 0.0f);
  glTexCoord3f (width, 0.0f, 0.0f);
  glVertex3f (posx + (size / 2), posy - (size / 2), 0.0f);
  glTexCoord3f (width, height, 0.0f);
  glVertex3f (posx + (size / 2), posy - (size / 2) + ratio, 0.0f);
  glTexCoord3f (0.0f, height, 0.0f);
  glVertex3f (posx - (size / 2), posy - (size / 2) + ratio, 0.0f);
  glEnd ();
  if (overlay->pbuftexture == 0)
    return;

  //  if (overlay->stretch) {
  //width = (gfloat) overlay->width;
  //height = (gfloat) overlay->height;
  //  }
  size = (overlay->size_png) / 50.0f;
  posx = (overlay->pos_x_png - 50.0f) / 50.0f;
  posx =
      (posx - (size / 2) < -1.00) ? (-1.0f + size / 2) : (posx + (size / 2) >
      1.00) ? (1.0f - size / 2) : posx;
  posy = (overlay->pos_y_png - 50.0f) / 50.0f;
  posy =
      (posy - (size / 2) < -1.00) ? (-1.0f + size / 2) : (posy + (size / 2) >
      1.00) ? (1.0f - size / 2) : posy;
  ratio = ((posx + (size / 2)) - (posx - (size / 2))) * height / width;
  translate = overlay->size_png / 400.0f;
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, overlay->pbuftexture);
  glLoadIdentity ();
  glTranslatef (0.0f, translate, -1.0f);
  if (overlay->rotate_png == 2)
    glRotatef (overlay->angle_png, 0, 1, 0);
  glBegin (GL_QUADS);
  glTexCoord3f (0.0f, 0.0f, 0.0f);
  glVertex3f (posx - (size / 2), posy - (size / 2), 0.0f);
  glTexCoord3f (width, 0.0f, 0.0f);
  glVertex3f (posx + (size / 2), posy - (size / 2), 0.0f);
  glTexCoord3f (width, height, 0.0f);
  glVertex3f (posx + (size / 2), posy - (size / 2) + ratio, 0.0f);
  glTexCoord3f (0.0f, height, 0.0f);
  glVertex3f (posx - (size / 2), posy - (size / 2) + ratio, 0.0f);
  glEnd ();
  glFlush ();
}



static void
gst_gl_overlay_init (GstGLOverlay * overlay, GstGLOverlayClass * klass)
{
  overlay->location = NULL;
  overlay->pixbuf = NULL;
  overlay->pbuftexture = 0;
  overlay->pbuftexture = 0;
  overlay->width = 0;
  overlay->height = 0;
  overlay->pos_x_png = 0;
  overlay->pos_y_png = 0;
  overlay->size_png = 100;
  overlay->pos_x_video = 0;
  overlay->pos_y_video = 0;
  overlay->size_video = 100;
  overlay->video_top = FALSE;
  overlay->rotate_png = 0;
  overlay->rotate_video = 0;
  overlay->angle_png = 0;
  overlay->angle_video = 0;
  //  overlay->stretch = TRUE;
  overlay->pbuf_has_changed = FALSE;
}

static void
gst_gl_overlay_reset_resources (GstGLFilter * filter)
{
  // GstGLOverlay* overlay = GST_GL_OVERLAY(filter);
}

static void
gst_gl_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (object);

  switch (prop_id) {
    case PROP_LOCATION:
      if (overlay->location != NULL)
        g_free (overlay->location);
      overlay->pbuf_has_changed = TRUE;
      overlay->location = g_value_dup_string (value);
      break;
    case PROP_XPOS_PNG:
      overlay->pos_x_png = g_value_get_int (value);
      break;
    case PROP_YPOS_PNG:
      overlay->pos_y_png = g_value_get_int (value);
      break;
    case PROP_SIZE_PNG:
      overlay->size_png = g_value_get_int (value);
      break;
    case PROP_XPOS_VIDEO:
      overlay->pos_x_video = g_value_get_int (value);
      break;
    case PROP_YPOS_VIDEO:
      overlay->pos_y_video = g_value_get_int (value);
      break;
    case PROP_SIZE_VIDEO:
      overlay->size_video = g_value_get_int (value);
      break;
    case PROP_VIDEOTOP:
      overlay->video_top = g_value_get_boolean (value);
      break;
    case PROP_ROTATE_PNG:
      overlay->rotate_png = g_value_get_int (value);
      break;
    case PROP_ROTATE_VIDEO:
      overlay->rotate_video = g_value_get_int (value);
      break;
    case PROP_ANGLE_PNG:
      overlay->angle_png = g_value_get_int (value);
      break;
    case PROP_ANGLE_VIDEO:
      overlay->angle_video = g_value_get_int (value);
      break;
      /*  case PROP_STRETCH:
         overlay->stretch = g_value_get_boolean (value);
         break;
       */
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, overlay->location);
      break;
    case PROP_XPOS_PNG:
      g_value_set_int (value, overlay->pos_x_png);
      break;
    case PROP_YPOS_PNG:
      g_value_set_int (value, overlay->pos_y_png);
      break;
    case PROP_SIZE_PNG:
      g_value_set_int (value, overlay->size_png);
      break;
    case PROP_XPOS_VIDEO:
      g_value_set_int (value, overlay->pos_x_video);
      break;
    case PROP_YPOS_VIDEO:
      g_value_set_int (value, overlay->pos_y_video);
      break;
    case PROP_SIZE_VIDEO:
      g_value_set_int (value, overlay->size_video);
      break;
    case PROP_VIDEOTOP:
      g_value_set_boolean (value, overlay->video_top);
      break;
    case PROP_ROTATE_PNG:
      g_value_set_int (value, overlay->rotate_png);
      break;
    case PROP_ROTATE_VIDEO:
      g_value_set_int (value, overlay->rotate_video);
      break;
    case PROP_ANGLE_PNG:
      g_value_set_int (value, overlay->angle_png);
      break;
    case PROP_ANGLE_VIDEO:
      g_value_set_int (value, overlay->angle_video);
      break;
      /*  case PROP_STRETCH:
         g_value_set_boolean (value, overlay->stretch);
         break;
       */
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_overlay_init_resources (GstGLFilter * filter)
{
//  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);
}

static void
gst_gl_overlay_callback (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (stuff);

  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();
  gluPerspective (70.0f, (GLfloat) width / (GLfloat) height, 0.0f, 1000.0f);
  glMatrixMode (GL_MODELVIEW);
  glLoadIdentity ();
  if (overlay->video_top)
    gst_gl_overlay_draw_texture_video_on_png (overlay, texture);
  else
    gst_gl_overlay_draw_texture_png_on_video (overlay, texture);
}

static void
init_pixbuf_texture (GstGLDisplay * display, gpointer data)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (data);

  if (overlay->pixbuf) {

    glDeleteTextures (1, &overlay->pbuftexture);
    glGenTextures (1, &overlay->pbuftexture);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, overlay->pbuftexture);
    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
        (gint) overlay->width, (gint) overlay->height, 0,
        GL_RGBA, GL_UNSIGNED_BYTE, overlay->pixbuf);
  } else
    display->isAlive = FALSE;
}

static gboolean
gst_gl_overlay_filter (GstGLFilter * filter, GstGLBuffer * inbuf,
    GstGLBuffer * outbuf)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);

  if (overlay->pbuf_has_changed && (overlay->location != NULL)) {

    if (!gst_gl_overlay_loader (filter))
      overlay->pixbuf = NULL;

    /* if loader failed then display is turned off */
    gst_gl_display_thread_add (filter->display, init_pixbuf_texture, overlay);

    if (overlay->pixbuf) {
      free (overlay->pixbuf);
      overlay->pixbuf = NULL;
    }

    overlay->pbuf_has_changed = FALSE;
  }

  gst_gl_filter_render_to_target (filter, inbuf->texture, outbuf->texture,
      gst_gl_overlay_callback, overlay);

  return TRUE;
}

static void
user_warning_fn (png_structp png_ptr, png_const_charp warning_msg)
{
  g_warning ("%s\n", warning_msg);
}

#define LOAD_ERROR(msg) { GST_WARNING ("unable to load %s: %s", overlay->location, msg); return FALSE; }

static gboolean
gst_gl_overlay_loader (GstGLFilter * filter)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);

  png_structp png_ptr;
  png_infop info_ptr;
  guint sig_read = 0;
  png_uint_32 width = 0;
  png_uint_32 height = 0;
  gint bit_depth = 0;
  gint color_type = 0;
  gint interlace_type = 0;
  png_FILE_p fp = NULL;
  guint y = 0;
  guchar **rows = NULL;
  gint filler;

  if (!filter->display)
    return TRUE;

  if ((fp = fopen (overlay->location, "rb")) == NULL)
    LOAD_ERROR ("file not found");

  png_ptr = png_create_read_struct (PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (png_ptr == NULL) {
    fclose (fp);
    LOAD_ERROR ("failed to initialize the png_struct");
  }

  png_set_error_fn (png_ptr, NULL, NULL, user_warning_fn);

  info_ptr = png_create_info_struct (png_ptr);
  if (info_ptr == NULL) {
    fclose (fp);
    png_destroy_read_struct (&png_ptr, png_infopp_NULL, png_infopp_NULL);
    LOAD_ERROR ("failed to initialize the memory for image information");
  }

  png_init_io (png_ptr, fp);

  png_set_sig_bytes (png_ptr, sig_read);

  png_read_info (png_ptr, info_ptr);

  png_get_IHDR (png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
      &interlace_type, int_p_NULL, int_p_NULL);

  if (color_type == PNG_COLOR_TYPE_RGB) {
    filler = 0xff;
    png_set_filler (png_ptr, filler, PNG_FILLER_AFTER);
    color_type = PNG_COLOR_TYPE_RGB_ALPHA;
  }

  if (color_type != PNG_COLOR_TYPE_RGB_ALPHA) {
    fclose (fp);
    png_destroy_read_struct (&png_ptr, png_infopp_NULL, png_infopp_NULL);
    LOAD_ERROR ("color type is not rgb");
  }

  overlay->width = width;
  overlay->height = height;

  overlay->pixbuf = (guchar *) malloc (sizeof (guchar) * width * height * 4);

  rows = (guchar **) malloc (sizeof (guchar *) * height);

  for (y = 0; y < height; ++y)
    rows[y] = (guchar *) (overlay->pixbuf + y * width * 4);

  png_read_image (png_ptr, rows);

  free (rows);

  png_read_end (png_ptr, info_ptr);
  png_destroy_read_struct (&png_ptr, &info_ptr, png_infopp_NULL);
  fclose (fp);

  return TRUE;
}
