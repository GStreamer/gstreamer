/*
 * gstvaapiutils_egl.c - EGL utilities
 *
 * Copyright (C) 2014 Intel Corporation
 *   Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301
 */

#include "sysdeps.h"
#include "gstvaapiutils_egl.h"
#if USE_GST_GL_HELPERS
# include <gst/gl/gl.h>
# if GST_GL_HAVE_PLATFORM_EGL
#  include <gst/gl/egl/gstgldisplay_egl.h>
# endif
#endif

#define DEBUG 1
#include "gstvaapidebug.h"

typedef struct egl_message_s EglMessage;
struct egl_message_s
{
  EglObject base;
  EglContextRunFunc func;
  gpointer args;
};

static void
egl_message_finalize (EglMessage * msg)
{
}

/* ------------------------------------------------------------------------- */
// Utility functions

typedef struct gl_version_info_s GlVersionInfo;
struct gl_version_info_s
{
  guint gles_version;
  guint gl_api_bit;
  guint gl_api;
  const gchar *gl_api_name;
};

static const GlVersionInfo gl_version_info[] = {
  {0, EGL_OPENGL_BIT, EGL_OPENGL_API, "OpenGL"},
  {1, EGL_OPENGL_ES_BIT, EGL_OPENGL_ES_API, "OpenGL_ES"},
  {2, EGL_OPENGL_ES2_BIT, EGL_OPENGL_ES_API, "OpenGL_ES2"},
  {3, EGL_OPENGL_ES3_BIT_KHR, EGL_OPENGL_ES_API, "OpenGL_ES3"},
  {0,}
};

static const GlVersionInfo *
gl_version_info_lookup (guint gles_version)
{
  const GlVersionInfo *vinfo;

  for (vinfo = gl_version_info; vinfo->gl_api_bit != 0; vinfo++) {
    if (vinfo->gles_version == gles_version)
      return vinfo;
  }
  return NULL;
}

static const GlVersionInfo *
gl_version_info_lookup_by_api (guint api)
{
  const GlVersionInfo *vinfo;

  for (vinfo = gl_version_info; vinfo->gl_api_bit != 0; vinfo++) {
    if (api & vinfo->gl_api_bit)
      return vinfo;
  }
  return NULL;
}

static const GlVersionInfo *
gl_version_info_lookup_by_api_name (const gchar * name)
{
  const GlVersionInfo *vinfo;

  for (vinfo = gl_version_info; vinfo->gl_api_bit != 0; vinfo++) {
    if (g_strcmp0 (vinfo->gl_api_name, name) == 0)
      return vinfo;
  }
  return NULL;
}

static gboolean
g_strv_match_string (gchar ** extensions_list, const gchar * name)
{
  if (extensions_list) {
    for (; *extensions_list != NULL; extensions_list++) {
      if (g_strcmp0 (*extensions_list, name) == 0)
        return TRUE;
    }
  }
  return FALSE;
}

static gboolean
egl_find_attrib_value (const EGLint * attribs, EGLint type, EGLint * value_ptr)
{
  while (attribs[0] != EGL_NONE) {
    if (attribs[0] == type) {
      if (value_ptr)
        *value_ptr = attribs[1];
      return TRUE;
    }
    attribs += 2;
  }
  return FALSE;
}

/* ------------------------------------------------------------------------- */
// Basic objects

#define EGL_OBJECT(object) ((EglObject *)(object))
#define EGL_OBJECT_CLASS(klass) ((EglObjectClass *)(klass))

#define EGL_OBJECT_DEFINE_CLASS_WITH_CODE(TN, t_n, code)        \
static void                                                     \
G_PASTE(t_n,_finalize) (TN * object);                           \
                                                                \
static inline const EglObjectClass *                            \
G_PASTE(t_n,_class) (void)                                      \
{                                                               \
  static G_PASTE(TN,Class) g_class;                             \
  static gsize g_class_init = FALSE;                            \
                                                                \
  if (g_once_init_enter (&g_class_init)) {                      \
    GstVaapiMiniObjectClass *const object_class =               \
        GST_VAAPI_MINI_OBJECT_CLASS (&g_class);                 \
    code;                                                       \
    object_class->size = sizeof (TN);                           \
    object_class->finalize = (GDestroyNotify)                   \
      G_PASTE(t_n,_finalize);                                   \
    g_once_init_leave (&g_class_init, TRUE);                    \
  }                                                             \
  return EGL_OBJECT_CLASS (&g_class);                           \
}

#define EGL_OBJECT_DEFINE_CLASS(TN, t_n) \
  EGL_OBJECT_DEFINE_CLASS_WITH_CODE (TN, t_n, /**/)

static inline gpointer
egl_object_new (const EglObjectClass * klass)
{
  return gst_vaapi_mini_object_new (GST_VAAPI_MINI_OBJECT_CLASS (klass));
}

static inline gpointer
egl_object_new0 (const EglObjectClass * klass)
{
  return gst_vaapi_mini_object_new0 (GST_VAAPI_MINI_OBJECT_CLASS (klass));
}

typedef struct egl_object_class_s EglMessageClass;
typedef struct egl_object_class_s EglVTableClass;
typedef struct egl_object_class_s EglDisplayClass;
typedef struct egl_object_class_s EglConfigClass;
typedef struct egl_object_class_s EglContextClass;
typedef struct egl_object_class_s EglSurfaceClass;
typedef struct egl_object_class_s EglProgramClass;
typedef struct egl_object_class_s EglWindowClass;

EGL_OBJECT_DEFINE_CLASS (EglMessage, egl_message);
EGL_OBJECT_DEFINE_CLASS (EglVTable, egl_vtable);
EGL_OBJECT_DEFINE_CLASS (EglDisplay, egl_display);
EGL_OBJECT_DEFINE_CLASS (EglConfig, egl_config);
EGL_OBJECT_DEFINE_CLASS (EglContext, egl_context);
EGL_OBJECT_DEFINE_CLASS (EglSurface, egl_surface);
EGL_OBJECT_DEFINE_CLASS (EglProgram, egl_program);
EGL_OBJECT_DEFINE_CLASS (EglWindow, egl_window);

