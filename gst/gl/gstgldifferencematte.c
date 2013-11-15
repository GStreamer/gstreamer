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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-gldifferencematte.
 *
 * Saves a background frame and replace it with a pixbuf.
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch videotestsrc ! glupload ! gldifferencemate location=backgroundimagefile ! glimagesink
 * ]|
 * FBO (Frame Buffer Object) and GLSL (OpenGL Shading Language) are required.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>
#include <png.h>

#include "gstgldifferencematte.h"
#include "effects/gstgleffectssources.h"

#if PNG_LIBPNG_VER >= 10400
#define int_p_NULL         NULL
#define png_infopp_NULL    NULL
#endif

#define GST_CAT_DEFAULT gst_gl_differencematte_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_differencematte_debug, "gldifferencematte", 0, "gldifferencematte element");

G_DEFINE_TYPE_WITH_CODE (GstGLDifferenceMatte, gst_gl_differencematte,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_differencematte_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);
static void gst_gl_differencematte_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_gl_differencematte_init_resources (GstGLFilter * filter);
static void gst_gl_differencematte_reset_resources (GstGLFilter * filter);

static gboolean gst_gl_differencematte_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);

static gboolean gst_gl_differencematte_loader (GstGLFilter * filter);

enum
{
  PROP_0,
  PROP_LOCATION,
};


