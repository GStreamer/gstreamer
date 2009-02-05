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
 * SECTION:element-glpixbufoverlay
 *
 * Overlay GL video texture with a gdkpixbuf.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch  videotestsrc ! "video/x-raw-rgb" ! glupload ! glpixbufoverlay location=imagefile ! glimagesink
 * ]|
 * FBO (Frame Buffer Object) is required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <png.h>
#include <gstglfilter.h>
#include <gstgleffectssources.h>

#define GST_TYPE_GL_PIXBUFOVERLAY            (gst_gl_pixbufoverlay_get_type())
#define GST_GL_PIXBUFOVERLAY(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_GL_PIXBUFOVERLAY,GstGLPixbufOverlay))
#define GST_IS_GL_PIXBUFOVERLAY(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_GL_PIXBUFOVERLAY))
#define GST_GL_PIXBUFOVERLAY_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass) , GST_TYPE_GL_PIXBUFOVERLAY,GstGLPixbufOverlayClass))
#define GST_IS_GL_PIXBUFOVERLAY_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass) , GST_TYPE_GL_PIXBUFOVERLAY))
#define GST_GL_PIXBUFOVERLAY_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj) , GST_TYPE_GL_PIXBUFOVERLAY,GstGLPixbufOverlayClass))

struct _GstGLPixbufOverlay
{
  GstGLFilter filter;

  gchar *location;
  gboolean pbuf_has_changed;

  guchar *pixbuf;
  gint width, height;
  GLuint pbuftexture;

//  gboolean stretch;
};

struct _GstGLPixbufOverlayClass
{
  GstGLFilterClass filter_class;
};

typedef struct _GstGLPixbufOverlay GstGLPixbufOverlay;
typedef struct _GstGLPixbufOverlayClass GstGLPixbufOverlayClass;

#define GST_CAT_DEFAULT gst_gl_pixbufoverlay_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT(bla)							\
  GST_DEBUG_CATEGORY_INIT (gst_gl_pixbufoverlay_debug, "glpixbufoverlay", 0, "glpixbufoverlay element");

