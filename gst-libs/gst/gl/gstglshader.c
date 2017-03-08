/*
 * GStreamer
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
 * Copyright (C) 2014 Julien Isorce <julien.isorce@collabora.co.uk>
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

#include "gl.h"
#include "gstglshader.h"
#include "gstglsl_private.h"

/**
 * SECTION:gstglshader
 * @title: GstGLShader
 * @short_description: object representing an OpenGL shader program
 * @see_also: #GstGLSLStage
 */

#ifndef GLhandleARB
#define GLhandleARB GLuint
#endif

#define GST_GL_SHADER_GET_PRIVATE(o)					\
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_GL_SHADER, GstGLShaderPrivate))

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

typedef struct _GstGLShaderVTable
{
  GLuint (GSTGLAPI *CreateProgram) (void);
  void (GSTGLAPI *DeleteProgram) (GLuint program);
  void (GSTGLAPI *UseProgram) (GLuint program);
  void (GSTGLAPI *GetAttachedShaders) (GLuint program, GLsizei maxcount,
      GLsizei * count, GLuint * shaders);

  GLuint (GSTGLAPI *CreateShader) (GLenum shaderType);
  void (GSTGLAPI *DeleteShader) (GLuint shader);
  void (GSTGLAPI *AttachShader) (GLuint program, GLuint shader);
  void (GSTGLAPI *DetachShader) (GLuint program, GLuint shader);

  void (GSTGLAPI *GetShaderiv) (GLuint program, GLenum pname, GLint * params);
  void (GSTGLAPI *GetProgramiv) (GLuint program, GLenum pname, GLint * params);
  void (GSTGLAPI *GetShaderInfoLog) (GLuint shader, GLsizei maxLength,
      GLsizei * length, char *log);
  void (GSTGLAPI *GetProgramInfoLog) (GLuint shader, GLsizei maxLength,
      GLsizei * length, char *log);
} GstGLShaderVTable;

enum
{
  PROP_0,
  PROP_LINKED,
};

struct _GstGLShaderPrivate
{
  GLhandleARB program_handle;
  GList *stages;

  gboolean linked;
  GHashTable *uniform_locations;

  GstGLSLFuncs vtable;
};

GST_DEBUG_CATEGORY_STATIC (gst_gl_shader_debug);
#define GST_CAT_DEFAULT gst_gl_shader_debug

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_gl_shader_debug, "glshader", 0, "shader");
G_DEFINE_TYPE_WITH_CODE (GstGLShader, gst_gl_shader, GST_TYPE_OBJECT,
    DEBUG_INIT);

static void
_cleanup_shader (GstGLContext * context, GstGLShader * shader)
{
  GstGLShaderPrivate *priv = shader->priv;

  GST_OBJECT_LOCK (shader);

  /* release shader objects */
  gst_gl_shader_release_unlocked (shader);

  /* delete program */
  if (priv->program_handle) {
    GST_TRACE ("finalizing program shader %u", priv->program_handle);

    priv->vtable.DeleteProgram (priv->program_handle);
  }

  GST_DEBUG ("shader deleted %u", priv->program_handle);

  GST_OBJECT_UNLOCK (shader);
}

static void
gst_gl_shader_finalize (GObject * object)
{
  GstGLShader *shader;
  GstGLShaderPrivate *priv;

  shader = GST_GL_SHADER (object);
  priv = shader->priv;

  GST_TRACE_OBJECT (shader, "finalizing shader %u", priv->program_handle);

  gst_gl_context_thread_add (shader->context,
      (GstGLContextThreadFunc) _cleanup_shader, shader);

  priv->program_handle = 0;
  g_hash_table_destroy (priv->uniform_locations);

  if (shader->context) {
    gst_object_unref (shader->context);
    shader->context = NULL;
  }

  G_OBJECT_CLASS (gst_gl_shader_parent_class)->finalize (object);
}

