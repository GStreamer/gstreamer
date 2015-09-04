/*
 * GStreamer
 * Copyright (C) 2015 Matthew Waters <matthew@centricular.com>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gl/gl.h>

#include "gstglslstage.h"
#include "gstglsl_private.h"

static const gchar *es2_version_header = "#version 100\n";

/* *INDENT-OFF* */
static const gchar *simple_vertex_shader_str_gles2 =
      "attribute vec4 a_position;\n"
      "attribute vec2 a_texcoord;\n"
      "varying vec2 v_texcoord;\n"
      "void main()\n"
      "{\n"
      "   gl_Position = a_position;\n"
      "   v_texcoord = a_texcoord;\n"
      "}\n";

static const gchar *simple_fragment_shader_str_gles2 =
      "#ifdef GL_ES\n"
      "precision mediump float;\n"
      "#endif\n"
      "varying vec2 v_texcoord;\n"
      "uniform sampler2D tex;\n"
      "void main()\n"
      "{\n"
      "  gl_FragColor = texture2D(tex, v_texcoord);\n"
      "}";
/* *INDENT-ON* */

GST_DEBUG_CATEGORY_STATIC (gst_glsl_stage_debug);
#define GST_CAT_DEFAULT gst_glsl_stage_debug

G_DEFINE_TYPE_WITH_CODE (GstGLSLStage, gst_glsl_stage, GST_TYPE_OBJECT,
    GST_DEBUG_CATEGORY_INIT (gst_glsl_stage_debug, "glslstage", 0,
        "GLSL Stage");
    );

#define GST_GLSL_STAGE_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GLSL_STAGE, GstGLSLStagePrivate))

struct _GstGLSLStagePrivate
{
  GstGLSLFuncs vtable;

  GLenum type;
  GLhandleARB handle;
  GstGLSLVersion version;
  GstGLSLProfile profile;
  gchar **strings;
  gint n_strings;

  gboolean compiled;
};

static void
gst_glsl_stage_finalize (GObject * object)
{
  GstGLSLStage *stage = GST_GLSL_STAGE (object);
  gint i;

  if (stage->context) {
    gst_object_unref (stage->context);
    stage->context = NULL;
  }

  for (i = 0; i < stage->priv->n_strings; i++) {
    g_free (stage->priv->strings[i]);
  }
  g_free (stage->priv->strings);
  stage->priv->strings = NULL;

  G_OBJECT_CLASS (gst_glsl_stage_parent_class)->finalize (object);
}

static void
gst_glsl_stage_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_glsl_stage_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }

}

static void
gst_glsl_stage_class_init (GstGLSLStageClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstGLSLStagePrivate));

  obj_class->finalize = gst_glsl_stage_finalize;
  obj_class->set_property = gst_glsl_stage_set_property;
  obj_class->get_property = gst_glsl_stage_get_property;
}

static void
gst_glsl_stage_init (GstGLSLStage * stage)
{
  stage->priv = GST_GLSL_STAGE_GET_PRIVATE (stage);
}

static gboolean
_is_valid_shader_type (GLenum type)
{
  switch (type) {
    case GL_VERTEX_SHADER:
    case GL_FRAGMENT_SHADER:
#ifdef GL_TESS_CONTROL_SHADER
    case GL_TESS_CONTROL_SHADER:
#endif
#ifdef GL_TESS_EVALUATION_SHADER
    case GL_TESS_EVALUATION_SHADER:
#endif
#ifdef GL_GEOMETRY_SHADER
    case GL_GEOMETRY_SHADER:
#endif
#ifdef GL_COMPUTE_SHADER
    case GL_COMPUTE_SHADER:
#endif
      return TRUE;
    default:
      return FALSE;
  }
}

static const gchar *
_shader_type_to_string (GLenum type)
{
  switch (type) {
    case GL_VERTEX_SHADER:
      return "vertex";
    case GL_FRAGMENT_SHADER:
      return "fragment";
#ifdef GL_TESS_CONTROL_SHADER
    case GL_TESS_CONTROL_SHADER:
      return "tesselation control";
#endif
#ifdef GL_TESS_EVALUATION_SHADER
    case GL_TESS_EVALUATION_SHADER:
      return "tesselation evaluation";
#endif
#ifdef GL_GEOMETRY_SHADER
    case GL_GEOMETRY_SHADER:
      return "geometry";
#endif
#ifdef GL_COMPUTE_SHADER
    case GL_COMPUTE_SHADER:
      return "compute";
#endif
    default:
      return "unknown";
  }
}

static gboolean
_ensure_shader (GstGLSLStage * stage)
{
  if (stage->priv->handle)
    return TRUE;

  if (!(stage->priv->handle =
          stage->priv->vtable.CreateShader (stage->priv->type)))
    return FALSE;

  return stage->priv->handle != 0;
}

/**
 * gst_glsl_stage_new_with_strings:
 * @context: a #GstGLContext
 * @type: the GL enum shader stage type
 *
 * Returns: (transfer full): a new #GstGLSLStage of the specified @type
 */
