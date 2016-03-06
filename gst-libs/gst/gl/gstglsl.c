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

#include "gstglsl.h"
#include "gstglsl_private.h"

/**
 * SECTION:gstglsl
 * @short_description: helpers for dealing with OpenGL shaders
 * @see_also: #GstGLSLStage, #GstGLShader
 */

GQuark
gst_glsl_error_quark (void)
{
  return g_quark_from_static_string ("gst-glsl-error");
}

struct glsl_version_string
{
  GstGLSLVersion version;
  const gchar *name;
};

static const struct glsl_version_string glsl_versions[] = {
  /* keep in sync with definition in the header */
  {GST_GLSL_VERSION_100, "100"},
  {GST_GLSL_VERSION_110, "110"},
  {GST_GLSL_VERSION_120, "120"},
  {GST_GLSL_VERSION_130, "130"},
  {GST_GLSL_VERSION_140, "140"},
  {GST_GLSL_VERSION_150, "150"},
  {GST_GLSL_VERSION_300, "300"},
  {GST_GLSL_VERSION_310, "310"},
  {GST_GLSL_VERSION_320, "320"},
  {GST_GLSL_VERSION_330, "330"},
  {GST_GLSL_VERSION_400, "400"},
  {GST_GLSL_VERSION_410, "410"},
  {GST_GLSL_VERSION_420, "420"},
  {GST_GLSL_VERSION_430, "430"},
  {GST_GLSL_VERSION_440, "440"},
  {GST_GLSL_VERSION_450, "450"},
};

struct glsl_profile_string
{
  GstGLSLProfile profile;
  const gchar *name;
};

static const struct glsl_profile_string glsl_profiles[] = {
  /* keep in sync with definition in the header */
  {GST_GLSL_PROFILE_ES, "es"},
  {GST_GLSL_PROFILE_CORE, "core"},
  {GST_GLSL_PROFILE_COMPATIBILITY, "compatibility"},
};

const gchar *
gst_glsl_version_to_string (GstGLSLVersion version)
{
  int i;

  if (version == GST_GLSL_VERSION_NONE)
    return NULL;

  for (i = 0; i < G_N_ELEMENTS (glsl_versions); i++) {
    if (version == glsl_versions[i].version)
      return glsl_versions[i].name;
  }

  return NULL;
}

GstGLSLVersion
gst_glsl_version_from_string (const gchar * string)
{
  gchar *str;
  int i;

  if (string == NULL)
    return 0;

  str = g_strdup (string);
  str = g_strstrip (str);

  for (i = 0; i < G_N_ELEMENTS (glsl_versions); i++) {
    if (g_strcmp0 (str, glsl_versions[i].name) == 0) {
      g_free (str);
      return glsl_versions[i].version;
    }
  }

  g_free (str);
  return 0;
}

const gchar *
gst_glsl_profile_to_string (GstGLSLProfile profile)
{
  int i;

  if (profile == GST_GLSL_PROFILE_NONE)
    return NULL;

  /* multiple profiles are not allowed */
  if ((profile & (profile - 1)) != 0)
    return NULL;

  for (i = 0; i < G_N_ELEMENTS (glsl_profiles); i++) {
    if (profile == glsl_profiles[i].profile)
      return glsl_profiles[i].name;
  }

  return NULL;
}

GstGLSLProfile
gst_glsl_profile_from_string (const gchar * string)
{
  gchar *str;
  int i;

  if (string == NULL)
    return GST_GLSL_PROFILE_NONE;

  str = g_strdup (string);
  str = g_strstrip (str);

  for (i = 0; i < G_N_ELEMENTS (glsl_profiles); i++) {
    if (g_strcmp0 (str, glsl_profiles[i].name) == 0) {
      g_free (str);
      return glsl_profiles[i].profile;
    }
  }

  g_free (str);
  return GST_GLSL_PROFILE_NONE;
}

static gboolean
_is_valid_version_profile (GstGLSLVersion version, GstGLSLProfile profile)
{
  if (version == GST_GLSL_VERSION_NONE)
    return TRUE;

  /* versions that may not need an explicit profile */
  if (version <= GST_GLSL_VERSION_150 && profile == GST_GLSL_PROFILE_NONE)
    return TRUE;

  /* ES versions require an ES profile */
  if (version == GST_GLSL_VERSION_100 || version == GST_GLSL_VERSION_300
      || version == GST_GLSL_VERSION_310 || version == GST_GLSL_VERSION_320)
    return profile == GST_GLSL_PROFILE_ES;

  /* required profile and no ES profile for normal GL contexts */
  if (version >= GST_GLSL_VERSION_330)
    return profile == GST_GLSL_PROFILE_NONE || profile == GST_GLSL_PROFILE_CORE
        || profile == GST_GLSL_PROFILE_COMPATIBILITY;

  if (version <= GST_GLSL_VERSION_150)
    return profile == GST_GLSL_PROFILE_NONE
        || profile == GST_GLSL_PROFILE_COMPATIBILITY;

  return FALSE;
}

