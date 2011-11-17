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

#define DEBUG_INIT(bla) \
  GST_DEBUG_CATEGORY_INIT (gst_gl_overlay_debug, "gloverlay", 0, "gloverlay element");

GST_BOILERPLATE_FULL (GstGLOverlay, gst_gl_overlay, GstGLFilter,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static gboolean gst_gl_overlay_set_caps (GstGLFilter * filter,
    GstCaps * incaps, GstCaps * outcaps);

static void gst_gl_overlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_overlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static void gst_gl_overlay_init_resources (GstGLFilter * filter);
static void gst_gl_overlay_reset_resources (GstGLFilter * filter);

static gboolean gst_gl_overlay_filter (GstGLFilter * filter,
    GstGLBuffer * inbuf, GstGLBuffer * outbuf);

static gboolean gst_gl_overlay_loader (GstGLFilter * filter);

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
  PROP_ANGLE_VIDEO,
  PROP_RATIO_VIDEO
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

  gst_element_class_set_details_simple (element_class,
      "Gstreamer OpenGL Overlay", "Filter/Effect",
      "Overlay GL video texture with a PNG image",
      "Filippo Argiolas <filippo.argiolas@gmail.com>");
}

static void
gst_gl_overlay_class_init (GstGLOverlayClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_overlay_set_property;
  gobject_class->get_property = gst_gl_overlay_get_property;

  GST_GL_FILTER_CLASS (klass)->set_caps = gst_gl_overlay_set_caps;
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
          "Location of the image", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_XPOS_PNG,
      g_param_spec_int ("xpos-png",
          "X position of overlay image in percents",
          "X position of overlay image in percents",
          0, 100, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_YPOS_PNG,
      g_param_spec_int ("ypos-png",
          "Y position of overlay image in percents",
          "Y position of overlay image in percents",
          0, 100, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SIZE_PNG,
      g_param_spec_int ("proportion-png",
          "Relative size of overlay image, in percents",
          "Relative size of iverlay image, in percents",
          0, 100, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_XPOS_VIDEO,
      g_param_spec_int ("xpos-video",
          "X position of overlay video in percents",
          "X position of overlay video in percents",
          0, 100, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_YPOS_VIDEO,
      g_param_spec_int ("ypos-video",
          "Y position of overlay video in percents",
          "Y position of overlay video in percents",
          0, 100, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_SIZE_VIDEO,
      g_param_spec_int ("proportion-video",
          "Relative size of overlay video, in percents",
          "Relative size of iverlay video, in percents",
          0, 100, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_VIDEOTOP,
      g_param_spec_boolean ("video-top",
          "Video-top", "Video is over png image", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ROTATE_PNG,
      g_param_spec_int ("rotate-png",
          "choose rotation axis for the moment only Y axis is implemented",
          "choose rotation axis for the moment only Y axis is implemented",
          0, 3, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ROTATE_VIDEO,
      g_param_spec_int ("rotate-video",
          "choose rotation axis for the moment only Y axis is implemented",
          "choose rotation axis for the moment only Y axis is implemented",
          0, 3, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ANGLE_PNG,
      g_param_spec_int ("angle-png",
          "choose angle in axis to choosen between -90 and 90",
          "choose angle in axis to choosen between -90 and 90",
          -90, 90, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_ANGLE_VIDEO,
      g_param_spec_int ("angle-video",
          "choose angle in axis to choosen between -90 and 90",
          "choose angle in axis to choosen between -90 and 90",
          -90, 90, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class,
      PROP_RATIO_VIDEO,
      g_param_spec_int ("ratio-video",
          "choose ratio video between 0 and 3\n \t\t\t0 : Default ratio\n\t\t\t1 : 4 / 3\n\t\t\t2 : 16 / 9\n\t\t\t3 : 16 / 10",
          "choose ratio video between 0 and 3\n \t\t\t0 : Default ratio\n\t\t\t1 : 4 / 3\n\t\t\t2 : 16 / 9\n\t\t\t3 : 16 / 10",
          0, 3, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  /*
     g_object_class_install_property (gobject_class,
     PROP_STRETCH,
     g_param_spec_boolean ("stretch",
     "Stretch the image to texture size",
     "Stretch the image to fit video texture size",
     TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
   */
}

static void
gst_gl_overlay_draw_texture_video (GstGLOverlay * o, GLuint tex)
{
  gfloat video_ratio_w;
  gfloat video_ratio_h;

  o->ratio_window = (gfloat) o->width_window / (gfloat) o->height_window;
  glMatrixMode (GL_MODELVIEW);
  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, tex);
  glTranslatef (0.0f, 0.0f, -1.43f);
  glScalef (1.0, 1.0, 1.0);
  if (o->rotate_video)
    glRotatef (o->angle_video, 0, 1, 0);
  if (o->ratio_video == 0) {
    o->ratio_texture = (gfloat) o->ratio_window;
    video_ratio_w = (gfloat) o->width_window;
    video_ratio_h = (gfloat) o->height_window;
  }
  if (o->ratio_video == 1) {
    o->ratio_texture = (gfloat) 1.33;
    video_ratio_w = 4.0;
    video_ratio_h = 3.0;
  } else if (o->ratio_video == 2) {
    o->ratio_texture = (gfloat) 1.77;
    video_ratio_w = 16.0;
    video_ratio_h = 9.0;
  } else {
    o->ratio_texture = (gfloat) 1.6;
    video_ratio_w = 16.0;
    video_ratio_h = 10.0;
  }
  if ((1.59 < o->ratio_window && o->ratio_window < 1.61
          && 1.77 < o->ratio_texture && o->ratio_texture < 1.78)
      || (1.3 < o->ratio_window && o->ratio_window < 1.34
          && ((1.7 < o->ratio_texture && o->ratio_texture < 1.78)
              || (1.59 < o->ratio_texture && o->ratio_texture < 1.61)))) {
    o->ratio_x = o->ratio_window * (gfloat) o->size_video / 100.0;
    o->ratio_y =
        (o->ratio_window / video_ratio_w) * video_ratio_h *
        (gfloat) o->size_video / 100.0;
  } else {
    o->ratio_x = o->ratio_texture * (gfloat) o->size_video / 100.0;
    o->ratio_y = 1.0 * o->size_video / 100.0;
  }
  o->posx = ((o->ratio_window - o->ratio_x) * (o->pos_x_video - 50.0) / 50.0);
  o->posy = (1.0 - o->ratio_y) * ((o->pos_y_video - 50.0) / 50.0);
  glBegin (GL_QUADS);
  glTexCoord3f (0.0f, 0.0f, 0.0f);
  glVertex3f (-o->ratio_x + o->posx, -o->ratio_y + o->posy, 0.0f);
  glTexCoord3f (o->width_window, 0.0, 0.0);
  glVertex3f (o->ratio_x + o->posx, -o->ratio_y + o->posy, 0.0f);
  glTexCoord3f (o->width_window, o->height_window, 0.0);
  glVertex3f (o->ratio_x + o->posx, o->ratio_y + o->posy, 0.0);
  glTexCoord3f (0.0, o->height_window, 0.0);
  glVertex3f (-o->ratio_x + o->posx, o->ratio_y + o->posy, 0.0);
  glEnd ();
}

static void
gst_gl_overlay_draw_texture_png (GstGLOverlay * o)
{
  o->ratio_window = (gfloat) o->width_window / (gfloat) o->height_window;
  o->ratio_texture = (gfloat) o->width / (gfloat) o->height;
  glMatrixMode (GL_MODELVIEW);
  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable (GL_BLEND);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, o->pbuftexture);
  glTranslatef (0.0f, 0.0f, -1.43f);
  glScalef (1.0, 1.0, 1.0);
  o->ratio_texture = (gfloat) o->width / (gfloat) o->height;
  if (o->rotate_png == 2)
    glRotatef (o->angle_png, 0, 1, 0);
  if ((1.59 < o->ratio_window && o->ratio_window < 1.61
          && 1.77 < o->ratio_texture && o->ratio_texture < 1.78)
      || (1.3 < o->ratio_window && o->ratio_window < 1.34
          && ((1.7 < o->ratio_texture && o->ratio_texture < 1.78)
              || (1.59 < o->ratio_texture && o->ratio_texture < 1.61)))) {
    o->ratio_x = o->ratio_window * (gfloat) o->size_png / 100.0;
    o->ratio_y =
        ((o->ratio_window / o->width) * o->height) * (gfloat) o->size_png /
        100.0;
  } else {
    o->ratio_x = o->ratio_texture * (gfloat) o->size_png / 100.0;
    o->ratio_y = 1.0 * o->size_png / 100.0;
  }
  o->posx = ((o->ratio_window - o->ratio_x) * (o->pos_x_png - 50.0) / 50.0);
  o->posy = (1.0 - o->ratio_y) * ((o->pos_y_png - 50.0) / 50.0);
  glBegin (GL_POLYGON);
  glTexCoord3f (0.0f, 0.0f, 0.0f);
  glVertex3f (-o->ratio_x + o->posx, -o->ratio_y + o->posy, 0.0f);
  glTexCoord3f (o->width, 0.0, 0.0);
  glVertex3f (o->ratio_x + o->posx, -o->ratio_y + o->posy, 0.0f);
  glTexCoord3f (o->width, o->height, 0.0);
  glVertex3f (o->ratio_x + o->posx, o->ratio_y + o->posy, 0.0);
  glTexCoord3f (0.0, o->height, 0.0);
  glVertex3f (-o->ratio_x + o->posx, o->ratio_y + o->posy, 0.0);
  glEnd ();
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
  overlay->ratio_video = 0;
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
    case PROP_RATIO_VIDEO:
      overlay->ratio_video = g_value_get_int (value);
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
    case PROP_RATIO_VIDEO:
      g_value_set_int (value, overlay->ratio_video);
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

static gboolean
gst_gl_overlay_set_caps (GstGLFilter * filter, GstCaps * incaps,
    GstCaps * outcaps)
{
  GstGLOverlay *overlay = GST_GL_OVERLAY (filter);
  gchar *str_caps;
  GstStructure *structure;
  const GValue *width_value;

  structure = gst_caps_get_structure (incaps, 0);
  width_value = gst_structure_get_value (structure, "width");
  str_caps = g_strdup_value_contents (width_value);
  overlay->width_window = atof (str_caps);
  g_free (str_caps);
  width_value = gst_structure_get_value (structure, "height");
  str_caps = g_strdup_value_contents (width_value);
  overlay->height_window = atof (str_caps);
  g_free (str_caps);

  return TRUE;
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
  gluPerspective (70.0f,
      (GLfloat) overlay->width_window / (GLfloat) overlay->height_window, 1.0f,
      1000.0f);
  glEnable (GL_DEPTH_TEST);
  gluLookAt (0.0, 0.0, 0.01, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0);
  if (overlay->video_top) {
    gst_gl_overlay_draw_texture_png (overlay);
    if (overlay->pbuftexture == 0)
      return;
    // if (overlay->stretch) {
    //   width = (gfloat) overlay->width;
    //   height = (gfloat) overlay->height;
    // }
    glLoadIdentity ();
    gst_gl_overlay_draw_texture_video (overlay, texture);
  } else {
    gst_gl_overlay_draw_texture_video (overlay, texture);
    if (overlay->pbuftexture == 0)
      return;
    // if (overlay->stretch) {
    //   width = (gfloat) overlay->width;
    //   height = (gfloat) overlay->height;
    // }
    glLoadIdentity ();
    gst_gl_overlay_draw_texture_png (overlay);
  }
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