static void
gst_gl_shader_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_shader_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstGLShader *shader = GST_GL_SHADER (object);
  GstGLShaderPrivate *priv = shader->priv;

  switch (prop_id) {
    case PROP_LINKED:
      g_value_set_boolean (value, priv->linked);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_gl_shader_class_init (GstGLShaderClass * klass)
{
  /* bind class methods .. */
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstGLShaderPrivate));

  obj_class->finalize = gst_gl_shader_finalize;
  obj_class->set_property = gst_gl_shader_set_property;
  obj_class->get_property = gst_gl_shader_get_property;

  /* .. and install properties */
  g_object_class_install_property (obj_class,
      PROP_LINKED,
      g_param_spec_boolean ("linked",
          "Linked",
          "Shader link status", FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

static void
gst_gl_shader_init (GstGLShader * self)
{
  /* initialize sources and create program object */
  GstGLShaderPrivate *priv;

  priv = self->priv = GST_GL_SHADER_GET_PRIVATE (self);

  priv->linked = FALSE;
  priv->uniform_locations =
      g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);
}

static int
_get_uniform_location (GstGLShader * shader, const gchar * name)
{
  GstGLShaderPrivate *priv = shader->priv;
  int location;
  gpointer value;

  g_return_val_if_fail (priv->linked, 0);

  if (!g_hash_table_lookup_extended (priv->uniform_locations, name, NULL,
          &value)) {
    const GstGLFuncs *gl = shader->context->gl_vtable;
    location = gl->GetUniformLocation (priv->program_handle, name);
    g_hash_table_insert (priv->uniform_locations, g_strdup (name),
        GINT_TO_POINTER (location));
  } else {
    location = GPOINTER_TO_INT (value);
  }

  return location;
}

static GstGLShader *
_new_with_stages_va_list (GstGLContext * context, GError ** error,
    va_list varargs)
{
  GstGLShader *shader;
  GstGLSLStage *stage;
  gboolean to_unref_and_out = FALSE;

  g_return_val_if_fail (GST_IS_GL_CONTEXT (context), NULL);

  shader = g_object_new (GST_TYPE_GL_SHADER, NULL);
  shader->context = gst_object_ref (context);

  while ((stage = va_arg (varargs, GstGLSLStage *))) {
    if (to_unref_and_out) {
      gst_object_unref (stage);
      continue;
    }

    if (!gst_glsl_stage_compile (stage, error)) {
      gst_object_unref (stage);
      to_unref_and_out = TRUE;
      continue;
    }
    if (!gst_gl_shader_attach (shader, stage)) {
      g_set_error (error, GST_GLSL_ERROR, GST_GLSL_ERROR_PROGRAM,
          "Failed to attach stage to program");
      to_unref_and_out = TRUE;
      continue;
    }
  }

  if (to_unref_and_out) {
    gst_object_unref (shader);
    return NULL;
  }

  return shader;
}

/**
 * gst_gl_shader_new_link_with_stages:
 * @context: a #GstGLContext
 * @error: a #GError
 * @...: a NULL terminated list of #GstGLSLStage's
 *
 * Each stage will attempt to be compiled and attached to @shader.  Then
 * the shader will be linked. On error, %NULL will be returned and @error will
 * contain the details of the error.
 *
 * Note: must be called in the GL thread
 *
 * Returns: (transfer full): a new @shader with the specified stages.
 *
 * Since: 1.8
 */
GstGLShader *
gst_gl_shader_new_link_with_stages (GstGLContext * context, GError ** error,
    ...)
{
  GstGLShader *shader;
  va_list varargs;

  va_start (varargs, error);
  shader = _new_with_stages_va_list (context, error, varargs);
  va_end (varargs);

  if (!shader)
    return NULL;

  if (!gst_gl_shader_link (shader, error))
    return NULL;

  return shader;
}

/**
 * gst_gl_shader_new_with_stages:
 * @context: a #GstGLContext
 * @error: a #GError
 * @...: a NULL terminated list of #GstGLSLStage's
 *
 * Each stage will attempt to be compiled and attached to @shader.  On error,
 * %NULL will be returned and @error will contain the details of the error.
 *
 * Note: must be called in the GL thread
 *
 * Returns: (transfer full): a new @shader with the specified stages.
 *
 * Since: 1.8
 */
GstGLShader *
gst_gl_shader_new_with_stages (GstGLContext * context, GError ** error, ...)
{
  GstGLShader *shader;
  va_list varargs;

  va_start (varargs, error);
  shader = _new_with_stages_va_list (context, error, varargs);
  va_end (varargs);

  return shader;
}

/**
 * gst_gl_shader_new:
 * @context: a #GstGLContext
 *
 * Note: must be called in the GL thread
 *
 * Returns: (transfer full): a new empty @shader
 */
GstGLShader *
gst_gl_shader_new (GstGLContext * context)
{
  return gst_gl_shader_new_with_stages (context, NULL, NULL);
}

/**
 * gst_gl_shader_new_default:
 * @context: a #GstGLContext
 * @error: a #GError that is filled on failure
 *
 * Note: must be called in the GL thread
 *
 * Returns: (transfer full): a default @shader or %NULL on failure
 *
 * Since: 1.8
 */
GstGLShader *
gst_gl_shader_new_default (GstGLContext * context, GError ** error)
{
  return gst_gl_shader_new_link_with_stages (context, error,
      gst_glsl_stage_new_default_vertex (context),
      gst_glsl_stage_new_default_fragment (context), NULL);
}

/**
 * gst_gl_shader_is_linked:
 * @shader: a #GstGLShader
 *
 * Note: must be called in the GL thread
 *
 * Returns: whether @shader has been successfully linked
 *
 * Since: 1.8
 */
gboolean
gst_gl_shader_is_linked (GstGLShader * shader)
{
  gboolean ret;

  g_return_val_if_fail (GST_IS_GL_SHADER (shader), FALSE);

  GST_OBJECT_LOCK (shader);
  ret = shader->priv->linked;
  GST_OBJECT_UNLOCK (shader);

  return ret;
}

static gboolean
_ensure_program (GstGLShader * shader)
{
  if (shader->priv->program_handle)
    return TRUE;

  shader->priv->program_handle = shader->priv->vtable.CreateProgram ();
  return shader->priv->program_handle != 0;
}

/**
 * gst_gl_shader_get_program_handle:
 * @shader: a #GstGLShader
 *
 * Returns: the GL program handle for this shader
 *
 * Since: 1.8
 */
int
gst_gl_shader_get_program_handle (GstGLShader * shader)
{
  int ret;

  g_return_val_if_fail (GST_IS_GL_SHADER (shader), 0);

  GST_OBJECT_LOCK (shader);
  ret = (int) shader->priv->program_handle;
  GST_OBJECT_UNLOCK (shader);

  return ret;
}

/**
 * gst_gl_shader_detach_unlocked:
 * @shader: a #GstGLShader
 * @stage: a #GstGLSLStage to attach
 *
 * Detaches @stage from @shader.  @stage must have been successfully attached
 * to @shader with gst_gl_shader_attach() or gst_gl_shader_attach_unlocked().
 *
 * Note: must be called in the GL thread
 *
 * Since: 1.8
 */
void
gst_gl_shader_detach_unlocked (GstGLShader * shader, GstGLSLStage * stage)
{
  guint stage_handle;
  GList *elem;

  g_return_if_fail (GST_IS_GL_SHADER (shader));
  g_return_if_fail (GST_IS_GLSL_STAGE (stage));

  if (!_gst_glsl_funcs_fill (&shader->priv->vtable, shader->context)) {
    GST_WARNING_OBJECT (shader, "Failed to retreive required GLSL functions");
    return;
  }

  if (!shader->priv->program_handle)
    return;

  elem = g_list_find (shader->priv->stages, stage);
  if (!elem) {
    GST_FIXME_OBJECT (shader, "Could not find stage %p in shader %p", stage,
        shader);
    return;
  }

  stage_handle = gst_glsl_stage_get_handle (stage);
  if (!stage_handle) {
    GST_FIXME_OBJECT (shader, "Stage %p doesn't have a GL handle", stage);
    return;
  }

  if (shader->context->gl_vtable->IsProgram)
    g_assert (shader->context->gl_vtable->IsProgram (shader->
            priv->program_handle));
  if (shader->context->gl_vtable->IsShader)
    g_assert (shader->context->gl_vtable->IsShader (stage_handle));

  GST_LOG_OBJECT (shader, "detaching shader %i from program %i", stage_handle,
      (int) shader->priv->program_handle);
  shader->priv->vtable.DetachShader (shader->priv->program_handle,
      stage_handle);

  shader->priv->stages = g_list_delete_link (shader->priv->stages, elem);
  gst_object_unref (stage);
}

/**
 * gst_gl_shader_detach:
 * @shader: a #GstGLShader
 * @stage: a #GstGLSLStage to attach
 *
 * Detaches @stage from @shader.  @stage must have been successfully attached
 * to @shader with gst_gl_shader_attach() or gst_gl_shader_attach_unlocked().
 *
 * Note: must be called in the GL thread
 *
 * Since: 1.8
 */
void
gst_gl_shader_detach (GstGLShader * shader, GstGLSLStage * stage)
{
  g_return_if_fail (GST_IS_GL_SHADER (shader));
  g_return_if_fail (GST_IS_GLSL_STAGE (stage));

  GST_OBJECT_LOCK (shader);
  gst_gl_shader_detach_unlocked (shader, stage);
  GST_OBJECT_UNLOCK (shader);
}

/**
 * gst_gl_shader_attach_unlocked:
 * @shader: a #GstGLShader
 * @stage: a #GstGLSLStage to attach
 *
 * Attaches @stage to @shader.  @stage must have been successfully compiled
 * with gst_glsl_stage_compile().
 *
 * Note: must be called in the GL thread
 *
 * Returns: whether @stage could be attached to @shader
 *
 * Since: 1.8
 */
gboolean
gst_gl_shader_attach_unlocked (GstGLShader * shader, GstGLSLStage * stage)
{
  guint stage_handle;

  g_return_val_if_fail (GST_IS_GL_SHADER (shader), FALSE);
  g_return_val_if_fail (GST_IS_GLSL_STAGE (stage), FALSE);

  if (!_gst_glsl_funcs_fill (&shader->priv->vtable, shader->context)) {
    GST_WARNING_OBJECT (shader, "Failed to retreive required GLSL functions");
    return FALSE;
  }

  if (!_ensure_program (shader))
    return FALSE;

  /* already attached? */
  if (g_list_find (shader->priv->stages, stage))
    return TRUE;

  stage_handle = gst_glsl_stage_get_handle (stage);
  if (!stage_handle) {
    return FALSE;
  }

  if (shader->context->gl_vtable->IsProgram)
    g_assert (shader->context->gl_vtable->IsProgram (shader->
            priv->program_handle));
  if (shader->context->gl_vtable->IsShader)
    g_assert (shader->context->gl_vtable->IsShader (stage_handle));

  shader->priv->stages =
      g_list_prepend (shader->priv->stages, gst_object_ref_sink (stage));
  GST_LOG_OBJECT (shader, "attaching shader %i to program %i", stage_handle,
      (int) shader->priv->program_handle);
  shader->priv->vtable.AttachShader (shader->priv->program_handle,
      stage_handle);

  return TRUE;
}

/**
 * gst_gl_shader_attach:
 * @shader: a #GstGLShader
 * @stage: a #GstGLSLStage to attach
 *
 * Attaches @stage to @shader.  @stage must have been successfully compiled
 * with gst_glsl_stage_compile().
 *
 * Note: must be called in the GL thread
 *
 * Returns: whether @stage could be attached to @shader
 *
 * Since: 1.8
 */
gboolean
gst_gl_shader_attach (GstGLShader * shader, GstGLSLStage * stage)
{
  gboolean ret;

  g_return_val_if_fail (GST_IS_GL_SHADER (shader), FALSE);
  g_return_val_if_fail (GST_IS_GLSL_STAGE (stage), FALSE);

  GST_OBJECT_LOCK (shader);
  ret = gst_gl_shader_attach_unlocked (shader, stage);
  GST_OBJECT_UNLOCK (shader);

  return ret;
}

/**
 * gst_gl_shader_compile_attach_stage:
 * @shader: a #GstGLShader
 * @stage: a #GstGLSLStage to attach
 * @error: a #GError
 *
 * Compiles @stage and attaches it to @shader.
 *
 * Note: must be called in the GL thread
 *
 * Returns: whether @stage could be compiled and attached to @shader
 *
 * Since: 1.8
 */
gboolean
gst_gl_shader_compile_attach_stage (GstGLShader * shader, GstGLSLStage * stage,
    GError ** error)
{
  g_return_val_if_fail (GST_IS_GLSL_STAGE (stage), FALSE);

  if (!gst_glsl_stage_compile (stage, error)) {
    return FALSE;
  }

  if (!gst_gl_shader_attach (shader, stage)) {
    g_set_error (error, GST_GLSL_ERROR, GST_GLSL_ERROR_COMPILE,
        "Failed to attach stage to shader");
    return FALSE;
  }

  return TRUE;
}

/**
 * gst_gl_shader_link:
 * @shader: a #GstGLShader
 * @error: a #GError
 *
 * Links the current list of #GstGLSLStage's in @shader.
 *
 * Note: must be called in the GL thread
 *
 * Returns: whether @shader could be linked together.
 *
 * Since: 1.8
 */
gboolean
gst_gl_shader_link (GstGLShader * shader, GError ** error)
{
  GstGLShaderPrivate *priv;
  const GstGLFuncs *gl;
  gchar info_buffer[2048];
  GLint status = GL_FALSE;
  gint len = 0;
  gboolean ret;
  GList *elem;

  g_return_val_if_fail (GST_IS_GL_SHADER (shader), FALSE);

  GST_OBJECT_LOCK (shader);

  priv = shader->priv;
  gl = shader->context->gl_vtable;

  if (priv->linked) {
    GST_OBJECT_UNLOCK (shader);
    return TRUE;
  }

  if (!_gst_glsl_funcs_fill (&shader->priv->vtable, shader->context)) {
    g_set_error (error, GST_GLSL_ERROR, GST_GLSL_ERROR_PROGRAM,
        "Failed to retreive required GLSL functions");
    GST_OBJECT_UNLOCK (shader);
    return FALSE;
  }

  if (!_ensure_program (shader)) {
    g_set_error (error, GST_GLSL_ERROR, GST_GLSL_ERROR_PROGRAM,
        "Failed to create GL program object");
    GST_OBJECT_UNLOCK (shader);
    return FALSE;
  }

  GST_TRACE ("shader created %u", shader->priv->program_handle);

  for (elem = shader->priv->stages; elem; elem = elem->next) {
    GstGLSLStage *stage = elem->data;

    if (!gst_glsl_stage_compile (stage, error)) {
      GST_OBJECT_UNLOCK (shader);
      return FALSE;
    }

    if (!gst_gl_shader_attach_unlocked (shader, stage)) {
      g_set_error (error, GST_GLSL_ERROR, GST_GLSL_ERROR_COMPILE,
          "Failed to attach shader %" GST_PTR_FORMAT "to program %"
          GST_PTR_FORMAT, stage, shader);
      GST_OBJECT_UNLOCK (shader);
      return FALSE;
    }
  }

  /* if nothing failed link shaders */
  gl->LinkProgram (priv->program_handle);
  status = GL_FALSE;
  priv->vtable.GetProgramiv (priv->program_handle, GL_LINK_STATUS, &status);

  priv->vtable.GetProgramInfoLog (priv->program_handle,
      sizeof (info_buffer) - 1, &len, info_buffer);
  info_buffer[len] = '\0';

  if (status != GL_TRUE) {
    GST_ERROR ("Shader linking failed:\n%s", info_buffer);

    g_set_error (error, GST_GLSL_ERROR, GST_GLSL_ERROR_LINK,
        "Shader Linking failed:\n%s", info_buffer);
    ret = priv->linked = FALSE;
    GST_OBJECT_UNLOCK (shader);
    return ret;
  } else if (len > 1) {
    GST_FIXME ("shader link log:\n%s\n", info_buffer);
  }

  ret = priv->linked = TRUE;
  GST_OBJECT_UNLOCK (shader);

  g_object_notify (G_OBJECT (shader), "linked");

  return ret;
}

/**
 * gst_gl_shader_release_unlocked:
 * @shader: a #GstGLShader
 *
 * Releases the shader and stages.
 *
 * Note: must be called in the GL thread
 *
 * Since: 1.8
 */
void
gst_gl_shader_release_unlocked (GstGLShader * shader)
{
  GstGLShaderPrivate *priv;
  GList *elem;

  g_return_if_fail (GST_IS_GL_SHADER (shader));

  priv = shader->priv;

  for (elem = shader->priv->stages; elem;) {
    GstGLSLStage *stage = elem->data;
    GList *next = elem->next;

    gst_gl_shader_detach_unlocked (shader, stage);
    elem = next;
  }

  g_list_free_full (shader->priv->stages, (GDestroyNotify) gst_object_unref);
  shader->priv->stages = NULL;

  priv->linked = FALSE;
  g_hash_table_remove_all (priv->uniform_locations);

  g_object_notify (G_OBJECT (shader), "linked");
}

/**
 * gst_gl_shader_release:
 * @shader: a #GstGLShader
 *
 * Releases the shader and stages.
 *
 * Note: must be called in the GL thread
 *
 * Since: 1.8
 */
void
gst_gl_shader_release (GstGLShader * shader)
{
  g_return_if_fail (GST_IS_GL_SHADER (shader));

  GST_OBJECT_LOCK (shader);
  gst_gl_shader_release_unlocked (shader);
  GST_OBJECT_UNLOCK (shader);
}

/**
 * gst_gl_shader_use:
 * @shader: a #GstGLShader
 *
 * Mark's @shader as being used for the next GL draw command.
 *
 * Note: must be called in the GL thread and @shader must have been linked.
 */
void
gst_gl_shader_use (GstGLShader * shader)
{
  GstGLShaderPrivate *priv;

  g_return_if_fail (GST_IS_GL_SHADER (shader));

  priv = shader->priv;

  g_return_if_fail (priv->program_handle);

  priv->vtable.UseProgram (priv->program_handle);

  return;
}

/**
 * gst_gl_context_clear_shader:
 * @context: a #GstGLContext
 *
 * Clear's the currently set shader from the GL state machine.
 *
 * Note: must be called in the GL thread.
 */
void
gst_gl_context_clear_shader (GstGLContext * context)
{
  GstGLFuncs *gl;

  g_return_if_fail (GST_IS_GL_CONTEXT (context));

  gl = context->gl_vtable;

  if (gl->CreateProgram)
    gl->UseProgram (0);
  else if (gl->CreateProgramObject)
    gl->UseProgramObject (0);
}

void
gst_gl_shader_set_uniform_1f (GstGLShader * shader, const gchar * name,
    gfloat value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);

  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform1f (location, value);
}

