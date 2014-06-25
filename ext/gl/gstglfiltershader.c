/*
 * glshader gstreamer plugin
 * Copyrithg (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
 * Copyright (C) 2009 Luc Deschenaux <luc.deschenaux@freesurf.ch>
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
 * SECTION:element-glshader
 *
 * Filter loading OpenGL fragment shader from file
 *
 * <refsect2>
 * <title>Examples</title>
 * |[
 * gst-launch videotestsrc ! glupload ! glshader location=myshader.fs ! glimagesink
 * ]|
 * FBO (Frame Buffer Object) and GLSL (OpenGL Shading Language) are required.
 * </refsect2>
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/gl/gstglshadervariables.h>

#include "gstglfiltershader.h"

/* horizontal filter */
static gchar *hfilter_fragment_source;
static gchar *hfilter_fragment_variables[2];

enum
{
  PROP_0,
  PROP_LOCATION,
  PROP_PRESET,
  PROP_VARIABLES
};

static const gchar text_vertex_shader[] =
    "attribute vec4 a_position;   \n"
    "attribute vec2 a_texcoord;   \n"
    "varying vec2 v_texcoord;     \n"
    "void main()                  \n"
    "{                            \n"
    "   gl_Position = a_position; \n"
    "   v_texcoord = a_texcoord;  \n" "}                            \n";

#define GST_CAT_DEFAULT gst_gl_filtershader_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filtershader_debug, "glshader", 0, "glshader element");