gchar *
gst_glsl_version_profile_to_string (GstGLSLVersion version,
    GstGLSLProfile profile)
{
  const gchar *version_s, *profile_s;

  if (!_is_valid_version_profile (version, profile))
    return NULL;

  version_s = gst_glsl_version_to_string (version);
  /* no profiles in GL/ES <= 150 */
  if (version <= GST_GLSL_VERSION_150)
    profile_s = NULL;
  else
    profile_s = gst_glsl_profile_to_string (profile);

  if (!version_s)
    return NULL;

  if (profile_s)
    return g_strdup_printf ("%s %s", version_s, profile_s);

  return g_strdup (version_s);
}

static void
_fixup_version_profile (GstGLSLVersion * version, GstGLSLProfile * profile)
{
  if (*version == GST_GLSL_VERSION_100 || *version == GST_GLSL_VERSION_300
      || *version == GST_GLSL_VERSION_310 || *version == GST_GLSL_VERSION_320)
    *profile = GST_GLSL_PROFILE_ES;
  else if (*version <= GST_GLSL_VERSION_150)
    *profile = GST_GLSL_PROFILE_COMPATIBILITY;
  else if (*profile == GST_GLSL_PROFILE_NONE
      && *version >= GST_GLSL_VERSION_330)
    *profile = GST_GLSL_PROFILE_CORE;
}

/* @str points to the start of "#version", "#    version" or "#\tversion", etc */
static const gchar *
_check_valid_version_preprocessor_string (const gchar * str)
{
  gint i = 0;

  if (!str || !str[i])
    return NULL;

  /* there can be whitespace between the '#' and 'version' */
  do {
    i++;
    if (str[i] == '\0' || str[i] == '\n' || str[i] == '\r')
      return NULL;
  } while (g_ascii_isspace (str[i]));
  if (g_strstr_len (&str[i], 7, "version"))
    return &str[i + 7];

  return NULL;
}

gboolean
gst_glsl_version_profile_from_string (const gchar * string,
    GstGLSLVersion * version_ret, GstGLSLProfile * profile_ret)
{
  gchar *str, *version_s, *profile_s;
  GstGLSLVersion version = GST_GLSL_VERSION_NONE;
  GstGLSLProfile profile = GST_GLSL_PROFILE_NONE;
  gint i;

  if (!string)
    goto error;

  str = g_strdup (string);
  version_s = g_strstrip (str);

  /* skip possible #version prefix */
  if (str[0] == '#') {
    if (!(version_s =
            (gchar *) _check_valid_version_preprocessor_string (version_s))) {
      g_free (str);
      goto error;
    }
  }

  version_s = g_strstrip (version_s);

  i = 0;
  while (version_s && version_s[i] != '\0' && g_ascii_isdigit (version_s[i]))
    i++;
  /* wrong version length */
  if (i != 3) {
    g_free (str);
    goto error;
  }

  if (version_s[i] != 0) {
    version_s[i] = '\0';
    i++;
    profile_s = &version_s[i];
    profile_s = g_strstrip (profile_s);

    profile = gst_glsl_profile_from_string (profile_s);
  }
  version = gst_glsl_version_from_string (version_s);
  g_free (str);

  /* check whether the parsed data is valid */
  if (!version)
    goto error;
  if (!_is_valid_version_profile (version, profile))
    goto error;
  /* got a profile when none was expected */
  if (version <= GST_GLSL_VERSION_150 && profile != GST_GLSL_PROFILE_NONE)
    goto error;

  _fixup_version_profile (&version, &profile);

  if (profile_ret)
    *profile_ret = profile;
  if (version_ret)
    *version_ret = version;

  return TRUE;

error:
  {
    if (profile_ret)
      *profile_ret = GST_GLSL_PROFILE_NONE;
    if (version_ret)
      *version_ret = GST_GLSL_VERSION_NONE;
    return FALSE;
  }
}

/* returns the pointer in @str to the #version declaration */
const gchar *
_gst_glsl_shader_string_find_version (const gchar * str)
{
  gboolean sl_comment = FALSE;
  gboolean ml_comment = FALSE;
  gboolean newline = TRUE;
  gint i = 0;

  /* search for #version while allowing for preceeding comments/whitespace as
   * permitted by the GLSL specification */
  while (str && str[i] != '\0' && i < 1024) {
    if (str[i] == '\n' || str[i] == '\r') {
      newline = TRUE;
      sl_comment = FALSE;
      i++;
      continue;
    }

    if (g_ascii_isspace (str[i]))
      goto next;

    if (sl_comment)
      goto next;

    if (ml_comment) {
      if (g_strstr_len (&str[i], 2, "*/")) {
        ml_comment = FALSE;
        i++;
      }
      goto next;
    }

    if (g_strstr_len (&str[i], 2, "//")) {
      sl_comment = TRUE;
      i++;
      goto next;
    }

    if (g_strstr_len (&str[i], 2, "/*")) {
      ml_comment = TRUE;
      i++;
      goto next;
    }

    if (str[i] == '#') {
      if (newline && _check_valid_version_preprocessor_string (&str[i]))
        return &str[i];
      break;
    }

  next:
    newline = FALSE;
    i++;
  }

  return NULL;
}