void
gst_gl_shader_set_uniform_1fv (GstGLShader * shader, const gchar * name,
    guint count, gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform1fv (location, count, value);
}

void
gst_gl_shader_set_uniform_1i (GstGLShader * shader, const gchar * name,
    gint value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform1i (location, value);
}

void
gst_gl_shader_set_uniform_1iv (GstGLShader * shader, const gchar * name,
    guint count, gint * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform1iv (location, count, value);
}

void
gst_gl_shader_set_uniform_2f (GstGLShader * shader, const gchar * name,
    gfloat value0, gfloat value1)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform2f (location, value0, value1);
}

void
gst_gl_shader_set_uniform_2fv (GstGLShader * shader, const gchar * name,
    guint count, gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform2fv (location, count, value);
}

void
gst_gl_shader_set_uniform_2i (GstGLShader * shader, const gchar * name,
    gint v0, gint v1)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform2i (location, v0, v1);
}

void
gst_gl_shader_set_uniform_2iv (GstGLShader * shader, const gchar * name,
    guint count, gint * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform2iv (location, count, value);
}

void
gst_gl_shader_set_uniform_3f (GstGLShader * shader, const gchar * name,
    gfloat v0, gfloat v1, gfloat v2)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform3f (location, v0, v1, v2);
}

void
gst_gl_shader_set_uniform_3fv (GstGLShader * shader, const gchar * name,
    guint count, gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform3fv (location, count, value);
}