/* ------------------------------------------------------------------------- */
// Desktop OpenGL and OpenGL|ES dispatcher (vtable)

static GMutex gl_vtables_lock;
static EglVTable *gl_vtables[4];

#if (USE_GLES_VERSION_MASK & (1U << 0))
static const gchar *gl_library_names[] = {
  "libGL.la",
  "libGL.so.1",
  NULL
};
#endif

#if (USE_GLES_VERSION_MASK & (1U << 1))
static const gchar *gles1_library_names[] = {
  "libGLESv1_CM.la",
  "libGLESv1_CM.so.1",
  NULL
};
#endif

#if (USE_GLES_VERSION_MASK & (1U << 2))
static const gchar *gles2_library_names[] = {
  "libGLESv2.la",
  "libGLESv2.so.2",
  NULL
};
#endif

static const gchar **gl_library_names_group[] = {
#if (USE_GLES_VERSION_MASK & (1U << 0))
  gl_library_names,
#endif
  NULL
};

static const gchar **gles1_library_names_group[] = {
#if (USE_GLES_VERSION_MASK & (1U << 1))
  gles1_library_names,
#endif
  NULL
};

static const gchar **gles2_library_names_group[] = {
#if (USE_GLES_VERSION_MASK & (1U << 2))
  gles2_library_names,
#endif
  NULL
};

static const gchar **gles3_library_names_group[] = {
#if (USE_GLES_VERSION_MASK & (1U << 3))
  gles2_library_names,
#endif
  NULL
};

static const gchar ***
egl_vtable_get_library_names_group (guint gles_version)
{
  const gchar ***library_names_group;

  switch (gles_version) {
    case 0:
      library_names_group = gl_library_names_group;
      break;
    case 1:
      library_names_group = gles1_library_names_group;
      break;
    case 2:
      library_names_group = gles2_library_names_group;
      break;
    case 3:
      library_names_group = gles3_library_names_group;
      break;
    default:
      library_names_group = NULL;
      break;
  }
  return library_names_group;
}

static gboolean
egl_vtable_check_extension (EglVTable * vtable, EGLDisplay display,
    gboolean is_egl, const gchar * group_name, guint * group_ptr)
{
  gchar ***extensions_list;
  const gchar *extensions;

  g_return_val_if_fail (group_name != NULL, FALSE);
  g_return_val_if_fail (group_ptr != NULL, FALSE);

  if (*group_ptr > 0)
    return TRUE;

  GST_DEBUG ("check for %s extension %s", is_egl ? "EGL" : "GL", group_name);

  if (is_egl) {
    if (!vtable->egl_extensions) {
      extensions = eglQueryString (display, EGL_EXTENSIONS);
      if (!extensions)
        return FALSE;
      GST_DEBUG ("EGL extensions: %s", extensions);
      vtable->egl_extensions = g_strsplit (extensions, " ", 0);
    }
    extensions_list = &vtable->egl_extensions;
  } else {
    if (!vtable->gl_extensions) {
      extensions = (const gchar *) vtable->glGetString (GL_EXTENSIONS);
      if (!extensions)
        return FALSE;
      GST_DEBUG ("GL extensions: %s", extensions);
      vtable->gl_extensions = g_strsplit (extensions, " ", 0);
    }
    extensions_list = &vtable->gl_extensions;
  }
  if (!g_strv_match_string (*extensions_list, group_name))
    return FALSE;

  GST_LOG ("  found %s extension %s", is_egl ? "EGL" : "GL", group_name);
  (*group_ptr)++;
  return TRUE;
}

static gboolean
egl_vtable_load_symbol (EglVTable * vtable, EGLDisplay display, gboolean is_egl,
    const gchar * symbol_name, gpointer * symbol_ptr,
    const gchar * group_name, guint * group_ptr)
{
  void (*symbol) (void);

  if (group_ptr && !*group_ptr) {
    if (!egl_vtable_check_extension (vtable, display, is_egl, group_name,
            group_ptr))
      return FALSE;
  }

  if (is_egl) {
    symbol = eglGetProcAddress (symbol_name);
  } else {
    if (!g_module_symbol (vtable->base.handle.p, symbol_name,
            (gpointer *) & symbol))
      return FALSE;
  }
  if (!symbol)
    return FALSE;

  GST_LOG ("  found symbol %s", symbol_name);
  if (symbol_ptr)
    *symbol_ptr = symbol;
  if (group_ptr)
    (*group_ptr)++;
  return TRUE;
}

static gboolean
egl_vtable_load_egl_symbols (EglVTable * vtable, EGLDisplay display)
{
  guint n = 0;

#define EGL_DEFINE_EXTENSION(NAME) do {                         \
      egl_vtable_check_extension (vtable, display, TRUE,        \
          "EGL_" G_STRINGIFY (NAME),                            \
          &vtable->GL_PROTO_GEN_CONCAT(has_EGL_,NAME));         \
    } while (0);

#define EGL_PROTO_BEGIN(NAME, TYPE, EXTENSION) do {             \
      n += egl_vtable_load_symbol (vtable, display, TRUE,       \
          G_STRINGIFY(GL_PROTO_GEN_CONCAT(egl,NAME)),           \
          (gpointer *) &vtable->GL_PROTO_GEN_CONCAT(egl,NAME),  \
          "EGL_" G_STRINGIFY(EXTENSION),                        \
          &vtable->GL_PROTO_GEN_CONCAT(has_EGL_,EXTENSION));    \
    } while (0);

#include "egl_vtable.h"

  vtable->num_egl_symbols = n;
  return TRUE;
}