G_DEFINE_TYPE_WITH_CODE (GstGLFilterShader, gst_gl_filtershader,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filtershader_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filtershader_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_gl_filter_filtershader_reset (GstGLFilter * filter);

static gboolean gst_gl_filtershader_load_shader (GstGLFilterShader *
    filter_shader, char *filename, char **storage);
static gboolean gst_gl_filtershader_load_variables (GstGLFilterShader *
    filter_shader, char *filename, char **storage);
static gboolean gst_gl_filtershader_init_shader (GstGLFilter * filter);
static gboolean gst_gl_filtershader_filter (GstGLFilter * filter,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_gl_filtershader_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);
static void gst_gl_filtershader_hcallback (gint width, gint height,
    guint texture, gpointer stuff);


static void
gst_gl_filtershader_init_resources (GstGLFilter * filter)
{
  glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA8,
      GST_VIDEO_INFO_WIDTH (&filter->out_info),
      GST_VIDEO_INFO_HEIGHT (&filter->out_info),
      0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

static void
gst_gl_filtershader_reset_resources (GstGLFilter * filter)
{
  //GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (filter);
}

static void
gst_gl_filtershader_class_init (GstGLFilterShaderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_filtershader_set_property;
  gobject_class->get_property = gst_gl_filtershader_get_property;

  g_object_class_install_property (gobject_class, PROP_LOCATION,
      g_param_spec_string ("location", "File Location",
          "Location of the GLSL file to load", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_PRESET,
      g_param_spec_string ("preset", "Preset File Location",
          "Location of the shader uniform variables preset file", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VARIABLES,
      g_param_spec_string ("vars", "Uniform variables",
          "Set the shader uniform variables", NULL,
          G_PARAM_WRITABLE | GST_PARAM_CONTROLLABLE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class,
      "OpenGL fragment shader filter", "Filter/Effect",
      "Load GLSL fragment shader from file", "<luc.deschenaux@freesurf.ch>");

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filtershader_filter;
  GST_GL_FILTER_CLASS (klass)->filter_texture =
      gst_gl_filtershader_filter_texture;
  GST_GL_FILTER_CLASS (klass)->display_init_cb =
      gst_gl_filtershader_init_resources;
  GST_GL_FILTER_CLASS (klass)->display_reset_cb =
      gst_gl_filtershader_reset_resources;
  GST_GL_FILTER_CLASS (klass)->onInitFBO = gst_gl_filtershader_init_shader;
  GST_GL_FILTER_CLASS (klass)->onReset = gst_gl_filter_filtershader_reset;
}

static void
gst_gl_filtershader_init (GstGLFilterShader * filtershader)
{
  filtershader->shader0 = NULL;
}

static void
gst_gl_filter_filtershader_reset (GstGLFilter * filter)
{
  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (filter);

  //blocking call, wait the opengl thread has destroyed the shader
  if (filtershader->shader0)
    gst_gl_context_del_shader (filter->context, filtershader->shader0);
  filtershader->shader0 = NULL;
}

static void
gst_gl_filtershader_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (object);

  switch (prop_id) {

    case PROP_LOCATION:

      if (filtershader->filename) {
        g_free (filtershader->filename);
      }
      if (filtershader->compiled) {
        //gst_gl_context_del_shader (filtershader->filter.context, filtershader->shader0);
        gst_gl_filter_filtershader_reset (&filtershader->filter);
        filtershader->shader0 = 0;
      }
      filtershader->filename = g_strdup (g_value_get_string (value));
      filtershader->compiled = 0;
      filtershader->texSet = 0;

      break;

    case PROP_PRESET:

      if (filtershader->presetfile) {
        g_free (filtershader->presetfile);
      }

      filtershader->presetfile = g_strdup (g_value_get_string (value));

      if (hfilter_fragment_variables[0]) {
        g_free (hfilter_fragment_variables[0]);
        hfilter_fragment_variables[0] = 0;
      }

      if (!filtershader->presetfile[0]) {
        g_free (filtershader->presetfile);
        filtershader->presetfile = 0;
      }

      break;

    case PROP_VARIABLES:

      if (hfilter_fragment_variables[1]) {
        g_free (hfilter_fragment_variables[1]);
      }

      hfilter_fragment_variables[1] = g_strdup (g_value_get_string (value));

      if (!hfilter_fragment_variables[1][0]) {
        g_free (hfilter_fragment_variables[1]);
        hfilter_fragment_variables[1] = 0;
      }

      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filtershader_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (object);

  switch (prop_id) {
    case PROP_LOCATION:
      g_value_set_string (value, filtershader->filename);
      break;

    case PROP_PRESET:
      g_value_set_string (value, filtershader->presetfile);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_filtershader_load_shader (GstGLFilterShader * filter_shader,
    char *filename, char **storage)
{
  GError *error = NULL;
  gsize length;

  g_return_val_if_fail (storage != NULL, FALSE);

  if (!filename) {
    GST_ELEMENT_ERROR (filter_shader, RESOURCE, NOT_FOUND,
        ("A shader file is required"), (NULL));
    return FALSE;
  }

  if (!g_file_get_contents (filename, storage, &length, &error)) {
    GST_ELEMENT_ERROR (filter_shader, RESOURCE, NOT_FOUND, ("%s",
            error->message), (NULL));
    g_error_free (error);

    return FALSE;
  }

  return TRUE;
}

static gboolean
gst_gl_filtershader_load_variables (GstGLFilterShader * filter_shader,
    char *filename, char **storage)
{
  GError *error = NULL;
  gsize length;

  if (storage[0]) {
    g_free (storage[0]);
    storage[0] = 0;
  }

  if (!filename)
    return TRUE;

  if (!g_file_get_contents (filename, storage, &length, &error)) {
    GST_ELEMENT_ERROR (filter_shader, RESOURCE, NOT_FOUND, ("%s",
            error->message), (NULL));
    g_error_free (error);

    return FALSE;
  }

  return TRUE;
}

static void
gst_gl_filtershader_variables_parse (GstGLShader * shader, gchar * variables)
{
  gst_gl_shadervariables_parse (shader, variables, 0);
}

static gboolean
gst_gl_filtershader_init_shader (GstGLFilter * filter)
{

  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (filter);

  if (!gst_gl_filtershader_load_shader (filtershader, filtershader->filename,
          &hfilter_fragment_source))
    return FALSE;

  //blocking call, wait the opengl thread has compiled the shader
  if (!gst_gl_context_gen_shader (filter->context, text_vertex_shader,
          hfilter_fragment_source, &filtershader->shader0))
    return FALSE;


  if (!gst_gl_filtershader_load_variables (filtershader,
          filtershader->presetfile, &hfilter_fragment_variables[0]))
    return FALSE;

  filtershader->compiled = 1;

  return TRUE;
}

static inline gboolean
_gst_clock_time_to_double (GstClockTime time, gdouble * result)
{
  if (!GST_CLOCK_TIME_IS_VALID (time))
    return FALSE;

  *result = (gdouble) time / GST_SECOND;

  return TRUE;
}

static inline gboolean
_gint64_time_val_to_double (gint64 time, gdouble * result)
{
  if (time == -1)
    return FALSE;

  *result = (gdouble) time / GST_USECOND;

  return TRUE;
}

static gboolean
gst_gl_filtershader_filter (GstGLFilter * filter, GstBuffer * inbuf,
    GstBuffer * outbuf)
{
  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (filter);

  if (!_gst_clock_time_to_double (GST_BUFFER_PTS (inbuf), &filtershader->time)) {
    if (!_gst_clock_time_to_double (GST_BUFFER_DTS (inbuf),
            &filtershader->time))
      _gint64_time_val_to_double (g_get_monotonic_time (), &filtershader->time);
  }

  return gst_gl_filter_filter_texture (filter, inbuf, outbuf);
}

static gboolean
gst_gl_filtershader_filter_texture (GstGLFilter * filter, guint in_tex,
    guint out_tex)
{
  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (filter);

  gst_gl_filter_render_to_target (filter, TRUE, in_tex, out_tex,
      gst_gl_filtershader_hcallback, filtershader);

  return TRUE;
}

static void
gst_gl_filtershader_hcallback (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (filter);
  GstGLFuncs *gl = filter->context->gl_vtable;
  const GLfloat vVertices[] = {
    -1.0f, -1.0f, -1.0f, 0.0f, 0.0f,
    1.0, -1.0f, -1.0f, 1.0f, 0.0f,
    1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
    -1.0f, 1.0f, -1.0f, 0.0f, 1.0f
  };
  GLushort indices[] = { 0, 1, 2, 0, 2, 3 };

#if GST_GL_HAVE_OPENGL
  if (gst_gl_context_get_gl_api (filter->context) & GST_GL_API_OPENGL) {
    gl->MatrixMode (GL_PROJECTION);
    gl->LoadIdentity ();
  }
#endif

  gst_gl_shader_use (filtershader->shader0);

  gl->ActiveTexture (GL_TEXTURE0);
  gl->Enable (GL_TEXTURE_2D);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_shader_set_uniform_1i (filtershader->shader0, "tex", 0);
  gst_gl_shader_set_uniform_1f (filtershader->shader0, "width", width);
  gst_gl_shader_set_uniform_1f (filtershader->shader0, "height", height);
  gst_gl_shader_set_uniform_1f (filtershader->shader0, "time",
      filtershader->time);

  filtershader->attr_position_loc =
      gst_gl_shader_get_attribute_location (filtershader->shader0,
      "a_position");
  filtershader->attr_texture_loc =
      gst_gl_shader_get_attribute_location (filtershader->shader0,
      "a_texcoord");

  if (hfilter_fragment_variables[0]) {
    gst_gl_filtershader_variables_parse (filtershader->shader0,
        hfilter_fragment_variables[0]);
    g_free (hfilter_fragment_variables[0]);
    hfilter_fragment_variables[0] = 0;
  }
  if (hfilter_fragment_variables[1]) {
    gst_gl_filtershader_variables_parse (filtershader->shader0,
        hfilter_fragment_variables[1]);
    g_free (hfilter_fragment_variables[1]);
    hfilter_fragment_variables[1] = 0;
  }

  gl->Clear (GL_COLOR_BUFFER_BIT);

  gl->EnableVertexAttribArray (filtershader->attr_position_loc);
  gl->EnableVertexAttribArray (filtershader->attr_texture_loc);

  /* Load the vertex position */
  gl->VertexAttribPointer (filtershader->attr_position_loc, 3, GL_FLOAT,
      GL_FALSE, 5 * sizeof (GLfloat), vVertices);

  /* Load the texture coordinate */
  gl->VertexAttribPointer (filtershader->attr_texture_loc, 2, GL_FLOAT,
      GL_FALSE, 5 * sizeof (GLfloat), &vVertices[3]);

  gl->DrawElements (GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, indices);

  gl->DisableVertexAttribArray (filtershader->attr_position_loc);
  gl->DisableVertexAttribArray (filtershader->attr_texture_loc);
}