void
gst_gl_shader_set_uniform_3i (GstGLShader * shader, const gchar * name,
    gint v0, gint v1, gint v2)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform3i (location, v0, v1, v2);
}

void
gst_gl_shader_set_uniform_3iv (GstGLShader * shader, const gchar * name,
    guint count, gint * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform3iv (location, count, value);
}

void
gst_gl_shader_set_uniform_4f (GstGLShader * shader, const gchar * name,
    gfloat v0, gfloat v1, gfloat v2, gfloat v3)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform4f (location, v0, v1, v2, v3);
}

void
gst_gl_shader_set_uniform_4fv (GstGLShader * shader, const gchar * name,
    guint count, gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform4fv (location, count, value);
}

void
gst_gl_shader_set_uniform_4i (GstGLShader * shader, const gchar * name,
    gint v0, gint v1, gint v2, gint v3)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform4i (location, v0, v1, v2, v3);
}

void
gst_gl_shader_set_uniform_4iv (GstGLShader * shader, const gchar * name,
    guint count, gint * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->Uniform4iv (location, count, value);
}

void
gst_gl_shader_set_uniform_matrix_2fv (GstGLShader * shader, const gchar * name,
    gint count, gboolean transpose, const gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->UniformMatrix2fv (location, count, transpose, value);
}