static gboolean
egl_vtable_load_gl_symbols (EglVTable * vtable, EGLDisplay display)
{
  guint n = 0;

  vtable->has_GL_CORE_1_0 = 1;
  vtable->has_GL_CORE_1_1 = 1;
  vtable->has_GL_CORE_1_3 = 1;
  vtable->has_GL_CORE_2_0 = 1;

#define GL_DEFINE_EXTENSION(NAME) do {                          \
      egl_vtable_check_extension (vtable, display, FALSE,       \
          "GL_" G_STRINGIFY (NAME),                             \
          &vtable->GL_PROTO_GEN_CONCAT(has_GL_,NAME));          \
    } while (0);

#define GL_PROTO_BEGIN(NAME, TYPE, EXTENSION) do {              \
      n += egl_vtable_load_symbol (vtable, display, FALSE,      \
          G_STRINGIFY(GL_PROTO_GEN_CONCAT(gl,NAME)),            \
          (gpointer *) &vtable->GL_PROTO_GEN_CONCAT(gl,NAME),   \
          "GL_" G_STRINGIFY(EXTENSION),                         \
          &vtable->GL_PROTO_GEN_CONCAT(has_GL_,EXTENSION));     \
    } while (0);

#include "egl_vtable.h"

  --vtable->has_GL_CORE_1_0;
  --vtable->has_GL_CORE_1_1;
  --vtable->has_GL_CORE_1_3;
  --vtable->has_GL_CORE_2_0;

  vtable->num_gl_symbols = n;
  return TRUE;
}

