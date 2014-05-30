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

#if GST_GL_HAVE_GLES2
/* *INDENT-OFF* */
static const gchar *simple_vertex_shader_str_gles2 =
      "attribute vec4 a_position;   \n"
      "attribute vec2 a_texCoord;   \n"
      "varying vec2 v_texCoord;     \n"
      "void main()                  \n"
      "{                            \n"
      "   gl_Position = a_position; \n"
      "   v_texCoord = a_texCoord;  \n"
      "}                            \n";

static const gchar *simple_fragment_shader_str_gles2 =
      "precision mediump float;                            \n"
      "varying vec2 v_texCoord;                            \n"
      "uniform sampler2D tex;                              \n"
      "void main()                                         \n"
      "{                                                   \n"
      "  gl_FragColor = texture2D( tex, v_texCoord );      \n"
      "}                                                   \n";
/* *INDENT-ON* */
#endif

#ifndef GL_COMPILE_STATUS
#define GL_COMPILE_STATUS             0x8B81
#endif
#ifndef GLhandleARB
#define GLhandleARB GLuint
#endif

#define GST_GL_SHADER_GET_PRIVATE(o)					\
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_SHADER, GstGLShaderPrivate))

#define USING_OPENGL(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0))
#define USING_OPENGL3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 3, 1))
#define USING_GLES(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES, 1, 0))
#define USING_GLES2(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0))
#define USING_GLES3(context) (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 0))

typedef struct _GstGLShaderVTable
{
  GLuint GSTGLAPI (*CreateProgram) (void);
  void GSTGLAPI (*DeleteProgram) (GLuint program);
  void GSTGLAPI (*UseProgram) (GLuint program);
  void GSTGLAPI (*GetAttachedShaders) (GLuint program, GLsizei maxcount,
      GLsizei * count, GLuint * shaders);

  GLuint GSTGLAPI (*CreateShader) (GLenum shaderType);
  void GSTGLAPI (*DeleteShader) (GLuint shader);
  void GSTGLAPI (*AttachShader) (GLuint program, GLuint shader);
  void GSTGLAPI (*DetachShader) (GLuint program, GLuint shader);

  void GSTGLAPI (*GetShaderiv) (GLuint program, GLenum pname, GLint * params);
  void GSTGLAPI (*GetProgramiv) (GLuint program, GLenum pname, GLint * params);
  void GSTGLAPI (*GetShaderInfoLog) (GLuint shader, GLsizei maxLength,
      GLsizei * length, char *log);
  void GSTGLAPI (*GetProgramInfoLog) (GLuint shader, GLsizei maxLength,
      GLsizei * length, char *log);
} GstGLShaderVTable;

enum
{
  PROP_0,
  PROP_VERTEX_SRC,
  PROP_FRAGMENT_SRC,
  PROP_COMPILED,
  PROP_ACTIVE                   /* unused */
};

struct _GstGLShaderPrivate
{
  gchar *vertex_src;
  gchar *fragment_src;

  GLhandleARB vertex_handle;
  GLhandleARB fragment_handle;
  GLhandleARB program_handle;

  gboolean compiled;
  gboolean active;

  GstGLShaderVTable vtable;
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

  /* release shader objects */
  gst_gl_shader_release (shader);

  /* delete program */
  if (priv->program_handle) {
    GST_TRACE ("finalizing program shader %u", priv->program_handle);

    priv->vtable.DeleteProgram (priv->program_handle);
    /* err = glGetError (); */
    /* GST_WARNING ("error: 0x%x", err);  */
    /* glGetObjectParameteriv(priv->program_handle, GL_OBJECT_DELETE_STATUS_, &status); */
    /* GST_INFO ("program deletion status:%s", status == GL_TRUE ? "true" : "false" ); */
  }

  GST_DEBUG ("shader deleted %u", priv->program_handle);
}