void
gst_gl_shader_set_uniform_matrix_3fv (GstGLShader * shader, const gchar * name,
    gint count, gboolean transpose, const gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->UniformMatrix3fv (location, count, transpose, value);
}

void
gst_gl_shader_set_uniform_matrix_4fv (GstGLShader * shader, const gchar * name,
    gint count, gboolean transpose, const gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->UniformMatrix4fv (location, count, transpose, value);
}

#if GST_GL_HAVE_OPENGL
void
gst_gl_shader_set_uniform_matrix_2x3fv (GstGLShader * shader,
    const gchar * name, gint count, gboolean transpose, const gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->UniformMatrix2x3fv (location, count, transpose, value);
}

void
gst_gl_shader_set_uniform_matrix_2x4fv (GstGLShader * shader,
    const gchar * name, gint count, gboolean transpose, const gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->UniformMatrix2x4fv (location, count, transpose, value);
}

void
gst_gl_shader_set_uniform_matrix_3x2fv (GstGLShader * shader,
    const gchar * name, gint count, gboolean transpose, const gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->UniformMatrix3x2fv (location, count, transpose, value);
}

void
gst_gl_shader_set_uniform_matrix_3x4fv (GstGLShader * shader,
    const gchar * name, gint count, gboolean transpose, const gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->UniformMatrix3x4fv (location, count, transpose, value);
}