GstGLSLStage *
gst_glsl_stage_new_with_strings (GstGLContext * context, guint type,
    GstGLSLVersion version, GstGLSLProfile profile, gint n_strings,
    const gchar ** str)
{
  GstGLSLStage *stage;

  g_return_val_if_fail (GST_GL_IS_CONTEXT (context), NULL);
  g_return_val_if_fail (_is_valid_shader_type (type), NULL);

  stage = g_object_new (GST_TYPE_GLSL_STAGE, NULL);
  /* FIXME: GInittable */
  if (!_gst_glsl_funcs_fill (&stage->priv->vtable, context)) {
    gst_object_unref (stage);
    return NULL;
  }

  stage->context = gst_object_ref (context);
  stage->priv->type = type;
  if (!gst_glsl_stage_set_strings (stage, version, profile, n_strings, str)) {
    gst_object_unref (stage);
    return NULL;
  }

  return stage;
}

/**
 * gst_glsl_stage_new_with_strings:
 * @context: a #GstGLContext
 * @type: the GL enum shader stage type
 *
 * Returns: (transfer full): a new #GstGLSLStage of the specified @type
 */
GstGLSLStage *
gst_glsl_stage_new_with_string (GstGLContext * context, guint type,
    GstGLSLVersion version, GstGLSLProfile profile, const gchar * str)
{
  return gst_glsl_stage_new_with_strings (context, type, version, profile, 1,
      &str);
}

/**
 * gst_glsl_stage_new:
 * @context: a #GstGLContext
 * @type: the GL enum shader stage type
 *
 * Returns: (transfer full): a new #GstGLSLStage of the specified @type
 */
GstGLSLStage *
gst_glsl_stage_new (GstGLContext * context, guint type)
{
  return gst_glsl_stage_new_with_string (context, type, GST_GLSL_VERSION_NONE,
      GST_GLSL_PROFILE_NONE, NULL);
}

/**
 * gst_glsl_stage_new_with_default_vertex:
 * @context: a #GstGLContext
 *
 * Returns: (transfer full): a new #GstGLSLStage with the default vertex shader
 */
GstGLSLStage *
gst_glsl_stage_new_default_vertex (GstGLContext * context)
{
  return gst_glsl_stage_new_with_string (context, GL_VERTEX_SHADER,
      GST_GLSL_VERSION_NONE,
      GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
      simple_vertex_shader_str_gles2);
}

/**
 * gst_glsl_stage_new_with_default_fragment:
 * @context: a #GstGLContext
 *
 * Returns: (transfer full): a new #GstGLSLStage with the default fragment shader
 */
GstGLSLStage *
gst_glsl_stage_new_default_fragment (GstGLContext * context)
{
  return gst_glsl_stage_new_with_string (context, GL_FRAGMENT_SHADER,
      GST_GLSL_VERSION_NONE,
      GST_GLSL_PROFILE_ES | GST_GLSL_PROFILE_COMPATIBILITY,
      simple_fragment_shader_str_gles2);
}

/**
 * gst_glsl_stage_set_strings:
 * @stage: a #GstGLSLStage
 * @version: a #GstGLSLVersion
 * @profile: a #GstGLSLProfile
 * @n_strings: number of strings in @str
 * @str: (transfer none): a GLSL shader string
 *
 * Replaces the current shader string with @str.
 */
gboolean
gst_glsl_stage_set_strings (GstGLSLStage * stage, GstGLSLVersion version,
    GstGLSLProfile profile, gint n_strings, const gchar ** str)
{
  gint i;

  g_return_val_if_fail (GST_IS_GLSL_STAGE (stage), FALSE);
  g_return_val_if_fail (n_strings > 0, FALSE);
  g_return_val_if_fail (str != NULL, FALSE);

  if (!gst_gl_context_supports_glsl_profile_version (stage->context, version,
          profile))
    return FALSE;

  stage->priv->version = version;
  stage->priv->profile = profile;

  for (i = 0; i < stage->priv->n_strings; i++) {
    g_free (stage->priv->strings[i]);
  }

  if (stage->priv->n_strings < n_strings) {
    /* only realloc if we need more space */
    if (stage->priv->strings)
      g_free (stage->priv->strings);
    stage->priv->strings = g_new0 (gchar *, n_strings);
  }

  for (i = 0; i < n_strings; i++)
    stage->priv->strings[i] = g_strdup (str[i]);
  stage->priv->n_strings = n_strings;

  return TRUE;
}

/**
 * gst_glsl_stage_get_shader_type:
 * @stage: a #GstGLSLStage
 *
 * Returns: The GL shader type for this shader stage
 */
guint
gst_glsl_stage_get_shader_type (GstGLSLStage * stage)
{
  g_return_val_if_fail (GST_IS_GLSL_STAGE (stage), 0);

  return stage->priv->type;
}

/**
 * gst_glsl_stage_get_handle:
 * @stage: a #GstGLSLStage
 *
 * Returns: The GL handle for this shader stage
 */
guint
gst_glsl_stage_get_handle (GstGLSLStage * stage)
{
  g_return_val_if_fail (GST_IS_GLSL_STAGE (stage), 0);
  g_return_val_if_fail (stage->priv->compiled, 0);

  return stage->priv->handle;
}

