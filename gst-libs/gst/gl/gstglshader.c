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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstglshader.h"

#define GST_GL_SHADER_GET_PRIVATE(o)					\
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_GL_TYPE_SHADER, GstGLShaderPrivate))

enum
{
  PROP_0,
  PROP_VERTEX_SRC,
  PROP_FRAGMENT_SRC,
  PROP_COMPILED,
  PROP_ACTIVE                   //unused
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
};

G_DEFINE_TYPE (GstGLShader, gst_gl_shader, G_TYPE_OBJECT);

#undef G_LOG_DOMAIN
#define G_LOG_DOMAIN "GstGLShader"

gboolean _gst_gl_shader_debug = FALSE;

static void
gst_gl_shader_finalize (GObject * object)
{
  GstGLShader *shader;
  GstGLShaderPrivate *priv;
/*  GLint status = GL_FALSE; */

  shader = GST_GL_SHADER (object);
  priv = shader->priv;

  g_free (priv->vertex_src);
  g_free (priv->fragment_src);

  /* release shader objects */
  gst_gl_shader_release (shader);

  /* delete program */
  if (priv->program_handle) {
    glDeleteObjectARB (priv->program_handle);
    glGetError ();
    /* g_debug ("error: 0x%x", err);  */
    /* glGetObjectParameterivARB(priv->program_handle, GL_OBJECT_DELETE_STATUS_ARB, &status); */
    /* g_debug ("program deletion status:%s", status == GL_TRUE ? "true" : "false" ); */
  }

  priv->fragment_handle = 0;
  priv->vertex_handle = 0;
  priv->program_handle = 0;

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
gst_gl_shader_log_handler (const gchar *domain, GLogLevelFlags flags,
                           const gchar *message, gpointer user_data)
{
  if (_gst_gl_shader_debug) {
    g_log_default_handler (domain, flags, message, user_data);
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
          "GLSL Vertex Shader source code", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (obj_class,
      PROP_FRAGMENT_SRC,
      g_param_spec_string ("fragment-src",
          "Fragment Source",
          "GLSL Fragment Shader source code", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (obj_class,
      PROP_ACTIVE,
      g_param_spec_string ("active",
          "Active", "Enable/Disable the shader", NULL, G_PARAM_READWRITE));
  g_object_class_install_property (obj_class,
      PROP_COMPILED,
      g_param_spec_boolean ("compiled",
          "Compiled",
          "Shader compile and link status", FALSE, G_PARAM_READABLE));
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

G_CONST_RETURN gchar *
gst_gl_shader_get_vertex_source (GstGLShader * shader)
{
  g_return_val_if_fail (GST_GL_IS_SHADER (shader), NULL);
  return shader->priv->vertex_src;
}

G_CONST_RETURN gchar *
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
  priv->program_handle = glCreateProgramObjectARB ();

  g_assert (priv->program_handle);

  priv->compiled = FALSE;
  priv->active = FALSE;         // unused at the moment

  if (g_getenv ("GST_GL_SHADER_DEBUG") != NULL)
    _gst_gl_shader_debug = TRUE;

  g_log_set_handler ("GstGLShader", G_LOG_LEVEL_DEBUG,
                     gst_gl_shader_log_handler, NULL);
}

GstGLShader *
gst_gl_shader_new (void)
{
  return g_object_new (GST_GL_TYPE_SHADER, NULL);
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

  gchar info_buffer[2048];
  GLsizei len = 0;
  GLint status = GL_FALSE;

  g_return_val_if_fail (GST_GL_IS_SHADER (shader), FALSE);

  priv = shader->priv;

  if (priv->compiled)
    return priv->compiled;

  g_assert (priv->program_handle);

  if (priv->vertex_src) {
    /* create vertex object */
    const gchar *vertex_source = priv->vertex_src;
    priv->vertex_handle = glCreateShaderObjectARB (GL_VERTEX_SHADER);
    glShaderSourceARB (priv->vertex_handle, 1, &vertex_source, NULL);
    /* compile */
    glCompileShaderARB (priv->vertex_handle);
    /* check everything is ok */
    glGetObjectParameterivARB (priv->vertex_handle,
        GL_OBJECT_COMPILE_STATUS_ARB, &status);

    glGetInfoLogARB (priv->vertex_handle,
        sizeof (info_buffer) - 1, &len, info_buffer);
    info_buffer[len] = '\0';

    if (status != GL_TRUE) {
      g_set_error (error, GST_GL_SHADER_ERROR,
          GST_GL_SHADER_ERROR_COMPILE,
          "Vertex Shader compilation failed:\n%s", info_buffer);

      glDeleteObjectARB (priv->vertex_handle);
      priv->compiled = FALSE;
      return priv->compiled;
    } else if (len > 1) {
      g_debug ("\n%s\n", info_buffer);
    }
    glAttachObjectARB (priv->program_handle, priv->vertex_handle);
  }

  if (priv->fragment_src) {
    /* create fragment object */
    const gchar *fragment_source = priv->fragment_src;
    priv->fragment_handle = glCreateShaderObjectARB (GL_FRAGMENT_SHADER_ARB);
    glShaderSourceARB (priv->fragment_handle, 1, &fragment_source, NULL);
    /* compile */
    glCompileShaderARB (priv->fragment_handle);
    /* check everything is ok */
    glGetObjectParameterivARB (priv->fragment_handle,
        GL_OBJECT_COMPILE_STATUS_ARB, &status);

    glGetInfoLogARB (priv->fragment_handle,
        sizeof (info_buffer) - 1, &len, info_buffer);
    info_buffer[len] = '\0';
    if (status != GL_TRUE) {
      g_set_error (error, GST_GL_SHADER_ERROR,
          GST_GL_SHADER_ERROR_COMPILE,
          "Fragment Shader compilation failed:\n%s", info_buffer);

      glDeleteObjectARB (priv->fragment_handle);
      priv->compiled = FALSE;
      return priv->compiled;
    } else if (len > 1) {
      g_debug ("\n%s\n", info_buffer);
    }
    glAttachObjectARB (priv->program_handle, priv->fragment_handle);
  }

  /* if nothing failed link shaders */
  glLinkProgramARB (priv->program_handle);

  glGetObjectParameterivARB (priv->program_handle, GL_LINK_STATUS, &status);

  glGetInfoLogARB (priv->program_handle,
      sizeof (info_buffer) - 1, &len, info_buffer);
  info_buffer[len] = '\0';

  if (status != GL_TRUE) {
    g_set_error (error, GST_GL_SHADER_ERROR,
        GST_GL_SHADER_ERROR_LINK, "Shader Linking failed:\n%s", info_buffer);
    priv->compiled = FALSE;
    return priv->compiled;
  } else if (len > 1) {
    g_debug ("\n%s\n", info_buffer);
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
  /* GLint status; */
  /* GLenum err = 0; */

  g_return_if_fail (GST_GL_IS_SHADER (shader));

  priv = shader->priv;

  g_assert (priv->program_handle);

  if (!priv->compiled)
    return;

  if (priv->vertex_handle) {    // not needed but nvidia doesn't care to respect the spec
    glDeleteObjectARB (priv->vertex_handle);

    /* err = glGetError (); */
    /* g_debug ("error: 0x%x", err); */
    /* glGetObjectParameterivARB(priv->vertex_handle, GL_OBJECT_DELETE_STATUS_ARB, &status); */
    /* g_debug ("vertex deletion status:%s", status == GL_TRUE ? "true" : "false" ); */
  }

  if (priv->fragment_handle) {
    glDeleteObjectARB (priv->fragment_handle);

    /* err = glGetError (); */
    /* g_debug ("error: 0x%x", err); */
    /* glGetObjectParameterivARB(priv->fragment_handle, GL_OBJECT_DELETE_STATUS_ARB, &status); */
    /* g_debug ("fragment deletion status:%s", status == GL_TRUE ? "true" : "false" ); */
  }

  if (priv->vertex_handle)
    glDetachObjectARB (priv->program_handle, priv->vertex_handle);
  if (priv->fragment_handle)
    glDetachObjectARB (priv->program_handle, priv->fragment_handle);

  priv->compiled = FALSE;
  g_object_notify (G_OBJECT (shader), "compiled");
}

void
gst_gl_shader_use (GstGLShader * shader)
{
  GstGLShaderPrivate *priv;

  if (!shader) {
    glUseProgramObjectARB (0);
    return;
  }

  priv = shader->priv;

  g_assert (priv->program_handle);

  glUseProgramObjectARB (priv->program_handle);

  return;
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
      g_warning ("%s", error->message);
      g_error_free (error);
      error = NULL;
      gst_gl_shader_use (NULL);
      return FALSE;
    }
  }
  return TRUE;
}

void
gst_gl_shader_set_uniform_1f (GstGLShader * shader, const gchar * name,
    gfloat value)
{
  GstGLShaderPrivate *priv;
  GLint location = -1;

  priv = shader->priv;

  g_return_if_fail (priv->program_handle != 0);

  location = glGetUniformLocationARB (priv->program_handle, name);

  glUniform1fARB (location, value);
}

void
gst_gl_shader_set_uniform_1fv (GstGLShader * shader, const gchar * name,
    guint count, gfloat * value)
{
  GstGLShaderPrivate *priv;
  GLint location = -1;

  priv = shader->priv;

  g_return_if_fail (priv->program_handle != 0);

  location = glGetUniformLocationARB (priv->program_handle, name);

  glUniform1fvARB (location, count, value);
}

void
gst_gl_shader_set_uniform_1i (GstGLShader * shader, const gchar * name,
    gint value)
{
  GstGLShaderPrivate *priv;
  GLint location = -1;

  priv = shader->priv;

  g_return_if_fail (priv->program_handle != 0);

  location = glGetUniformLocationARB (priv->program_handle, name);

  glUniform1iARB (location, value);
}

GLint
gst_gl_shader_get_attribute_location (GstGLShader * shader, const gchar *name)
{
  GstGLShaderPrivate *priv;

  priv = shader->priv;

  g_return_val_if_fail (priv->program_handle != 0, 0);

  return glGetAttribLocationARB (priv->program_handle, name);
}

GQuark
gst_gl_shader_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-shader-error");
}