GST_BOILERPLATE_FULL (GstGLPixbufOverlay, gst_gl_pixbufoverlay, GstGLFilter,
		      GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_pixbufoverlay_set_property (GObject * object, guint prop_id,
					 const GValue * value, GParamSpec * pspec);
static void gst_gl_pixbufoverlay_get_property (GObject * object, guint prop_id,
					 GValue * value, GParamSpec * pspec);

static void gst_gl_pixbufoverlay_init_resources (GstGLFilter* filter);
static void gst_gl_pixbufoverlay_reset_resources (GstGLFilter* filter);

static gboolean gst_gl_pixbufoverlay_filter (GstGLFilter * filter,
				       GstGLBuffer * inbuf, GstGLBuffer * outbuf);

static gboolean gst_gl_pixbufoverlay_loader (GstGLFilter* filter);

static const GstElementDetails element_details = GST_ELEMENT_DETAILS (
  "Gstreamer OpenGL PixbufOverlay",
  "Filter/Effect",
  "Overlay GL video texture with a gdkpixbuf",
  "Filippo Argiolas <filippo.argiolas@gmail.com>");

enum
{
  PROP_0,
  PROP_LOCATION,
//  PROP_STRETCH,
  /* future properties? */
  /* PROP_WIDTH, */
  /* PROP_HEIGHT, */
  /* PROP_XPOS, */
  /* PROP_YPOS */
};


/* init resources that need a gl context */
static void
gst_gl_pixbufoverlay_init_gl_resources (GstGLFilter *filter)
{
//  GstGLPixbufOverlay *pixbufoverlay = GST_GL_PIXBUFOVERLAY (filter);
}

/* free resources that need a gl context */
static void
gst_gl_pixbufoverlay_reset_gl_resources (GstGLFilter *filter)
{
  GstGLPixbufOverlay *pixbufoverlay = GST_GL_PIXBUFOVERLAY (filter);
  
  glDeleteTextures (1, &pixbufoverlay->pbuftexture);
}

static void
gst_gl_pixbufoverlay_base_init (gpointer klass)
{
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  gst_element_class_set_details (element_class, &element_details);
}

static void
gst_gl_pixbufoverlay_class_init (GstGLPixbufOverlayClass * klass)
{
  GObjectClass *gobject_class;

  gobject_class = (GObjectClass *) klass;
  gobject_class->set_property = gst_gl_pixbufoverlay_set_property;
  gobject_class->get_property = gst_gl_pixbufoverlay_get_property;

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_pixbufoverlay_filter;
  GST_GL_FILTER_CLASS (klass)->display_init_cb = gst_gl_pixbufoverlay_init_gl_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb = gst_gl_pixbufoverlay_reset_gl_resources;
  GST_GL_FILTER_CLASS (klass)->onStart = gst_gl_pixbufoverlay_init_resources;
  GST_GL_FILTER_CLASS (klass)->onStop = gst_gl_pixbufoverlay_reset_resources;

  g_object_class_install_property (gobject_class,
                                   PROP_LOCATION,
                                   g_param_spec_string ("location",
                                                        "Location of the image", 
                                                        "Location of the image", 
                                                        NULL, G_PARAM_READWRITE));
  /*
  g_object_class_install_property (gobject_class,
                                   PROP_STRETCH,
                                   g_param_spec_boolean ("stretch",
                                                         "Stretch the image to texture size", 
                                                         "Stretch the image to fit video texture size", 
                                                         TRUE, G_PARAM_READWRITE));
  */
}

void
gst_gl_pixbufoverlay_draw_texture (GstGLPixbufOverlay * pixbufoverlay, GLuint tex)
{
  GstGLFilter *filter = GST_GL_FILTER (pixbufoverlay);

  gfloat width = (gfloat) filter->width;
  gfloat height = (gfloat) filter->height;

  glActiveTexture (GL_TEXTURE0);
  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, tex);

  glBegin (GL_QUADS);

  glTexCoord2f (0.0, 0.0);
  glVertex2f (-1.0, -1.0);
  glTexCoord2f (width, 0.0);
  glVertex2f (1.0, -1.0);
  glTexCoord2f (width, height);
  glVertex2f (1.0, 1.0);
  glTexCoord2f (0.0, height);
  glVertex2f (-1.0, 1.0);

  glEnd ();

  if (pixbufoverlay->pbuftexture == 0) return;

//  if (pixbufoverlay->stretch) {
    width = (gfloat) pixbufoverlay->width;
    height = (gfloat) pixbufoverlay->height;
//  }

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glEnable(GL_BLEND);

  glEnable (GL_TEXTURE_RECTANGLE_ARB);
  glBindTexture (GL_TEXTURE_RECTANGLE_ARB, pixbufoverlay->pbuftexture);

  glBegin (GL_QUADS);

  glTexCoord2f (0.0, 0.0);
  glVertex2f (-1.0, -1.0);
  glTexCoord2f (width, 0.0);
  glVertex2f (1.0, -1.0);
  glTexCoord2f (width, height);
  glVertex2f (1.0, 1.0);
  glTexCoord2f (0.0, height);
  glVertex2f (-1.0, 1.0);

  glEnd ();


  glFlush ();
}

static void
gst_gl_pixbufoverlay_init (GstGLPixbufOverlay * pixbufoverlay, 
                           GstGLPixbufOverlayClass * klass)
{
  pixbufoverlay->location = NULL;
  pixbufoverlay->pixbuf = NULL;
  pixbufoverlay->pbuftexture = 0;
  pixbufoverlay->pbuftexture = 0;
  pixbufoverlay->width = 0;
  pixbufoverlay->height = 0;
//  pixbufoverlay->stretch = TRUE;
  pixbufoverlay->pbuf_has_changed = FALSE;
}

static void
gst_gl_pixbufoverlay_reset_resources (GstGLFilter* filter)
{
  // GstGLPixbufOverlay* pixbufoverlay = GST_GL_PIXBUFOVERLAY(filter);
}

static void
gst_gl_pixbufoverlay_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLPixbufOverlay *pixbufoverlay = GST_GL_PIXBUFOVERLAY (object); 

  switch (prop_id) {
  case PROP_LOCATION:
    if (pixbufoverlay->location != NULL) g_free (pixbufoverlay->location);
    pixbufoverlay->pbuf_has_changed = TRUE;
    pixbufoverlay->location = g_value_dup_string (value);
    break;
/*  case PROP_STRETCH:
    pixbufoverlay->stretch = g_value_get_boolean (value);
    break;
*/
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_pixbufoverlay_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLPixbufOverlay *pixbufoverlay = GST_GL_PIXBUFOVERLAY (object);

  switch (prop_id) {
  case PROP_LOCATION:
    g_value_set_string (value, pixbufoverlay->location);
    break;
/*  case PROP_STRETCH:
    g_value_set_boolean (value, pixbufoverlay->stretch);
    break;
*/
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    break;
  }
}

static void
gst_gl_pixbufoverlay_init_resources (GstGLFilter* filter)
{
//  GstGLPixbufOverlay *pixbufoverlay = GST_GL_PIXBUFOVERLAY (filter);
}