gboolean
gst_glsl_string_get_version_profile (const gchar * s, GstGLSLVersion * version,
    GstGLSLProfile * profile)
{
  const gchar *version_profile_s;

  version_profile_s = _gst_glsl_shader_string_find_version (s);
  if (!version_profile_s)
    goto error;

  if (!gst_glsl_version_profile_from_string (version_profile_s, version,
          profile))
    goto error;

  return TRUE;

error:
  {
    if (version)
      *version = GST_GLSL_VERSION_NONE;
    if (profile)
      *profile = GST_GLSL_PROFILE_NONE;
    return FALSE;
  }
}

GstGLSLVersion
gst_gl_version_to_glsl_version (GstGLAPI gl_api, gint maj, gint min)
{
  g_return_val_if_fail (gl_api != GST_GL_API_NONE, 0);

  if (gl_api & GST_GL_API_GLES2) {
    if (maj == 2 && min == 0)
      return 100;

    if (maj == 3 && min >= 0 && min <= 2)
      return maj * 100 + min * 10;

    return 0;
  }

  /* versions match for >= 3.3 */
  if (gl_api & (GST_GL_API_OPENGL3 | GST_GL_API_OPENGL)) {
    if (maj > 3 || (maj == 3 && min >= 3))
      return maj * 100 + min * 10;

    if (maj == 3 && min == 2)
      return 150;
    if (maj == 3 && min == 1)
      return 140;
    if (maj == 3 && min == 0)
      return 130;
    if (maj == 2 && min == 1)
      return 120;
    if (maj == 2 && min == 0)
      return 110;

    return 0;
  }

  return 0;
}

gboolean
gst_gl_context_supports_glsl_profile_version (GstGLContext * context,
    GstGLSLVersion version, GstGLSLProfile profile)
{
  g_return_val_if_fail (GST_IS_GL_CONTEXT (context), FALSE);

  if (!_is_valid_version_profile (version, profile))
    return FALSE;

  if (profile != GST_GLSL_PROFILE_NONE) {
    if (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2, 0)) {
      if ((profile & GST_GLSL_PROFILE_ES) == 0)
        return FALSE;
    } else if ((gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL) != 0) {
      if ((profile & GST_GLSL_PROFILE_COMPATIBILITY) == 0)
        return FALSE;
    } else if ((gst_gl_context_get_gl_api (context) & GST_GL_API_OPENGL3) != 0) {
      /* GL_ARB_es2_compatibility is requried for GL3 contexts */
      if ((profile & (GST_GLSL_PROFILE_CORE | GST_GLSL_PROFILE_ES)) == 0)
        return FALSE;
    } else {
      g_assert_not_reached ();
    }
  }

  if (version != GST_GLSL_VERSION_NONE) {
    GstGLAPI gl_api;
    gint maj, min, glsl_version;

    if (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3, 1)) {
      if (version > GST_GLSL_VERSION_310)
        return FALSE;
    } else if (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 3,
            0)) {
      if (version > GST_GLSL_VERSION_300)
        return FALSE;
    } else if (gst_gl_context_check_gl_version (context, GST_GL_API_GLES2, 2,
            0)) {
      if (version > GST_GLSL_VERSION_100)
        return FALSE;
    }

    gl_api = gst_gl_context_get_gl_api (context);
    gst_gl_context_get_gl_version (context, &maj, &min);
    glsl_version = gst_gl_version_to_glsl_version (gl_api, maj, min);
    if (version > glsl_version)
      return FALSE;

    if (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL3, 1, 0))
      /* GL_ARB_es2_compatibility is requried for GL3 contexts */
      if (version < GST_GLSL_VERSION_150 && version != GST_GLSL_VERSION_100)
        return FALSE;

    if (gst_gl_context_check_gl_version (context, GST_GL_API_OPENGL, 1, 0)
        && version < GST_GLSL_VERSION_110)
      return FALSE;
  }

  return TRUE;
}

gboolean
_gst_glsl_funcs_fill (GstGLSLFuncs * vtable, GstGLContext * context)
{
  GstGLFuncs *gl = context->gl_vtable;

  if (vtable->initialized)
    return TRUE;

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
    vtable->initialized = FALSE;
    return FALSE;
  }

  vtable->initialized = TRUE;
  return TRUE;
}