/**
 * gst_glsl_stage_get_version:
 * @stage: a #GstGLSLStage
 *
 * Returns: The GLSL version for the current shader stage
 */
GstGLSLVersion
gst_glsl_stage_get_version (GstGLSLStage * stage)
{
  g_return_val_if_fail (GST_IS_GLSL_STAGE (stage), 0);

  return stage->priv->version;
}

/**
 * gst_glsl_stage_get_profile:
 * @stage: a #GstGLSLStage
 *
 * Returns: The GLSL profile for the current shader stage
 */
GstGLSLProfile
gst_glsl_stage_get_profile (GstGLSLStage * stage)
{
  g_return_val_if_fail (GST_IS_GLSL_STAGE (stage), 0);

  return stage->priv->profile;
}

static void
_maybe_prepend_version (GstGLSLStage * stage, gchar ** shader_str,
    gint * n_vertex_sources, const gchar *** vertex_sources)
{
  gint n = *n_vertex_sources;
  gboolean add_header = FALSE;
  gint i, j;

  /* FIXME: this all an educated guess */
  if (gst_gl_context_check_gl_version (stage->context, GST_GL_API_OPENGL3, 3, 0)
      && (stage->priv->profile & GST_GLSL_PROFILE_ES) != 0
      && !_gst_glsl_shader_string_find_version (shader_str[0])) {
    add_header = TRUE;
    n++;
  }

  *vertex_sources = g_malloc0 (n * sizeof (gchar *));

  i = 0;
  if (add_header)
    *vertex_sources[i++] = es2_version_header;

  for (j = 0; j < stage->priv->n_strings; i++, j++)
    (*vertex_sources)[i] = shader_str[j];
  *n_vertex_sources = n;
}

struct compile
{
  GstGLSLStage *stage;
  GError **error;
  gboolean result;
};

static void
_compile_shader (GstGLContext * context, struct compile *data)
{
  GstGLSLStagePrivate *priv = data->stage->priv;
  GstGLSLFuncs *vtable = &data->stage->priv->vtable;
  const GstGLFuncs *gl = context->gl_vtable;
  const gchar **vertex_sources;
  gchar info_buffer[2048];
  gint n_vertex_sources;
  GLint status;
  gint len;
  gint i;

  if (data->stage->priv->compiled) {
    data->result = TRUE;
    return;
  }

  if (!_ensure_shader (data->stage)) {
    g_set_error (data->error, GST_GLSL_ERROR, GST_GLSL_ERROR_COMPILE,
        "Failed to create shader object");
    return;
  }

  n_vertex_sources = data->stage->priv->n_strings;
  _maybe_prepend_version (data->stage, priv->strings, &n_vertex_sources,
      &vertex_sources);

  GST_TRACE_OBJECT (data->stage, "compiling shader:");
  for (i = 0; i < n_vertex_sources; i++) {
    GST_TRACE_OBJECT (data->stage, "%s", vertex_sources[i]);
  }

  gl->ShaderSource (priv->handle, n_vertex_sources,
      (const gchar **) vertex_sources, NULL);
  gl->CompileShader (priv->handle);
  /* FIXME: supported threaded GLSL compilers and don't destroy compilation
   * performance by getting the compilation result directly after compilation */
  gl->GetShaderiv (priv->handle, GL_COMPILE_STATUS, &status);

  vtable->GetShaderInfoLog (priv->handle, sizeof (info_buffer) - 1, &len,
      info_buffer);
  info_buffer[len] = '\0';

  if (status != GL_TRUE) {
    GST_ERROR_OBJECT (data->stage, "%s shader compilation failed:%s",
        _shader_type_to_string (priv->type), info_buffer);

    g_set_error (data->error, GST_GLSL_ERROR, GST_GLSL_ERROR_COMPILE,
        "%s shader compilation failed:%s",
        _shader_type_to_string (priv->type), info_buffer);

    vtable->DeleteShader (priv->handle);
    data->result = FALSE;
    return;
  } else if (len > 1) {
    GST_FIXME_OBJECT (data->stage, "%s shader info log:%s",
        _shader_type_to_string (priv->type), info_buffer);
  }

  data->result = TRUE;
}

/**
 * gst_glsl_stage_compile:
 * @stage: a #GstGLSLStage
 *
 * Returns: whether the compilation suceeded
 */
gboolean
gst_glsl_stage_compile (GstGLSLStage * stage, GError ** error)
{
  struct compile data;

  g_return_val_if_fail (GST_IS_GLSL_STAGE (stage), FALSE);

  if (!stage->priv->strings) {
    g_set_error (error, GST_GLSL_ERROR, GST_GLSL_ERROR_COMPILE,
        "No shader source to compile");
    return FALSE;
  }

  data.stage = stage;
  data.error = error;

  gst_gl_context_thread_add (stage->context,
      (GstGLContextThreadFunc) _compile_shader, &data);

  stage->priv->compiled = TRUE;

  return data.result;
}