/* init resources that need a gl context */
static void
gst_gl_differencematte_init_gl_resources (GstGLFilter * filter)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;
  gint i;

  for (i = 0; i < 4; i++) {
    gl->GenTextures (1, &differencematte->midtexture[i]);
    gl->BindTexture (GL_TEXTURE_2D, differencematte->midtexture[i]);
    gl->TexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8,
        GST_VIDEO_INFO_WIDTH (&filter->out_info),
        GST_VIDEO_INFO_HEIGHT (&filter->out_info),
        0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    differencematte->shader[i] = gst_gl_shader_new (filter->context);
  }

  if (!gst_gl_shader_compile_and_check (differencematte->shader[0],
          difference_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_context_set_error (GST_GL_FILTER (differencematte)->context,
        "Failed to initialize difference shader");
    GST_ELEMENT_ERROR (differencematte, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return;
  }

  if (!gst_gl_shader_compile_and_check (differencematte->shader[1],
          hconv7_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_context_set_error (GST_GL_FILTER (differencematte)->context,
        "Failed to initialize hconv7 shader");
    GST_ELEMENT_ERROR (differencematte, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return;
  }

  if (!gst_gl_shader_compile_and_check (differencematte->shader[2],
          vconv7_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_context_set_error (GST_GL_FILTER (differencematte)->context,
        "Failed to initialize vconv7 shader");
    GST_ELEMENT_ERROR (differencematte, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return;
  }

  if (!gst_gl_shader_compile_and_check (differencematte->shader[3],
          texture_interp_fragment_source, GST_GL_SHADER_FRAGMENT_SOURCE)) {
    gst_gl_context_set_error (GST_GL_FILTER (differencematte)->context,
        "Failed to initialize interp shader");
    GST_ELEMENT_ERROR (differencematte, RESOURCE, NOT_FOUND,
        ("%s", gst_gl_context_get_error ()), (NULL));
    return;
  }
}

/* free resources that need a gl context */
static void
gst_gl_differencematte_reset_gl_resources (GstGLFilter * filter)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;
  gint i;

  gl->DeleteTextures (1, &differencematte->savedbgtexture);
  gl->DeleteTextures (1, &differencematte->newbgtexture);
  for (i = 0; i < 4; i++) {
    if (differencematte->shader[i]) {
      gst_object_unref (differencematte->shader[i]);
      differencematte->shader[i] = NULL;
    }
    if (differencematte->midtexture[i]) {
      gl->DeleteTextures (1, &differencematte->midtexture[i]);
      differencematte->midtexture[i] = 0;
    }
  }
  differencematte->location = NULL;
  differencematte->pixbuf = NULL;
  differencematte->savedbgtexture = 0;
  differencematte->newbgtexture = 0;
  differencematte->bg_has_changed = FALSE;
}

static void
gst_gl_differencematte_class_init (GstGLDifferenceMatteClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);
  gobject_class->set_property = gst_gl_differencematte_set_property;
  gobject_class->get_property = gst_gl_differencematte_get_property;

  GST_GL_FILTER_CLASS (klass)->filter_texture =
      gst_gl_differencematte_filter_texture;
  GST_GL_FILTER_CLASS (klass)->display_init_cb =
      gst_gl_differencematte_init_gl_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb =
      gst_gl_differencematte_reset_gl_resources;
  GST_GL_FILTER_CLASS (klass)->onStart = gst_gl_differencematte_init_resources;
  GST_GL_FILTER_CLASS (klass)->onStop = gst_gl_differencematte_reset_resources;

  g_object_class_install_property (gobject_class,
      PROP_LOCATION,
      g_param_spec_string ("location",
          "Background image location",
          "Background image location", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class,
      "Gstreamer OpenGL DifferenceMatte", "Filter/Effect/Video",
      "Saves a background frame and replace it with a pixbuf",
      "Filippo Argiolas <filippo.argiolas@gmail.com>");
}

static void
gst_gl_differencematte_init (GstGLDifferenceMatte * differencematte)
{
  differencematte->shader[0] = NULL;
  differencematte->shader[1] = NULL;
  differencematte->shader[2] = NULL;
  differencematte->shader[3] = NULL;
  differencematte->location = NULL;
  differencematte->pixbuf = NULL;
  differencematte->savedbgtexture = 0;
  differencematte->newbgtexture = 0;
  differencematte->bg_has_changed = FALSE;

  fill_gaussian_kernel (differencematte->kernel, 7, 30.0);
}

static void
gst_gl_differencematte_reset_resources (GstGLFilter * filter)
{
//  GstGLDifferenceMatte* differencematte = GST_GL_DIFFERENCEMATTE(filter);
}

static void
gst_gl_differencematte_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (object);

  switch (prop_id) {
    case PROP_LOCATION:
      if (differencematte->location != NULL)
        g_free (differencematte->location);
      differencematte->bg_has_changed = TRUE;
      differencematte->location = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_differencematte_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, differencematte->location);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_differencematte_init_resources (GstGLFilter * filter)
{
//  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (filter);
}

static void
gst_gl_differencematte_save_texture (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
init_pixbuf_texture (GstGLContext * context, gpointer data)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (data);
  GstGLFilter *filter = GST_GL_FILTER (data);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->DeleteTextures (1, &differencematte->newbgtexture);
  gl->GenTextures (1, &differencematte->newbgtexture);
  gl->BindTexture (GL_TEXTURE_2D, differencematte->newbgtexture);
  gl->TexImage2D (GL_TEXTURE_2D, 0, GL_RGBA,
      (gint) differencematte->pbuf_width, (gint) differencematte->pbuf_height,
      0, GL_RGBA, GL_UNSIGNED_BYTE, differencematte->pixbuf);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  if (differencematte->savedbgtexture == 0) {
    gl->GenTextures (1, &differencematte->savedbgtexture);
    gl->BindTexture (GL_TEXTURE_2D, differencematte->savedbgtexture);
    gl->TexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8,
        GST_VIDEO_INFO_WIDTH (&filter->out_info),
        GST_VIDEO_INFO_HEIGHT (&filter->out_info),
        0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    gl->TexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }
}

static void
gst_gl_differencematte_diff (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (differencematte->shader[0]);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (differencematte->shader[0], "current", 0);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, differencematte->savedbgtexture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (differencematte->shader[0], "saved", 1);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_differencematte_hblur (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (differencematte->shader[1]);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (differencematte->shader[1], "tex", 0);

  gst_gl_shader_set_uniform_1fv (differencematte->shader[1], "kernel", 7,
      differencematte->kernel);
  gst_gl_shader_set_uniform_1f (differencematte->shader[1], "width", width);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_differencematte_vblur (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_shader_use (differencematte->shader[2]);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (differencematte->shader[2], "tex", 0);

  gst_gl_shader_set_uniform_1fv (differencematte->shader[2], "kernel", 7,
      differencematte->kernel);
  gst_gl_shader_set_uniform_1f (differencematte->shader[2], "height", height);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_differencematte_interp (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->MatrixMode (GL_PROJECTION);
  glLoadIdentity ();

  gst_gl_shader_use (differencematte->shader[3]);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (differencematte->shader[3], "blend", 0);

  gl->ActiveTexture (GL_TEXTURE1);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, differencematte->newbgtexture);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (differencematte->shader[3], "base", 1);

  gl->ActiveTexture (GL_TEXTURE2);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, differencematte->midtexture[2]);
  gl->Disable (GL_TEXTURE_2D);

  gst_gl_shader_set_uniform_1i (differencematte->shader[3], "alpha", 2);

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static void
gst_gl_differencematte_identity (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (stuff);
  GstGLFilter *filter = GST_GL_FILTER (differencematte);
  GstGLFuncs *gl = filter->context->gl_vtable;

  gl->MatrixMode (GL_PROJECTION);
  gl->LoadIdentity ();

  gst_gl_filter_draw_texture (filter, texture, width, height);
}

static gboolean
gst_gl_differencematte_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (filter);

  differencematte->intexture = in_tex;

  if (differencematte->bg_has_changed && (differencematte->location != NULL)) {

    if (!gst_gl_differencematte_loader (filter))
      differencematte->pixbuf = NULL;

    /* if loader failed then context is turned off */
    gst_gl_context_thread_add (filter->context, init_pixbuf_texture,
        differencematte);

    /* save current frame, needed to calculate difference between
     * this frame and next ones */
    gst_gl_filter_render_to_target (filter, TRUE, in_tex,
        differencematte->savedbgtexture,
        gst_gl_differencematte_save_texture, differencematte);

    if (differencematte->pixbuf) {
      free (differencematte->pixbuf);
      differencematte->pixbuf = NULL;
    }

    differencematte->bg_has_changed = FALSE;
  }

  if (differencematte->savedbgtexture != 0) {
    gst_gl_filter_render_to_target (filter, TRUE, in_tex,
        differencematte->midtexture[0], gst_gl_differencematte_diff,
        differencematte);
    gst_gl_filter_render_to_target (filter, FALSE,
        differencematte->midtexture[0], differencematte->midtexture[1],
        gst_gl_differencematte_hblur, differencematte);
    gst_gl_filter_render_to_target (filter, FALSE,
        differencematte->midtexture[1], differencematte->midtexture[2],
        gst_gl_differencematte_vblur, differencematte);
    gst_gl_filter_render_to_target (filter, TRUE, in_tex, out_tex,
        gst_gl_differencematte_interp, differencematte);
  } else {
    gst_gl_filter_render_to_target (filter, TRUE, in_tex, out_tex,
        gst_gl_differencematte_identity, differencematte);
  }

  return TRUE;
}

static void
user_warning_fn (png_structp png_ptr, png_const_charp warning_msg)
{
  g_warning ("%s\n", warning_msg);
}

#define LOAD_ERROR(msg) { GST_WARNING ("unable to load %s: %s", differencematte->location, msg); return FALSE; }

static gboolean
gst_gl_differencematte_loader (GstGLFilter * filter)
{
  GstGLDifferenceMatte *differencematte = GST_GL_DIFFERENCEMATTE (filter);

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

  if (!filter->context)
    return TRUE;

  if ((fp = fopen (differencematte->location, "rb")) == NULL)
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

  differencematte->pbuf_width = width;
  differencematte->pbuf_height = height;

  differencematte->pixbuf =
      (guchar *) malloc (sizeof (guchar) * width * height * 4);

  rows = (guchar **) malloc (sizeof (guchar *) * height);

  for (y = 0; y < height; ++y)
    rows[y] = (guchar *) (differencematte->pixbuf + y * width * 4);

  png_read_image (png_ptr, rows);

  free (rows);

  png_read_end (png_ptr, info_ptr);
  png_destroy_read_struct (&png_ptr, &info_ptr, png_infopp_NULL);
  fclose (fp);

  return TRUE;
}