void
gst_gl_shader_set_uniform_matrix_4x2fv (GstGLShader * shader,
    const gchar * name, gint count, gboolean transpose, const gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->UniformMatrix4x2fv (location, count, transpose, value);
}

void
gst_gl_shader_set_uniform_matrix_4x3fv (GstGLShader * shader,
    const gchar * name, gint count, gboolean transpose, const gfloat * value)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  GLint location = -1;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  location = _get_uniform_location (shader, name);

  gl->UniformMatrix4x3fv (location, count, transpose, value);
}
#endif /* GST_GL_HAVE_OPENGL */

GLint
gst_gl_shader_get_attribute_location (GstGLShader * shader, const gchar * name)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;
  gint ret;

  g_return_val_if_fail (shader != NULL, -1);
  priv = shader->priv;
  g_return_val_if_fail (priv->program_handle != 0, -1);

  gl = shader->context->gl_vtable;

  ret = gl->GetAttribLocation (priv->program_handle, name);

  GST_TRACE_OBJECT (shader, "retreived program %i attribute \'%s\' location %i",
      (int) priv->program_handle, name, ret);

  return ret;
}

void
gst_gl_shader_bind_attribute_location (GstGLShader * shader, GLuint index,
    const gchar * name)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;

  g_return_if_fail (shader != NULL);
  priv = shader->priv;
  g_return_if_fail (priv->program_handle != 0);
  gl = shader->context->gl_vtable;

  GST_TRACE_OBJECT (shader, "binding program %i attribute \'%s\' location %i",
      (int) priv->program_handle, name, index);

  gl->BindAttribLocation (priv->program_handle, index, name);
}

void
gst_gl_shader_bind_frag_data_location (GstGLShader * shader,
    guint index, const gchar * name)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;

  g_return_if_fail (shader != NULL);
  if (!_ensure_program (shader))
    g_return_if_fail (shader->priv->program_handle);
  priv = shader->priv;
  gl = shader->context->gl_vtable;
  g_return_if_fail (gl->BindFragDataLocation);

  GST_TRACE_OBJECT (shader, "binding program %i frag data \'%s\' location %i",
      (int) priv->program_handle, name, index);

  gl->BindFragDataLocation (priv->program_handle, index, name);
}
