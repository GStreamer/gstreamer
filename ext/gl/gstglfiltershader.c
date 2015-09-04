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

enum
{
  PROP_0,
  PROP_SHADER,
  PROP_VERTEX,
  PROP_FRAGMENT,
  PROP_LAST,
};

enum
{
  SIGNAL_0,
  SIGNAL_CREATE_SHADER,
  SIGNAL_LAST,
};

static guint gst_gl_shader_signals[SIGNAL_LAST] = { 0 };

#define GST_CAT_DEFAULT gst_gl_filtershader_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_filtershader_debug, "glshader", 0, "glshader element");
#define gst_gl_filtershader_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstGLFilterShader, gst_gl_filtershader,
    GST_TYPE_GL_FILTER, DEBUG_INIT);

static void gst_gl_filtershader_finalize (GObject * object);
static void gst_gl_filtershader_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_filtershader_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static gboolean gst_gl_filtershader_gl_start (GstGLBaseFilter * base);
static void gst_gl_filtershader_gl_stop (GstGLBaseFilter * base);
static gboolean gst_gl_filtershader_filter (GstGLFilter * filter,
    GstBuffer * inbuf, GstBuffer * outbuf);
static gboolean gst_gl_filtershader_filter_texture (GstGLFilter * filter,
    guint in_tex, guint out_tex);
static void gst_gl_filtershader_hcallback (gint width, gint height,
    guint texture, gpointer stuff);