static void
gst_gl_pixbufoverlay_callback (gint width, gint height, guint texture, gpointer stuff)
{
  GstGLPixbufOverlay* pixbufoverlay = GST_GL_PIXBUFOVERLAY (stuff);
  
  glMatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_pixbufoverlay_draw_texture (pixbufoverlay, texture);
}

static void init_pixbuf_texture (GstGLDisplay *display, gpointer data)
{
  GstGLPixbufOverlay *pixbufoverlay = GST_GL_PIXBUFOVERLAY (data);
  
  if (pixbufoverlay->pixbuf) {

    glDeleteTextures (1, &pixbufoverlay->pbuftexture);
    glGenTextures (1, &pixbufoverlay->pbuftexture);
    glBindTexture (GL_TEXTURE_RECTANGLE_ARB, pixbufoverlay->pbuftexture);
    glTexImage2D (GL_TEXTURE_RECTANGLE_ARB, 0, GL_RGBA,
                  (gint)pixbufoverlay->width, (gint)pixbufoverlay->height, 0,
                  GL_RGBA, GL_UNSIGNED_BYTE, pixbufoverlay->pixbuf);
  }
  else
    display->isAlive = FALSE;
}

static gboolean
gst_gl_pixbufoverlay_filter (GstGLFilter* filter, GstGLBuffer* inbuf,
				GstGLBuffer* outbuf)
{
  GstGLPixbufOverlay* pixbufoverlay = GST_GL_PIXBUFOVERLAY(filter);

  if (pixbufoverlay->pbuf_has_changed && (pixbufoverlay->location != NULL)) {

    if (!gst_gl_pixbufoverlay_loader (filter))
      pixbufoverlay->pixbuf = NULL;
    
    /* if loader failed then display is turned off */
    gst_gl_display_thread_add (filter->display, init_pixbuf_texture, pixbufoverlay);

    if (pixbufoverlay->pixbuf) {
      free (pixbufoverlay->pixbuf);
      pixbufoverlay->pixbuf = NULL;
    }

    pixbufoverlay->pbuf_has_changed = FALSE;
  }
  
  gst_gl_filter_render_to_target (filter, inbuf->texture, outbuf->texture,
				  gst_gl_pixbufoverlay_callback, pixbufoverlay);

  return TRUE;
}

static void 
user_warning_fn(png_structp png_ptr, png_const_charp warning_msg)
{
    g_warning("%s\n", warning_msg);
}

#define LOAD_ERROR(msg) { GST_WARNING ("unable to load %s: %s", pixbufoverlay->location, msg); return FALSE; }

static gboolean
gst_gl_pixbufoverlay_loader (GstGLFilter* filter)
{
  GstGLPixbufOverlay* pixbufoverlay = GST_GL_PIXBUFOVERLAY(filter);

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

  if (!filter->display)
    return TRUE;

  if ((fp = fopen(pixbufoverlay->location, "rb")) == NULL)
    LOAD_ERROR ("file not found");

  png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);

  if (png_ptr == NULL)
  {
    fclose(fp);
    LOAD_ERROR ("failed to initialize the png_struct");
  }

  png_set_error_fn (png_ptr, NULL, NULL, user_warning_fn);

  info_ptr = png_create_info_struct(png_ptr);
  if (info_ptr == NULL)
  {
    fclose(fp);
    png_destroy_read_struct(&png_ptr, png_infopp_NULL, png_infopp_NULL);
    LOAD_ERROR ("failed to initialize the memory for image information");
  }

  png_init_io(png_ptr, fp);

  png_set_sig_bytes(png_ptr, sig_read);

  png_read_info(png_ptr, info_ptr);

  png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
     &interlace_type, int_p_NULL, int_p_NULL);

  if (color_type != PNG_COLOR_TYPE_RGB_ALPHA)
  {
    fclose(fp);
    png_destroy_read_struct(&png_ptr, png_infopp_NULL, png_infopp_NULL);
    LOAD_ERROR ("color type is not rgb");
  }

  pixbufoverlay->width = width;
  pixbufoverlay->height = height;

  pixbufoverlay->pixbuf = (guchar*) malloc ( sizeof(guchar) * width * height * 4 );

  rows = (guchar**)malloc(sizeof(guchar*) * height);

  for (y = 0;  y < height; ++y)
    rows[y] = (guchar*) (pixbufoverlay->pixbuf + y * width * 4);

  png_read_image(png_ptr, rows);

  free(rows);

  png_read_end(png_ptr, info_ptr);
  png_destroy_read_struct(&png_ptr, &info_ptr, png_infopp_NULL);
  fclose(fp);

  return TRUE;
}
