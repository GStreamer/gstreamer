/*
 * GStreamer
 * Copyright (C) 2012 Matthew Waters <ystreet00@gmail.com>
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

#include <string.h>

#include "gstglfeature.h"

#include "gstglapi.h"

gboolean
gst_gl_check_extension (const char *name, const gchar * ext)
{
  char *end;
  int name_len, n;

  if (name == NULL || ext == NULL)
    return FALSE;

  end = (char *) (ext + strlen (ext));

  name_len = strlen (name);

  while (ext < end) {
    n = strcspn (ext, " ");

    if ((name_len == n) && (!strncmp (name, ext, n)))
      return TRUE;
    ext += (n + 1);
  }

  return FALSE;
}

/* Define a set of arrays containing the functions required from GL
   for each feature */
#define GST_GL_EXT_BEGIN(name,                                            \
                       min_gl_major, min_gl_minor,                      \
                       gles_availability,                               \
                       namespaces, extension_names)                     \
  static const GstGLFeatureFunction gst_gl_ext_ ## name ## _funcs[] = {
#define GST_GL_EXT_FUNCTION(ret, name, args)                          \
  { G_STRINGIFY (name), G_STRUCT_OFFSET (GstGLFuncs, name) },
#define GST_GL_EXT_END()                      \
  { NULL, 0 },                                  \
  };
#include "glprototypes/all_functions.h"

#undef GST_GL_EXT_BEGIN
#define GST_GL_EXT_BEGIN(name,                                          \
                       min_gl_major, min_gl_minor,                      \
                       gles_availability,                               \
                       namespaces, extension_names)                     \
  { min_gl_major, min_gl_minor, gles_availability, namespaces,          \
    extension_names,                                                    \
    gst_gl_ext_ ## name ## _funcs },
#undef GST_GL_EXT_FUNCTION
#define GST_GL_EXT_FUNCTION(ret, name, args)
#undef GST_GL_EXT_END
#define GST_GL_EXT_END()

static const GstGLFeatureData gst_gl_feature_ext_functions_data[] = {
#include "glprototypes/all_functions.h"
};

#undef GST_GL_EXT_BEGIN
#undef GST_GL_EXT_FUNCTION
#undef GST_GL_EXT_END

gboolean
_gst_gl_feature_check_for_extension (const GstGLFeatureData * data,
    const char *driver_prefix, const char *extensions_string,
    const char **suffix)
{
  const char *namespace, *namespace_suffix;
  unsigned int namespace_len;

  g_return_val_if_fail (suffix != NULL, FALSE);

  for (namespace = data->namespaces; *namespace;
      namespace += strlen (namespace) + 1) {
    const char *extension;
    GString *full_extension_name = g_string_new ("");

    /* If the namespace part contains a ':' then the suffix for
       the function names is different from the name space */
    if ((namespace_suffix = strchr (namespace, ':'))) {
      namespace_len = namespace_suffix - namespace;
      namespace_suffix++;
    } else {
      namespace_len = strlen (namespace);
      namespace_suffix = namespace;
    }

    for (extension = data->extension_names; *extension;
        extension += strlen (extension) + 1) {
      g_string_assign (full_extension_name, driver_prefix);
      g_string_append_c (full_extension_name, '_');
      g_string_append_len (full_extension_name, namespace, namespace_len);
      g_string_append_c (full_extension_name, '_');
      g_string_append (full_extension_name, extension);

      if (gst_gl_check_extension (full_extension_name->str, extensions_string)) {
        GST_TRACE ("found %s in extension string", full_extension_name->str);
        break;
      }
    }

    g_string_free (full_extension_name, TRUE);

    /* If we found an extension with this namespace then use it
       as the suffix */
    if (*extension) {
      *suffix = namespace_suffix;
      return TRUE;
    }
  }

  return FALSE;
}

gboolean
_gst_gl_feature_check (GstGLDisplay * display,
    const char *driver_prefix,
    const GstGLFeatureData * data,
    int gl_major, int gl_minor, const char *extensions_string)
{
  char *full_function_name = NULL;
  gboolean in_core = FALSE;
  const char *suffix = NULL;
  int func_num;
  GstGLFuncs *gst_gl = display->gl_vtable;

  /* First check whether the functions should be directly provided by
     GL */
  if (((display->gl_api & GST_GL_API_OPENGL) &&
          GST_GL_CHECK_GL_VERSION (gl_major, gl_minor,
              data->min_gl_major, data->min_gl_minor)) ||
      ((display->gl_api & GST_GL_API_GLES2) &&
          (data->gl_availability & GST_GL_API_GLES2))) {
    in_core = TRUE;
    suffix = "";
  } else {
    /* Otherwise try all of the extensions */
    if (!_gst_gl_feature_check_for_extension (data, driver_prefix,
            extensions_string, &suffix))
      goto error;
  }

  /* If we couldn't find anything that provides the functions then
     give up */
  if (suffix == NULL)
    goto error;

  /* Try to get all of the entry points */
  for (func_num = 0; data->functions[func_num].name; func_num++) {
    void *func;

    if (full_function_name)
      g_free (full_function_name);

    full_function_name = g_strconcat ("gl", data->functions[func_num].name,
        suffix, NULL);
    GST_TRACE ("%s should %sbe in core", full_function_name,
        in_core ? "" : "not ");
    func =
        gst_gl_window_get_proc_address (display->gl_window, full_function_name);

    if (func == NULL && in_core) {
      GST_TRACE ("%s was not found in core, trying the extension version",
          full_function_name);
      if (!_gst_gl_feature_check_for_extension (data, driver_prefix,
              extensions_string, &suffix)) {
        goto error;
      } else {
        g_free (full_function_name);
        full_function_name = g_strconcat ("gl", data->functions[func_num].name,
            suffix, NULL);
        func = gst_gl_window_get_proc_address (display->gl_window,
            full_function_name);
      }
    }

    if (func == NULL) {
      goto error;
    }

    /* Set the function pointer in the context */
    *(void **) ((guint8 *) gst_gl +
        data->functions[func_num].pointer_offset) = func;

  }

  g_free (full_function_name);

  return TRUE;

  /* If the extension isn't found or one of the functions wasn't found
   * then set all of the functions pointers to NULL so we can safely
   * do feature testing by just looking at the function pointers */
error:

  for (func_num = 0; data->functions[func_num].name; func_num++) {
    *(void **) ((guint8 *) gst_gl +
        data->functions[func_num].pointer_offset) = NULL;
  }

  if (full_function_name) {
    GST_TRACE ("failed to find function %s", full_function_name);
    g_free (full_function_name);
  }

  return FALSE;
}

void
_gst_gl_feature_check_ext_functions (GstGLDisplay * display,
    int gl_major, int gl_minor, const char *gl_extensions)
{
  int i;

  for (i = 0; i < G_N_ELEMENTS (gst_gl_feature_ext_functions_data); i++) {
    _gst_gl_feature_check (display, "GL",
        gst_gl_feature_ext_functions_data + i, gl_major, gl_minor,
        gl_extensions);
  }
}