static void
gst_gl_filtershader_class_init (GstGLFilterShaderClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *element_class;

  gobject_class = (GObjectClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->finalize = gst_gl_filtershader_finalize;
  gobject_class->set_property = gst_gl_filtershader_set_property;
  gobject_class->get_property = gst_gl_filtershader_get_property;

  g_object_class_install_property (gobject_class, PROP_SHADER,
      g_param_spec_object ("shader", "Shader object",
          "GstGLShader to use", GST_GL_TYPE_SHADER,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_VERTEX,
      g_param_spec_string ("vertex", "Vertex Source",
          "GLSL vertex source", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_property (gobject_class, PROP_FRAGMENT,
      g_param_spec_string ("fragment", "Fragment Source",
          "GLSL fragment source", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  /* FIXME: add other stages */

  gst_gl_shader_signals[SIGNAL_CREATE_SHADER] =
      g_signal_new ("create-shader", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      GST_GL_TYPE_SHADER, 0);

  gst_element_class_set_metadata (element_class,
      "OpenGL fragment shader filter", "Filter/Effect",
      "Perform operations with a GLSL shader", "<matthew@centricular.com>");

  GST_GL_FILTER_CLASS (klass)->filter = gst_gl_filtershader_filter;
  GST_GL_FILTER_CLASS (klass)->filter_texture =
      gst_gl_filtershader_filter_texture;

  GST_GL_BASE_FILTER_CLASS (klass)->gl_start = gst_gl_filtershader_gl_start;
  GST_GL_BASE_FILTER_CLASS (klass)->gl_stop = gst_gl_filtershader_gl_stop;
  GST_GL_BASE_FILTER_CLASS (klass)->supported_gl_api =
      GST_GL_API_OPENGL | GST_GL_API_GLES2 | GST_GL_API_OPENGL3;
}

static void
gst_gl_filtershader_init (GstGLFilterShader * filtershader)
{
  filtershader->new_source = TRUE;
}

static void
gst_gl_filtershader_finalize (GObject * object)
{
  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (object);

  if (filtershader->shader_prop) {
    gst_object_unref (filtershader->shader_prop);
    filtershader->shader_prop = NULL;
  }

  G_OBJECT_CLASS (gst_gl_filtershader_parent_class)->finalize (object);
}

static void
gst_gl_filtershader_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (object);

  switch (prop_id) {
    case PROP_SHADER:
      GST_OBJECT_LOCK (filtershader);
      gst_object_replace ((GstObject **) & filtershader->shader_prop,
          g_value_dup_object (value));
      filtershader->new_source = TRUE;
      GST_OBJECT_UNLOCK (filtershader);
      break;
    case PROP_VERTEX:
      GST_OBJECT_LOCK (filtershader);
      if (filtershader->vertex)
        g_free (filtershader->vertex);
      filtershader->vertex = g_value_dup_string (value);
      filtershader->new_source = TRUE;
      GST_OBJECT_UNLOCK (filtershader);
      break;
    case PROP_FRAGMENT:
      GST_OBJECT_LOCK (filtershader);
      if (filtershader->fragment)
        g_free (filtershader->fragment);
      filtershader->fragment = g_value_dup_string (value);
      filtershader->new_source = TRUE;
      GST_OBJECT_UNLOCK (filtershader);
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
    case PROP_SHADER:
      GST_OBJECT_LOCK (filtershader);
      g_value_set_object (value, filtershader->shader_prop);
      GST_OBJECT_UNLOCK (filtershader);
      break;
    case PROP_VERTEX:
      GST_OBJECT_LOCK (filtershader);
      g_value_set_string (value, filtershader->vertex);
      GST_OBJECT_UNLOCK (filtershader);
      break;
    case PROP_FRAGMENT:
      GST_OBJECT_LOCK (filtershader);
      g_value_set_string (value, filtershader->fragment);
      GST_OBJECT_UNLOCK (filtershader);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_filtershader_gl_stop (GstGLBaseFilter * base)
{
  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (base);

  if (filtershader->shader)
    gst_object_unref (filtershader->shader);
  filtershader->shader = NULL;

  GST_GL_BASE_FILTER_CLASS (parent_class)->gl_stop (base);
}

static gboolean
gst_gl_filtershader_gl_start (GstGLBaseFilter * base)
{
  return GST_GL_BASE_FILTER_CLASS (parent_class)->gl_start (base);
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

  if (!filtershader->shader)
    return FALSE;

  return TRUE;
}

static GstGLShader *
_maybe_recompile_shader (GstGLFilterShader * filtershader)
{
  GstGLContext *context = GST_GL_BASE_FILTER (filtershader)->context;
  GstGLShader *shader;
  GError *error = NULL;

  GST_OBJECT_LOCK (filtershader);

  if (filtershader->shader_prop) {
    shader = gst_object_ref (filtershader->shader_prop);
    GST_OBJECT_UNLOCK (filtershader);
    return shader;
  }
  if (!filtershader->shader) {
    g_signal_emit (filtershader, gst_gl_shader_signals[SIGNAL_CREATE_SHADER], 0,
        &filtershader->shader);
    if (filtershader->shader) {
      filtershader->new_source = FALSE;
      shader = gst_object_ref (filtershader->shader);
      GST_OBJECT_UNLOCK (filtershader);
      return shader;
    }
  }

  if (filtershader->new_source) {
    GstGLSLStage *stage;

    shader = gst_gl_shader_new (context);

    if (filtershader->vertex) {
      if (!(stage = gst_glsl_stage_new_with_string (context, GL_VERTEX_SHADER,
                  GST_GLSL_VERSION_NONE, GST_GLSL_PROFILE_NONE,
                  filtershader->vertex))) {
        g_set_error (&error, GST_GLSL_ERROR, GST_GLSL_ERROR_COMPILE,
            "Failed to create shader vertex stage");
        goto print_error;
      }
    } else {
      stage = gst_glsl_stage_new_default_vertex (context);
    }

    if (!gst_gl_shader_compile_attach_stage (shader, stage, &error)) {
      gst_object_unref (stage);
      goto print_error;
    }

    if (filtershader->fragment) {
      if (!(stage = gst_glsl_stage_new_with_string (context, GL_FRAGMENT_SHADER,
                  GST_GLSL_VERSION_NONE, GST_GLSL_PROFILE_NONE,
                  filtershader->fragment))) {
        g_set_error (&error, GST_GLSL_ERROR, GST_GLSL_ERROR_COMPILE,
            "Failed to create shader fragment stage");
        goto print_error;
      }
    } else {
      stage = gst_glsl_stage_new_default_fragment (context);
    }

    if (!gst_gl_shader_compile_attach_stage (shader, stage, &error)) {
      gst_object_unref (stage);
      goto print_error;
    }

    if (!gst_gl_shader_link (shader, &error)) {
      goto print_error;
    }
    if (filtershader->shader)
      gst_object_unref (filtershader->shader);
    filtershader->shader = gst_object_ref (shader);
    filtershader->new_source = FALSE;

    GST_OBJECT_UNLOCK (filtershader);
    return shader;
  } else if (filtershader->shader) {
    shader = gst_object_ref (filtershader->shader);
    GST_OBJECT_UNLOCK (filtershader);
    return shader;
  }

  return NULL;

print_error:
  if (shader) {
    gst_object_unref (shader);
    shader = NULL;
  }

  GST_OBJECT_UNLOCK (filtershader);
  GST_ELEMENT_ERROR (filtershader, RESOURCE, NOT_FOUND,
      ("%s", error->message), (NULL));
  return NULL;
}

static void
gst_gl_filtershader_hcallback (gint width, gint height, guint texture,
    gpointer stuff)
{
  GstGLFilter *filter = GST_GL_FILTER (stuff);
  GstGLFilterShader *filtershader = GST_GL_FILTERSHADER (filter);
  GstGLFuncs *gl = GST_GL_BASE_FILTER (filter)->context->gl_vtable;
  GstGLShader *shader;

  if (!(shader = _maybe_recompile_shader (filtershader)))
    return;

  gl->ClearColor (0.0, 0.0, 0.0, 1.0);
  gl->Clear (GL_COLOR_BUFFER_BIT);

  gst_gl_shader_use (shader);

  /* FIXME: propertise these */
  gst_gl_shader_set_uniform_1i (shader, "tex", 0);
  gst_gl_shader_set_uniform_1f (shader, "width", width);
  gst_gl_shader_set_uniform_1f (shader, "height", height);
  gst_gl_shader_set_uniform_1f (shader, "time", filtershader->time);

  /* FIXME: propertise these */
  filter->draw_attr_position_loc =
      gst_gl_shader_get_attribute_location (shader, "a_position");
  filter->draw_attr_texture_loc =
      gst_gl_shader_get_attribute_location (shader, "a_texcoord");

  gl->ActiveTexture (GL_TEXTURE0);
  gl->BindTexture (GL_TEXTURE_2D, texture);

  gst_gl_filter_draw_texture (filter, texture, width, height);

  gst_object_unref (shader);
}