static void
gst_gl_shader_finalize (GObject * object)
{
  GstGLShader *shader;
  GstGLShaderPrivate *priv;

  shader = GST_GL_SHADER (object);
  priv = shader->priv;

  GST_TRACE ("finalizing shader %u", priv->program_handle);

  g_free (priv->vertex_src);
  g_free (priv->fragment_src);

  gst_gl_context_thread_add (shader->context,
      (GstGLContextThreadFunc) _cleanup_shader, shader);

  priv->fragment_handle = 0;
  priv->vertex_handle = 0;
  priv->program_handle = 0;

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
  GstGLShader *shader = GST_GL_SHADER (object);

  switch (prop_id) {
    case PROP_VERTEX_SRC:
      gst_gl_shader_set_vertex_source (shader, g_value_get_string (value));
      break;
    case PROP_FRAGMENT_SRC:
      gst_gl_shader_set_fragment_source (shader, g_value_get_string (value));
      break;
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
    case PROP_VERTEX_SRC:
      g_value_set_string (value, priv->vertex_src);
      break;
    case PROP_FRAGMENT_SRC:
      g_value_set_string (value, priv->fragment_src);
      break;
    case PROP_COMPILED:
      g_value_set_boolean (value, priv->compiled);
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
      PROP_VERTEX_SRC,
      g_param_spec_string ("vertex-src",
          "Vertex Source",
          "GLSL Vertex Shader source code", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (obj_class,
      PROP_FRAGMENT_SRC,
      g_param_spec_string ("fragment-src",
          "Fragment Source",
          "GLSL Fragment Shader source code", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (obj_class,
      PROP_ACTIVE,
      g_param_spec_string ("active",
          "Active", "Enable/Disable the shader", NULL,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (obj_class,
      PROP_COMPILED,
      g_param_spec_boolean ("compiled",
          "Compiled",
          "Shader compile and link status", FALSE,
          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));
}

void
gst_gl_shader_set_vertex_source (GstGLShader * shader, const gchar * src)
{
  GstGLShaderPrivate *priv;

  g_return_if_fail (GST_GL_IS_SHADER (shader));
  g_return_if_fail (src != NULL);

  priv = shader->priv;

  if (gst_gl_shader_is_compiled (shader))
    gst_gl_shader_release (shader);

  g_free (priv->vertex_src);

  priv->vertex_src = g_strdup (src);
}

void
gst_gl_shader_set_fragment_source (GstGLShader * shader, const gchar * src)
{
  GstGLShaderPrivate *priv;

  g_return_if_fail (GST_GL_IS_SHADER (shader));
  g_return_if_fail (src != NULL);

  priv = shader->priv;

  if (gst_gl_shader_is_compiled (shader))
    gst_gl_shader_release (shader);

  g_free (priv->fragment_src);

  priv->fragment_src = g_strdup (src);
}

const gchar *
gst_gl_shader_get_vertex_source (GstGLShader * shader)
{
  g_return_val_if_fail (GST_GL_IS_SHADER (shader), NULL);
  return shader->priv->vertex_src;
}

const gchar *
gst_gl_shader_get_fragment_source (GstGLShader * shader)
{
  g_return_val_if_fail (GST_GL_IS_SHADER (shader), NULL);
  return shader->priv->fragment_src;
}

static void
gst_gl_shader_init (GstGLShader * self)
{
  /* initialize sources and create program object */
  GstGLShaderPrivate *priv;

  priv = self->priv = GST_GL_SHADER_GET_PRIVATE (self);

  priv->vertex_src = NULL;
  priv->fragment_src = NULL;

  priv->fragment_handle = 0;
  priv->vertex_handle = 0;

  priv->compiled = FALSE;
  priv->active = FALSE;         /* unused at the moment */
}

static gboolean
_fill_vtable (GstGLShader * shader, GstGLContext * context)
{
  GstGLFuncs *gl = context->gl_vtable;
  GstGLShaderVTable *vtable = &shader->priv->vtable;

  if (gl->CreateProgram) {
    vtable->CreateProgram = gl->CreateProgram;
    vtable->DeleteProgram = gl->DeleteProgram;
    vtable->UseProgram = gl->UseProgram;

    vtable->CreateShader = gl->CreateShader;
    vtable->DeleteShader = gl->DeleteShader;
    vtable->AttachShader = gl->AttachShader;
    vtable->DetachShader = gl->DetachShader;

    vtable->GetAttachedShaders = gl->GetAttachedShaders;

    vtable->GetShaderInfoLog = gl->GetShaderInfoLog;
    vtable->GetShaderiv = gl->GetShaderiv;
    vtable->GetProgramInfoLog = gl->GetProgramInfoLog;
    vtable->GetProgramiv = gl->GetProgramiv;
  } else if (gl->CreateProgramObject) {
    vtable->CreateProgram = gl->CreateProgramObject;
    vtable->DeleteProgram = gl->DeleteObject;
    vtable->UseProgram = gl->UseProgramObject;

    vtable->CreateShader = gl->CreateShaderObject;
    vtable->DeleteShader = gl->DeleteObject;
    vtable->AttachShader = gl->AttachObject;
    vtable->DetachShader = gl->DetachObject;

    vtable->GetAttachedShaders = gl->GetAttachedObjects;

    vtable->GetShaderInfoLog = gl->GetInfoLog;
    vtable->GetShaderiv = gl->GetObjectParameteriv;
    vtable->GetProgramInfoLog = gl->GetInfoLog;
    vtable->GetProgramiv = gl->GetObjectParameteriv;
  } else {
    return FALSE;
  }

  return TRUE;
}

GstGLShader *
gst_gl_shader_new (GstGLContext * context)
{
  GstGLShader *shader;

  g_return_val_if_fail (GST_GL_IS_CONTEXT (context), NULL);

  shader = g_object_new (GST_GL_TYPE_SHADER, NULL);
  shader->context = gst_object_ref (context);

  return shader;
}

gboolean
gst_gl_shader_is_compiled (GstGLShader * shader)
{
  g_return_val_if_fail (GST_GL_IS_SHADER (shader), FALSE);

  return shader->priv->compiled;
}

gboolean
gst_gl_shader_compile (GstGLShader * shader, GError ** error)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;

  gchar info_buffer[2048];
  gint len = 0;
  GLint status = GL_FALSE;

  g_return_val_if_fail (GST_GL_IS_SHADER (shader), FALSE);

  priv = shader->priv;
  gl = shader->context->gl_vtable;

  if (priv->compiled)
    return priv->compiled;

  if (!_fill_vtable (shader, shader->context))
    return FALSE;

  shader->priv->program_handle = shader->priv->vtable.CreateProgram ();

  GST_TRACE ("shader created %u", shader->priv->program_handle);

  g_return_val_if_fail (priv->program_handle, FALSE);

  if (priv->vertex_src) {
    /* create vertex object */
    const gchar *vertex_source = priv->vertex_src;
    priv->vertex_handle = priv->vtable.CreateShader (GL_VERTEX_SHADER);
    gl->ShaderSource (priv->vertex_handle, 1, &vertex_source, NULL);
    /* compile */
    gl->CompileShader (priv->vertex_handle);
    /* check everything is ok */
    gl->GetShaderiv (priv->vertex_handle, GL_COMPILE_STATUS, &status);

    priv->vtable.GetShaderInfoLog (priv->vertex_handle,
        sizeof (info_buffer) - 1, &len, info_buffer);
    info_buffer[len] = '\0';

    if (status != GL_TRUE) {
      GST_ERROR ("Vertex Shader compilation failed:\n%s", info_buffer);

      g_set_error (error, GST_GL_SHADER_ERROR,
          GST_GL_SHADER_ERROR_COMPILE,
          "Vertex Shader compilation failed:\n%s", info_buffer);

      priv->vtable.DeleteShader (priv->vertex_handle);
      priv->compiled = FALSE;
      return priv->compiled;
    } else if (len > 1) {
      GST_FIXME ("vertex shader info log:\n%s\n", info_buffer);
    }
    priv->vtable.AttachShader (priv->program_handle, priv->vertex_handle);

    GST_LOG ("vertex shader attached %u", priv->vertex_handle);
  }

  if (priv->fragment_src) {
    /* create fragment object */
    const gchar *fragment_source = priv->fragment_src;
    priv->fragment_handle = priv->vtable.CreateShader (GL_FRAGMENT_SHADER);
    gl->ShaderSource (priv->fragment_handle, 1, &fragment_source, NULL);
    /* compile */
    gl->CompileShader (priv->fragment_handle);
    /* check everything is ok */
    priv->vtable.GetShaderiv (priv->fragment_handle,
        GL_COMPILE_STATUS, &status);

    priv->vtable.GetShaderInfoLog (priv->fragment_handle,
        sizeof (info_buffer) - 1, &len, info_buffer);
    info_buffer[len] = '\0';
    if (status != GL_TRUE) {
      GST_ERROR ("Fragment Shader compilation failed:\n%s", info_buffer);

      g_set_error (error, GST_GL_SHADER_ERROR,
          GST_GL_SHADER_ERROR_COMPILE,
          "Fragment Shader compilation failed:\n%s", info_buffer);

      priv->vtable.DeleteShader (priv->fragment_handle);
      priv->compiled = FALSE;
      return priv->compiled;
    } else if (len > 1) {
      GST_FIXME ("vertex shader info log:\n%s\n", info_buffer);
    }
    priv->vtable.AttachShader (priv->program_handle, priv->fragment_handle);

    GST_LOG ("fragment shader attached %u", priv->fragment_handle);
  }

  /* if nothing failed link shaders */
  gl->LinkProgram (priv->program_handle);
  priv->vtable.GetProgramiv (priv->program_handle, GL_LINK_STATUS, &status);

  priv->vtable.GetProgramInfoLog (priv->program_handle,
      sizeof (info_buffer) - 1, &len, info_buffer);
  info_buffer[len] = '\0';

  if (status != GL_TRUE) {
    GST_ERROR ("Shader linking failed:\n%s", info_buffer);

    g_set_error (error, GST_GL_SHADER_ERROR,
        GST_GL_SHADER_ERROR_LINK, "Shader Linking failed:\n%s", info_buffer);
    priv->compiled = FALSE;
    return priv->compiled;
  } else if (len > 1) {
    GST_FIXME ("shader link log:\n%s\n", info_buffer);
  }
  /* success! */
  priv->compiled = TRUE;
  g_object_notify (G_OBJECT (shader), "compiled");

  return priv->compiled;
}

void
gst_gl_shader_release (GstGLShader * shader)
{
  GstGLShaderPrivate *priv;

  g_return_if_fail (GST_GL_IS_SHADER (shader));

  priv = shader->priv;

  if (!priv->compiled || !priv->program_handle)
    return;

  if (priv->vertex_handle) {    /* not needed but nvidia doesn't care to respect the spec */
    GST_TRACE ("finalizing vertex shader %u", priv->vertex_handle);

    priv->vtable.DeleteShader (priv->vertex_handle);
  }

  if (priv->fragment_handle) {
    GST_TRACE ("finalizing fragment shader %u", priv->fragment_handle);

    priv->vtable.DeleteShader (priv->fragment_handle);
  }

  if (priv->vertex_handle)
    priv->vtable.DetachShader (priv->program_handle, priv->vertex_handle);
  if (priv->fragment_handle)
    priv->vtable.DetachShader (priv->program_handle, priv->fragment_handle);

  priv->compiled = FALSE;
  g_object_notify (G_OBJECT (shader), "compiled");
}

void
gst_gl_shader_use (GstGLShader * shader)
{
  GstGLShaderPrivate *priv;

  g_return_if_fail (GST_GL_IS_SHADER (shader));

  priv = shader->priv;

  g_return_if_fail (priv->program_handle);

  priv->vtable.UseProgram (priv->program_handle);

  return;
}

void
gst_gl_context_clear_shader (GstGLContext * context)
{
  GstGLFuncs *gl;

  g_return_if_fail (GST_GL_IS_CONTEXT (context));

  gl = context->gl_vtable;

  if (gl->CreateProgram)
    gl->UseProgram (0);
  else if (gl->CreateProgramObject)
    gl->UseProgramObject (0);
}

gboolean
gst_gl_shader_compile_and_check (GstGLShader * shader,
    const gchar * source, GstGLShaderSourceType type)
{
  gboolean is_compiled = FALSE;

  g_object_get (G_OBJECT (shader), "compiled", &is_compiled, NULL);

  if (!is_compiled) {
    GError *error = NULL;

    switch (type) {
      case GST_GL_SHADER_FRAGMENT_SOURCE:
        gst_gl_shader_set_fragment_source (shader, source);
        break;
      case GST_GL_SHADER_VERTEX_SOURCE:
        gst_gl_shader_set_vertex_source (shader, source);
        break;
      default:
        g_assert_not_reached ();
        break;
    }

    gst_gl_shader_compile (shader, &error);
    if (error) {
      gst_gl_context_set_error (shader->context, "%s", error->message);
      g_error_free (error);
      gst_gl_context_clear_shader (shader->context);

      return FALSE;
    }
  }
  return TRUE;
}

gboolean
gst_gl_shader_compile_all_with_attribs_and_check (GstGLShader * shader,
    const gchar * v_src, const gchar * f_src, const gint n_attribs,
    const gchar * attrib_names[], GLint attrib_locs[])
{
  gint i = 0;
  GError *error = NULL;

  gst_gl_shader_set_vertex_source (shader, v_src);
  gst_gl_shader_set_fragment_source (shader, f_src);

  gst_gl_shader_compile (shader, &error);
  if (error) {
    gst_gl_context_set_error (shader->context, "%s", error->message);
    g_error_free (error);
    gst_gl_context_clear_shader (shader->context);

    return FALSE;
  }

  for (i = 0; i < n_attribs; i++)
    attrib_locs[i] =
        gst_gl_shader_get_attribute_location (shader, attrib_names[i]);

  return TRUE;
}

#if GST_GL_HAVE_GLES2
gboolean
gst_gl_shader_compile_with_default_f_and_check (GstGLShader * shader,
    const gchar * v_src, const gint n_attribs, const gchar * attrib_names[],
    GLint attrib_locs[])
{
  return gst_gl_shader_compile_all_with_attribs_and_check (shader, v_src,
      simple_fragment_shader_str_gles2, n_attribs, attrib_names, attrib_locs);
}

gboolean
gst_gl_shader_compile_with_default_v_and_check (GstGLShader * shader,
    const gchar * f_src, GLint * pos_loc, GLint * tex_loc)
{
  const gchar *attrib_names[2] = { "a_position", "a_texCoord" };
  GLint attrib_locs[2] = { 0 };
  gboolean ret = TRUE;

  ret =
      gst_gl_shader_compile_all_with_attribs_and_check (shader,
      simple_vertex_shader_str_gles2, f_src, 2, attrib_names, attrib_locs);

  if (ret) {
    *pos_loc = attrib_locs[0];
    *tex_loc = attrib_locs[1];
  }

  return ret;
}

gboolean
gst_gl_shader_compile_with_default_vf_and_check (GstGLShader * shader,
    GLint * pos_loc, GLint * tex_loc)
{
  return gst_gl_shader_compile_with_default_v_and_check (shader,
      simple_fragment_shader_str_gles2, pos_loc, tex_loc);
}
#endif

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

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

  location = gl->GetUniformLocation (priv->program_handle, name);

  gl->UniformMatrix4x3fv (location, count, transpose, value);
}
#endif /* GST_GL_HAVE_OPENGL */

GLint
gst_gl_shader_get_attribute_location (GstGLShader * shader, const gchar * name)
{
  GstGLShaderPrivate *priv;
  GstGLFuncs *gl;

  g_return_val_if_fail (shader != NULL, 0);
  priv = shader->priv;
  g_return_val_if_fail (priv->program_handle != 0, 0);
  gl = shader->context->gl_vtable;

  return gl->GetAttribLocation (priv->program_handle, name);
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

  gl->BindAttribLocation (priv->program_handle, index, name);
}

GQuark
gst_gl_shader_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-shader-error");
}