static gboolean
egl_vtable_try_load_library (EglVTable * vtable, const gchar * name)
{
  if (vtable->base.handle.p)
    g_module_close (vtable->base.handle.p);
  vtable->base.handle.p = g_module_open (name,
      G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
  if (!vtable->base.handle.p)
    return FALSE;

  GST_DEBUG ("loaded backend: %s", g_module_name (vtable->base.handle.p));
  return TRUE;
}

static gboolean
egl_vtable_find_library (EglVTable * vtable)
{
  const gchar ***library_names_ptr =
      egl_vtable_get_library_names_group (vtable->gles_version);

  if (!library_names_ptr)
    return FALSE;

  for (; *library_names_ptr != NULL; library_names_ptr++) {
    const gchar **library_name_ptr = *library_names_ptr;
    for (; *library_name_ptr != NULL; library_name_ptr++) {
      if (egl_vtable_try_load_library (vtable, *library_name_ptr))
        return TRUE;
    }
  }
  return FALSE;
}

static gboolean
egl_vtable_init (EglVTable * vtable, EGLDisplay display, guint gles_version)
{
  GST_DEBUG ("initialize for OpenGL|ES API version %d", gles_version);

  vtable->gles_version = gles_version;
  if (!egl_vtable_find_library (vtable))
    return FALSE;
  if (!egl_vtable_load_egl_symbols (vtable, display))
    return FALSE;
  return TRUE;
}

static void
egl_vtable_finalize (EglVTable * vtable)
{
  g_strfreev (vtable->egl_extensions);
  g_strfreev (vtable->gl_extensions);
  if (vtable->base.handle.p)
    g_module_close (vtable->base.handle.p);

  if (vtable->base.is_wrapped) {
    g_mutex_lock (&gl_vtables_lock);
    gl_vtables[vtable->gles_version] = NULL;
    g_mutex_unlock (&gl_vtables_lock);
  }
}

static EglVTable *
egl_vtable_new (EglDisplay * display, guint gles_version)
{
  EglVTable *vtable;

  g_return_val_if_fail (display != NULL, NULL);

  vtable = egl_object_new0 (egl_vtable_class ());
  if (!vtable
      || !egl_vtable_init (vtable, display->base.handle.p, gles_version))
    goto error;
  return vtable;

  /* ERRORS */
error:
  {
    egl_object_replace (&vtable, NULL);
    return NULL;
  }
}

static EglVTable *
egl_vtable_new_cached (EglDisplay * display, guint gles_version)
{
  EglVTable *vtable, **vtable_ptr;

  g_return_val_if_fail (gles_version < G_N_ELEMENTS (gl_vtables), NULL);

  vtable_ptr = &gl_vtables[gles_version];

  g_mutex_lock (&gl_vtables_lock);
  vtable = *vtable_ptr;
  if (vtable)
    egl_object_ref (vtable);
  else {
    vtable = egl_vtable_new (display, gles_version);
    if (vtable) {
      vtable->base.is_wrapped = TRUE;
      *vtable_ptr = vtable;
    }
  }
  g_mutex_unlock (&gl_vtables_lock);
  return vtable;
}

/* ------------------------------------------------------------------------- */
// EGL Display

static gboolean
egl_display_run (EglDisplay * display, EglContextRunFunc func, gpointer args)
{
  EglMessage *msg;

  if (display->gl_thread == g_thread_self ()) {
    func (args);
    return TRUE;
  }

  msg = egl_object_new0 (egl_message_class ());
  if (!msg)
    return FALSE;

  msg->base.is_valid = TRUE;
  msg->func = func;
  msg->args = args;
  g_async_queue_push (display->gl_queue, egl_object_ref (msg));

  g_mutex_lock (&display->mutex);
  while (msg->base.is_valid)
    g_cond_wait (&display->gl_thread_ready, &display->mutex);
  g_mutex_unlock (&display->mutex);
  egl_object_unref (msg);
  return TRUE;
}

static gpointer
egl_get_display_from_native (guintptr native_display, guint gl_platform)
{
#if USE_GST_GL_HELPERS && GST_GL_HAVE_PLATFORM_EGL
  EGLDisplay ret;
  GstGLDisplayType display_type = GST_GL_DISPLAY_TYPE_ANY;

  switch (gl_platform) {
    case EGL_PLATFORM_X11:
      display_type = GST_GL_DISPLAY_TYPE_X11;
      break;
    case EGL_PLATFORM_WAYLAND:
      display_type = GST_GL_DISPLAY_TYPE_WAYLAND;
      break;
    default:
      break;
  }

  ret = gst_gl_display_egl_get_from_native (display_type, native_display);
  if (ret != EGL_NO_DISPLAY)
    return ret;
#endif
  return eglGetDisplay ((EGLNativeDisplayType) native_display);
}

static gpointer
egl_display_thread (gpointer data)
{
  EglDisplay *const display = data;
  EGLDisplay gl_display = display->base.handle.p;
  EGLint major_version, minor_version;
  gchar **gl_apis, **gl_api;

  g_mutex_lock (&display->mutex);
  if (!display->base.is_wrapped) {
    gl_display = display->base.handle.p =
        egl_get_display_from_native (display->base.handle.u,
        display->gl_platform);
    if (!gl_display)
      goto error;
    if (!eglInitialize (gl_display, &major_version, &minor_version))
      goto error;
  }

  display->gl_vendor_string =
      g_strdup (eglQueryString (gl_display, EGL_VENDOR));
  display->gl_version_string =
      g_strdup (eglQueryString (gl_display, EGL_VERSION));
  display->gl_apis_string =
      g_strdup (eglQueryString (gl_display, EGL_CLIENT_APIS));

  GST_INFO ("EGL vendor: %s", display->gl_vendor_string);
  GST_INFO ("EGL version: %s", display->gl_version_string);
  GST_INFO ("EGL client APIs: %s", display->gl_apis_string);

  gl_apis = g_strsplit (display->gl_apis_string, " ", 0);
  if (!gl_apis)
    goto error;
  for (gl_api = gl_apis; *gl_api != NULL; gl_api++) {
    const GlVersionInfo *const vinfo =
        gl_version_info_lookup_by_api_name (*gl_api);

    if (vinfo)
      display->gl_apis |= vinfo->gl_api_bit;
  }
  g_strfreev (gl_apis);
  if (!display->gl_apis)
    goto error;

  display->base.is_valid = TRUE;
  display->created = TRUE;
  g_cond_broadcast (&display->gl_thread_ready);
  g_mutex_unlock (&display->mutex);

  while (!g_atomic_int_get (&display->gl_thread_cancel)) {
    EglMessage *const msg =
        g_async_queue_timeout_pop (display->gl_queue, 100000);

    if (msg) {
      if (msg->base.is_valid) {
        msg->func (msg->args);
        msg->base.is_valid = FALSE;
        g_cond_broadcast (&display->gl_thread_ready);
      }
      egl_object_unref (msg);
    }
  }
  g_mutex_lock (&display->mutex);

done:
  if (gl_display != EGL_NO_DISPLAY && !display->base.is_wrapped)
    eglTerminate (gl_display);
  display->base.handle.p = NULL;
  g_cond_broadcast (&display->gl_thread_ready);
  g_mutex_unlock (&display->mutex);
  return NULL;

  /* ERRORS */
error:
  {
    display->created = TRUE;
    display->base.is_valid = FALSE;
    goto done;
  }
}

static gboolean
egl_display_init (EglDisplay * display)
{
  display->gl_queue =
      g_async_queue_new_full ((GDestroyNotify) gst_vaapi_mini_object_unref);
  if (!display->gl_queue)
    return FALSE;

  g_mutex_init (&display->mutex);
  g_cond_init (&display->gl_thread_ready);
  display->gl_thread = g_thread_try_new ("OpenGL Thread", egl_display_thread,
      display, NULL);
  if (!display->gl_thread)
    return FALSE;

  g_mutex_lock (&display->mutex);
  while (!display->created)
    g_cond_wait (&display->gl_thread_ready, &display->mutex);
  g_mutex_unlock (&display->mutex);
  return display->base.is_valid;
}

static void
egl_display_finalize (EglDisplay * display)
{
  g_atomic_int_set (&display->gl_thread_cancel, TRUE);
  g_thread_join (display->gl_thread);
  g_cond_clear (&display->gl_thread_ready);
  g_mutex_clear (&display->mutex);
  g_async_queue_unref (display->gl_queue);

  g_free (display->gl_vendor_string);
  g_free (display->gl_version_string);
  g_free (display->gl_apis_string);
}

static EglDisplay *
egl_display_new_full (gpointer handle, gboolean is_wrapped, guint platform)
{
  EglDisplay *display;

  display = egl_object_new0 (egl_display_class ());
  if (!display)
    return NULL;

  display->base.handle.p = handle;
  display->base.is_wrapped = is_wrapped;
  display->gl_platform = platform;
  if (!egl_display_init (display))
    goto error;
  return display;

  /* ERRORS */
error:
  {
    egl_object_unref (display);
    return NULL;
  }
}

EglDisplay *
egl_display_new (gpointer native_display, guint platform)
{
  g_return_val_if_fail (native_display != NULL, NULL);

  return egl_display_new_full (native_display, FALSE, platform);
}

EglDisplay *
egl_display_new_wrapped (EGLDisplay gl_display)
{
  g_return_val_if_fail (gl_display != EGL_NO_DISPLAY, NULL);

  return egl_display_new_full (gl_display, TRUE, EGL_PLATFORM_UNKNOWN);
}

/* ------------------------------------------------------------------------- */
// EGL Config

static gboolean
egl_config_init (EglConfig * config, EglDisplay * display,
    const EGLint * attribs)
{
  EGLDisplay const gl_display = display->base.handle.p;
  const GlVersionInfo *vinfo;
  EGLConfig gl_config;
  EGLint v, gl_apis, num_configs;

  egl_object_replace (&config->display, display);

  if (!eglChooseConfig (gl_display, attribs, &gl_config, 1, &num_configs))
    return FALSE;
  if (num_configs != 1)
    return FALSE;
  config->base.handle.p = gl_config;

  if (!eglGetConfigAttrib (gl_display, gl_config, EGL_CONFIG_ID, &v))
    return FALSE;
  config->config_id = v;

  if (!eglGetConfigAttrib (gl_display, gl_config, EGL_NATIVE_VISUAL_ID, &v))
    return FALSE;
  config->visual_id = v;

  if (!eglGetConfigAttrib (gl_display, gl_config, EGL_RENDERABLE_TYPE, &v))
    return FALSE;
  if (!egl_find_attrib_value (attribs, EGL_RENDERABLE_TYPE, &gl_apis))
    return FALSE;
  vinfo = gl_version_info_lookup_by_api (v & gl_apis);
  if (!vinfo)
    return FALSE;
  config->gles_version = vinfo->gles_version;
  config->gl_api = vinfo->gles_version > 0 ? EGL_OPENGL_ES_API : EGL_OPENGL_API;
  return TRUE;
}

static void
egl_config_finalize (EglConfig * config)
{
  egl_object_replace (&config->display, NULL);
}

EglConfig *
egl_config_new (EglDisplay * display, guint gles_version, GstVideoFormat format)
{
  EGLint attribs[2 * 6 + 1], *attrib = attribs;
  const GstVideoFormatInfo *finfo;
  const GlVersionInfo *vinfo;

  g_return_val_if_fail (display != NULL, NULL);

  finfo = gst_video_format_get_info (format);
  if (!finfo || !GST_VIDEO_FORMAT_INFO_IS_RGB (finfo))
    return NULL;

  vinfo = gl_version_info_lookup (gles_version);
  if (!vinfo)
    return NULL;

  *attrib++ = EGL_COLOR_BUFFER_TYPE;
  *attrib++ = EGL_RGB_BUFFER;
  *attrib++ = EGL_RED_SIZE;
  *attrib++ = GST_VIDEO_FORMAT_INFO_DEPTH (finfo, GST_VIDEO_COMP_R);
  *attrib++ = EGL_GREEN_SIZE;
  *attrib++ = GST_VIDEO_FORMAT_INFO_DEPTH (finfo, GST_VIDEO_COMP_G);
  *attrib++ = EGL_BLUE_SIZE;
  *attrib++ = GST_VIDEO_FORMAT_INFO_DEPTH (finfo, GST_VIDEO_COMP_B);
  *attrib++ = EGL_ALPHA_SIZE;
  *attrib++ = GST_VIDEO_FORMAT_INFO_DEPTH (finfo, GST_VIDEO_COMP_A);
  *attrib++ = EGL_RENDERABLE_TYPE;
  *attrib++ = vinfo->gl_api_bit;
  *attrib++ = EGL_NONE;
  g_assert (attrib - attribs <= G_N_ELEMENTS (attribs));

  return egl_config_new_with_attribs (display, attribs);
}

EglConfig *
egl_config_new_with_attribs (EglDisplay * display, const EGLint * attribs)
{
  EglConfig *config;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (attribs != NULL, NULL);

  config = egl_object_new0 (egl_config_class ());
  if (!config || !egl_config_init (config, display, attribs))
    goto error;
  return config;

  /* ERRORS */
error:
  {
    egl_object_replace (&config, NULL);
    return NULL;
  }
}

static EglConfig *
egl_config_new_from_gl_context (EglDisplay * display, EGLContext gl_context)
{
  EGLDisplay const gl_display = display->base.handle.p;
  EGLint attribs[3 * 2 + 1], *attrib = attribs;
  EGLint config_id, api, v;
  guint gles_version;
  const GlVersionInfo *vinfo;

  if (!eglQueryContext (gl_display, gl_context, EGL_CONFIG_ID, &config_id))
    return NULL;
  if (!eglQueryContext (gl_display, gl_context, EGL_CONTEXT_CLIENT_TYPE, &api))
    return NULL;
  if (!eglQueryContext (gl_display, gl_context, EGL_CONTEXT_CLIENT_VERSION, &v))
    return NULL;

  if (api == EGL_OPENGL_API)
    gles_version = 0;
  else if (api == EGL_OPENGL_ES_API)
    gles_version = v;
  else {
    GST_ERROR ("unsupported EGL client API (%d)", api);
    return NULL;
  }

  vinfo = gl_version_info_lookup (gles_version);
  if (!vinfo)
    return NULL;

  *attrib++ = EGL_COLOR_BUFFER_TYPE;
  *attrib++ = EGL_RGB_BUFFER;
  *attrib++ = EGL_CONFIG_ID;
  *attrib++ = config_id;
  *attrib++ = EGL_RENDERABLE_TYPE;
  *attrib++ = vinfo->gl_api_bit;
  *attrib++ = EGL_NONE;
  g_assert (attrib - attribs <= G_N_ELEMENTS (attribs));

  return egl_config_new_with_attribs (display, attribs);
}

/* ------------------------------------------------------------------------- */
// EGL Surface

static void
egl_surface_finalize (EglSurface * surface)
{
  if (surface->base.handle.p != EGL_NO_SURFACE && !surface->base.is_wrapped)
    eglDestroySurface (surface->display->base.handle.p, surface->base.handle.p);
  egl_object_replace (&surface->display, NULL);
}

static EglSurface *
egl_surface_new_wrapped (EglDisplay * display, EGLSurface gl_surface)
{
  EglSurface *surface;

  g_return_val_if_fail (display != NULL, NULL);

  surface = egl_object_new (egl_surface_class ());
  if (!surface)
    return NULL;

  surface->base.is_wrapped = TRUE;
  surface->base.handle.p = gl_surface;
  surface->display = egl_object_ref (display);
  return surface;
}

/* ------------------------------------------------------------------------- */
// EGL Context

static void egl_context_state_get_current (EglContextState * cs);

static gboolean
egl_context_state_set_current (EglContextState * new_cs,
    EglContextState * old_cs);

static gboolean
ensure_vtable (EglContext * ctx)
{
  if (!ctx->vtable) {
    ctx->vtable = egl_vtable_new_cached (ctx->display,
        ctx->config ? ctx->config->gles_version : 0);
    if (!ctx->vtable)
      return FALSE;
  }
  return TRUE;
}

static gboolean
ensure_context (EglContext * ctx, EGLContext gl_parent_context)
{
  EGLDisplay *const gl_display = ctx->display->base.handle.p;
  EGLContext gl_context = ctx->base.handle.p;
  EGLint gles_attribs[3], *attribs = NULL;

  if (!gl_context) {
    if (ctx->config->gles_version >= 2) {
      attribs = gles_attribs;
      attribs[0] = EGL_CONTEXT_CLIENT_VERSION;
      attribs[1] = ctx->config->gles_version;
      attribs[2] = EGL_NONE;
    }

    gl_context = eglCreateContext (gl_display, ctx->config->base.handle.p,
        gl_parent_context, attribs);
    if (!gl_context)
      goto error_create_context;
    ctx->base.handle.p = gl_context;
  }
  return TRUE;

  /* ERRORS */
error_create_context:
  GST_ERROR ("failed to create EGL context");
  return FALSE;
}

static inline gboolean
ensure_gl_is_surfaceless (EglContext * ctx)
{
  return ctx->vtable->has_EGL_KHR_surfaceless_context ||
      (ctx->read_surface && ctx->draw_surface);
}

static gboolean
ensure_gl_scene (EglContext * ctx)
{
  EglVTable *vtable;

  if (!ensure_gl_is_surfaceless (ctx))
    return FALSE;

  if (ctx->base.is_valid)
    return TRUE;

  vtable = egl_context_get_vtable (ctx, TRUE);
  if (!vtable)
    return FALSE;

  vtable->glClearColor (0.0, 0.0, 0.0, 1.0);
  if (ctx->config && ctx->config->gles_version == 0)
    vtable->glEnable (GL_TEXTURE_2D);
  vtable->glDisable (GL_BLEND);
  vtable->glDisable (GL_DEPTH_TEST);

  ctx->base.is_valid = TRUE;
  return TRUE;
}

/**
 * egl_context_state_get_current:
 * @cs: return location to the current #EglContextState
 *
 * Retrieves the current EGL context, display and surface to pack into
 * the #EglContextState struct.
 */
static void
egl_context_state_get_current (EglContextState * cs)
{
  cs->display = eglGetCurrentDisplay ();
  cs->context = eglGetCurrentContext ();
  if (cs->context) {
    cs->read_surface = eglGetCurrentSurface (EGL_READ);
    cs->draw_surface = eglGetCurrentSurface (EGL_DRAW);
  } else {
    cs->read_surface = EGL_NO_SURFACE;
    cs->draw_surface = EGL_NO_SURFACE;
  }
}

/**
 * egl_context_state_set_current:
 * @new_cs: the requested new #EglContextState
 * @old_cs: return location to the context that was previously current
 *
 * Makes the @new_cs EGL context the current EGL rendering context of
 * the calling thread, replacing the previously current context if
 * there was one.
 *
 * If @old_cs is non %NULL, the previously current EGL context and
 * surface are recorded.
 *
 * Return value: %TRUE on success
 */
static gboolean
egl_context_state_set_current (EglContextState * new_cs,
    EglContextState * old_cs)
{
  /* If display is NULL, this could be that new_cs was retrieved from
     egl_context_state_get_current() with none set previously. If that case,
     the other fields are also NULL and we don't return an error */
  if (!new_cs->display)
    return !new_cs->context && !new_cs->read_surface && !new_cs->draw_surface;

  if (old_cs) {
    if (old_cs == new_cs)
      return TRUE;
    egl_context_state_get_current (old_cs);
    if (old_cs->display == new_cs->display &&
        old_cs->context == new_cs->context &&
        old_cs->read_surface == new_cs->read_surface &&
        old_cs->draw_surface == new_cs->draw_surface)
      return TRUE;
  }
  return eglMakeCurrent (new_cs->display, new_cs->draw_surface,
      new_cs->read_surface, new_cs->context);
}

static gboolean
egl_context_init (EglContext * ctx, EglDisplay * display, EglConfig * config,
    EGLContext gl_parent_context)
{
  egl_object_replace (&ctx->display, display);
  egl_object_replace (&ctx->config, config);

  if (config)
    eglBindAPI (config->gl_api);

  if (!ensure_vtable (ctx))
    return FALSE;
  if (!ensure_context (ctx, gl_parent_context))
    return FALSE;
  return TRUE;
}

static void
egl_context_finalize (EglContext * ctx)
{
  if (ctx->base.handle.p && !ctx->base.is_wrapped)
    eglDestroyContext (ctx->display->base.handle.p, ctx->base.handle.p);
  egl_object_replace (&ctx->read_surface, NULL);
  egl_object_replace (&ctx->draw_surface, NULL);
  egl_object_replace (&ctx->config, NULL);
  egl_object_replace (&ctx->display, NULL);
  egl_object_replace (&ctx->vtable, NULL);
}

typedef struct
{
  EglDisplay *display;
  EglConfig *config;
  EGLContext gl_parent_context;
  EglContext *context;          /* result */
} CreateContextArgs;

static void
do_egl_context_new (CreateContextArgs * args)
{
  EglContext *ctx;

  ctx = egl_object_new0 (egl_context_class ());
  if (!ctx || !egl_context_init (ctx, args->display, args->config,
          args->gl_parent_context))
    goto error;
  args->context = ctx;
  return;

  /* ERRORS */
error:
  {
    egl_object_replace (&ctx, NULL);
    args->context = NULL;
  }
}

EglContext *
egl_context_new (EglDisplay * display, EglConfig * config, EglContext * parent)
{
  CreateContextArgs args;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (config != NULL, NULL);

  args.display = display;
  args.config = config;
  args.gl_parent_context = parent ? parent->base.handle.p : EGL_NO_CONTEXT;
  if (!egl_display_run (display, (EglContextRunFunc) do_egl_context_new, &args))
    return NULL;
  return args.context;
}

EglContext *
egl_context_new_wrapped (EglDisplay * display, EGLContext gl_context)
{
  CreateContextArgs args;
  EglConfig *config;
  gboolean success;

  g_return_val_if_fail (display != NULL, NULL);
  g_return_val_if_fail (gl_context != EGL_NO_CONTEXT, NULL);

  config = egl_config_new_from_gl_context (display, gl_context);
  if (!config)
    return NULL;

  args.display = display;
  args.config = config;
  args.gl_parent_context = gl_context;
  args.context = NULL;
  success = egl_display_run (display, (EglContextRunFunc) do_egl_context_new,
      &args);
  egl_object_unref (config);
  if (!success)
    return NULL;

  return args.context;
}

EglVTable *
egl_context_get_vtable (EglContext * ctx, gboolean need_gl_symbols)
{
  g_return_val_if_fail (ctx != NULL, NULL);
  g_return_val_if_fail (ctx->display->gl_thread == g_thread_self (), NULL);

  if (!ensure_vtable (ctx))
    return NULL;

  if (need_gl_symbols && !(ctx->vtable->num_gl_symbols > 0 ||
          egl_vtable_load_gl_symbols (ctx->vtable,
              ctx->display->base.handle.p)))
    return NULL;
  return ctx->vtable;
}

static void
egl_context_set_surface (EglContext * ctx, EglSurface * surface)
{
  g_return_if_fail (ctx != NULL);
  g_return_if_fail (surface != NULL);

  egl_object_replace (&ctx->read_surface, surface);
  egl_object_replace (&ctx->draw_surface, surface);
}

gboolean
egl_context_set_current (EglContext * ctx, gboolean activate,
    EglContextState * old_cs)
{
  EglContextState cs, *new_cs;

  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (ctx->display->gl_thread == g_thread_self (), FALSE);

  if (activate) {
    new_cs = &cs;
    new_cs->display = ctx->display->base.handle.p;
    new_cs->context = ctx->base.handle.p;
    new_cs->draw_surface = ctx->draw_surface ?
        ctx->draw_surface->base.handle.p : EGL_NO_SURFACE;
    new_cs->read_surface = ctx->read_surface ?
        ctx->read_surface->base.handle.p : EGL_NO_SURFACE;
  } else if (old_cs) {
    new_cs = old_cs;
    old_cs = NULL;
  } else {
    new_cs = &cs;
    new_cs->display = ctx->display->base.handle.p;
    new_cs->context = EGL_NO_CONTEXT;
    new_cs->draw_surface = EGL_NO_SURFACE;
    new_cs->read_surface = EGL_NO_SURFACE;
    old_cs = NULL;
  }

  if (!egl_context_state_set_current (new_cs, old_cs))
    return FALSE;
  if (activate && !ensure_gl_scene (ctx))
    return FALSE;
  return TRUE;
}

gboolean
egl_context_run (EglContext * ctx, EglContextRunFunc func, gpointer args)
{
  g_return_val_if_fail (ctx != NULL, FALSE);
  g_return_val_if_fail (func != NULL, FALSE);

  return egl_display_run (ctx->display, func, args);
}

/* ------------------------------------------------------------------------- */
// EGL Program

static GLuint
egl_compile_shader (EglContext * ctx, GLenum type, const char *source)
{
  EglVTable *const vtable = egl_context_get_vtable (ctx, TRUE);
  GLuint shader;
  GLint status;
  char log[BUFSIZ];
  GLsizei log_length;

  shader = vtable->glCreateShader (type);
  vtable->glShaderSource (shader, 1, &source, NULL);
  vtable->glCompileShader (shader);
  vtable->glGetShaderiv (shader, GL_COMPILE_STATUS, &status);
  if (!status) {
    GST_ERROR ("failed to compile %s shader",
        type == GL_FRAGMENT_SHADER ? "fragment" :
        type == GL_VERTEX_SHADER ? "vertex" : "<unknown>");

    vtable->glGetShaderInfoLog (shader, sizeof (log), &log_length, log);
    GST_ERROR ("info log: %s", log);
    return 0;
  }
  return shader;
}

static void
egl_program_finalize (EglProgram * program)
{
  EglVTable *const vtable = program->vtable;

  if (program->base.handle.u)
    vtable->glDeleteProgram (program->base.handle.u);
  if (program->frag_shader)
    vtable->glDeleteShader (program->frag_shader);
  if (program->vert_shader)
    vtable->glDeleteShader (program->vert_shader);
  egl_object_replace (&program->vtable, NULL);
}

static gboolean
egl_program_init (EglProgram * program, EglContext * ctx,
    const gchar * frag_shader_text, const gchar * vert_shader_text)
{
  EglVTable *const vtable = egl_context_get_vtable (ctx, TRUE);
  GLuint prog_id;
  char msg[BUFSIZ];
  GLsizei msglen;
  GLint status;

  if (ctx->config->gles_version == 1)
    goto error_unsupported_gles_version;

  program->vtable = egl_object_ref (vtable);

  program->frag_shader =
      egl_compile_shader (ctx, GL_FRAGMENT_SHADER, frag_shader_text);
  if (!program->frag_shader)
    return FALSE;

  program->vert_shader =
      egl_compile_shader (ctx, GL_VERTEX_SHADER, vert_shader_text);
  if (!program->vert_shader)
    return FALSE;

  prog_id = vtable->glCreateProgram ();
  if (!prog_id)
    return FALSE;
  program->base.handle.u = prog_id;

  vtable->glAttachShader (prog_id, program->frag_shader);
  vtable->glAttachShader (prog_id, program->vert_shader);
  vtable->glBindAttribLocation (prog_id, 0, "position");
  vtable->glBindAttribLocation (prog_id, 1, "texcoord");
  vtable->glLinkProgram (prog_id);

  vtable->glGetProgramiv (prog_id, GL_LINK_STATUS, &status);
  if (!status)
    goto error_link_program;
  return TRUE;

  /* ERRORS */
error_unsupported_gles_version:
  GST_ERROR ("unsupported shader with OpenGL|ES version 1");
  return FALSE;
error_link_program:
  vtable->glGetProgramInfoLog (prog_id, sizeof (msg), &msglen, msg);
  GST_ERROR ("failed to link program: %s", msg);
  return FALSE;
}

EglProgram *
egl_program_new (EglContext * ctx, const gchar * frag_shader_text,
    const gchar * vert_shader_text)
{
  EglProgram *program;

  g_return_val_if_fail (ctx != NULL, NULL);
  g_return_val_if_fail (frag_shader_text != NULL, NULL);
  g_return_val_if_fail (vert_shader_text != NULL, NULL);

  program = egl_object_new0 (egl_program_class ());
  if (!program
      || !egl_program_init (program, ctx, frag_shader_text, vert_shader_text))
    goto error;
  return program;

  /* ERRORS */
error:
  {
    egl_object_replace (&program, NULL);
    return NULL;
  }
}

/* ------------------------------------------------------------------------- */
// EGL Window

static gboolean
egl_window_init (EglWindow * window, EglContext * ctx, gpointer native_window)
{
  EGLSurface gl_surface;

  window->context = egl_context_new (ctx->display, ctx->config, ctx);
  if (!window->context)
    return FALSE;
  ctx = window->context;

  gl_surface = eglCreateWindowSurface (ctx->display->base.handle.p,
      ctx->config->base.handle.p, (EGLNativeWindowType) native_window, NULL);
  if (!gl_surface)
    return FALSE;

  window->surface = egl_surface_new_wrapped (ctx->display, gl_surface);
  if (!window->surface)
    goto error_create_surface;
  window->base.handle.p = gl_surface;
  window->base.is_wrapped = FALSE;

  egl_context_set_surface (ctx, window->surface);
  return TRUE;

  /* ERRORS */
error_create_surface:
  GST_ERROR ("failed to create EGL wrapper surface");
  eglDestroySurface (ctx->display->base.handle.p, gl_surface);
  return FALSE;
}

static void
egl_window_finalize (EglWindow * window)
{
  if (window->context && window->base.handle.p)
    eglDestroySurface (window->context->display->base.handle.p,
        window->base.handle.p);

  egl_object_replace (&window->surface, NULL);
  egl_object_replace (&window->context, NULL);
}

EglWindow *
egl_window_new (EglContext * ctx, gpointer native_window)
{
  EglWindow *window;

  g_return_val_if_fail (ctx != NULL, NULL);
  g_return_val_if_fail (native_window != NULL, NULL);

  window = egl_object_new0 (egl_window_class ());
  if (!window || !egl_window_init (window, ctx, native_window))
    goto error;
  return window;

  /* ERRORS */
error:
  {
    egl_object_replace (&window, NULL);
    return NULL;
  }
}

/* ------------------------------------------------------------------------- */
// Misc utility functions

void
egl_matrix_set_identity (gfloat m[16])
{
#define MAT(m,r,c) (m)[(c) * 4 + (r)]
  MAT (m, 0, 0) = 1.0;
  MAT (m, 0, 1) = 0.0;
  MAT (m, 0, 2) = 0.0;
  MAT (m, 0, 3) = 0.0;
  MAT (m, 1, 0) = 0.0;
  MAT (m, 1, 1) = 1.0;
  MAT (m, 1, 2) = 0.0;
  MAT (m, 1, 3) = 0.0;
  MAT (m, 2, 0) = 0.0;
  MAT (m, 2, 1) = 0.0;
  MAT (m, 2, 2) = 1.0;
  MAT (m, 2, 3) = 0.0;
  MAT (m, 3, 0) = 0.0;
  MAT (m, 3, 1) = 0.0;
  MAT (m, 3, 2) = 0.0;
  MAT (m, 3, 3) = 1.0;
#undef MAT
}

/**
 * egl_create_texture:
 * @ctx: the parent #EglContext object
 * @target: the target to which the texture is bound
 * @format: the format of the pixel data
 * @width: the requested width, in pixels
 * @height: the requested height, in pixels
 *
 * Creates a texture with the specified dimensions and @format. The
 * internal format will be automatically derived from @format.
 *
 * Return value: the newly created texture name
 */
guint
egl_create_texture (EglContext * ctx, guint target, guint format,
    guint width, guint height)
{
  EglVTable *const vtable = egl_context_get_vtable (ctx, TRUE);
  guint internal_format, texture, bytes_per_component;

  internal_format = format;
  switch (format) {
    case GL_LUMINANCE:
      bytes_per_component = 1;
      break;
    case GL_LUMINANCE_ALPHA:
      bytes_per_component = 2;
      break;
    case GL_RGBA:
    case GL_BGRA_EXT:
      internal_format = GL_RGBA;
      bytes_per_component = 4;
      break;
    default:
      bytes_per_component = 0;
      break;
  }
  g_assert (bytes_per_component > 0);

  vtable->glGenTextures (1, &texture);
  vtable->glBindTexture (target, texture);

  if (width > 0 && height > 0)
    vtable->glTexImage2D (target, 0, internal_format, width, height, 0,
        format, GL_UNSIGNED_BYTE, NULL);

  vtable->glTexParameteri (target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  vtable->glTexParameteri (target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  vtable->glTexParameteri (target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  vtable->glTexParameteri (target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  vtable->glPixelStorei (GL_UNPACK_ALIGNMENT, bytes_per_component);

  return texture;
}

/**
 * egl_destroy_texture:
 * @ctx: the parent #EglContext object
 * @texture: the texture name to delete
 *
 * Destroys the supplied @texture name.
 */
void
egl_destroy_texture (EglContext * ctx, guint texture)
{
  EglVTable *const vtable = egl_context_get_vtable (ctx, TRUE);

  vtable->glDeleteTextures (1, &texture);
}
