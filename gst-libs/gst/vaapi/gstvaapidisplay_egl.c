/*
 *  gstvaapidisplay_egl.c - VA/EGL display abstraction
 *
 *  Copyright (C) 2014 Intel Corporation
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@intel.com>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public License
 *  as published by the Free Software Foundation; either version 2.1
 *  of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free
 *  Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 *  Boston, MA 02110-1301 USA
 */

#include "sysdeps.h"
#include <gmodule.h>
#include "gstvaapidisplay_egl.h"
#include "gstvaapidisplay_egl_priv.h"
#include "gstvaapiwindow.h"
#include "gstvaapiwindow_egl.h"
#include "gstvaapiwindow_priv.h"
#include "gstvaapitexture_egl.h"

GST_DEBUG_CATEGORY (gst_debug_vaapidisplay_egl);

/* ------------------------------------------------------------------------- */
/* --- Display backend loader                                            --- */
/* ------------------------------------------------------------------------- */

typedef struct _GstVaapiDisplayLoader GstVaapiDisplayLoader;
typedef struct _GstVaapiDisplayLoaderInfo GstVaapiDisplayLoaderInfo;

typedef GstVaapiDisplay *(*GstVaapiDisplayCreateFunc) (const gchar * name);
typedef GstVaapiDisplay *(*GstVaapiDisplayCreateFromNativeFunc) (gpointer dpy);

struct _GstVaapiDisplayLoader
{
  GstVaapiMiniObject parent_instance;

  GModule *module;
  GPtrArray *module_names;
  GstVaapiDisplayCreateFunc create_display;
  GstVaapiDisplayCreateFromNativeFunc create_display_from_native;
};

struct _GstVaapiDisplayLoaderInfo
{
  const gchar *name;
  GstVaapiDisplayType type;
  const gchar *create_display;
  const gchar *create_display_from_native;
};

static GMutex g_loader_lock;
static GstVaapiDisplayLoader *g_loader;

/* *INDENT-OFF* */
static const GstVaapiDisplayLoaderInfo g_loader_info[] = {
#if USE_WAYLAND
  { "wayland",
    GST_VAAPI_DISPLAY_TYPE_WAYLAND,
    "gst_vaapi_display_wayland_new",
    "gst_vaapi_display_wayland_new_with_display",
  },
#endif
#if USE_X11
  { "x11",
    GST_VAAPI_DISPLAY_TYPE_X11,
    "gst_vaapi_display_x11_new",
    "gst_vaapi_display_x11_new_with_display",
  },
#endif
  {NULL,}
};
/* *INDENT-ON* */

static void
gst_vaapi_display_loader_finalize (GstVaapiDisplayLoader * loader)
{
  if (!loader)
    return;

  if (loader->module) {
    g_module_close (loader->module);
    loader->module = NULL;
  }

  if (loader->module_names) {
    g_ptr_array_unref (loader->module_names);
    loader->module_names = NULL;
  }
}

static inline const GstVaapiMiniObjectClass *
gst_vaapi_display_loader_class (void)
{
  static const GstVaapiMiniObjectClass g_class = {
    .size = sizeof (GstVaapiDisplayLoader),
    .finalize = (GDestroyNotify) gst_vaapi_display_loader_finalize,
  };
  return &g_class;
}

static inline GstVaapiDisplayLoader *
gst_vaapi_display_loader_new (void)
{
  return (GstVaapiDisplayLoader *)
      gst_vaapi_mini_object_new0 (gst_vaapi_display_loader_class ());
}

static gboolean
gst_vaapi_display_loader_reset_module_names (GstVaapiDisplayLoader * loader,
    const GstVaapiDisplayLoaderInfo * loader_info)
{
  gchar *module_name;

  if (loader->module_names)
    g_ptr_array_unref (loader->module_names);
  loader->module_names = g_ptr_array_new_full (3, (GDestroyNotify) g_free);
  if (!loader->module_names)
    return FALSE;

  module_name =
      g_strdup_printf ("libgstvaapi-%s-%s.la", loader_info->name,
      GST_API_VERSION_S);
  if (module_name)
    g_ptr_array_add (loader->module_names, module_name);

  module_name =
      g_strdup_printf ("libgstvaapi-%s-%s.so", loader_info->name,
      GST_API_VERSION_S);
  if (module_name)
    g_ptr_array_add (loader->module_names, module_name);

  module_name =
      g_strdup_printf ("libgstvaapi-%s-%s.so.%s", loader_info->name,
      GST_API_VERSION_S, GST_VAAPI_MAJOR_VERSION_S);
  if (module_name)
    g_ptr_array_add (loader->module_names, module_name);

  return loader->module_names->len > 0;
}

static gboolean
gst_vaapi_display_loader_try_load_module (GstVaapiDisplayLoader * loader,
    const GstVaapiDisplayLoaderInfo * loader_info)
{
  guint i;

  if (!gst_vaapi_display_loader_reset_module_names (loader, loader_info))
    return FALSE;

  if (loader->module) {
    g_module_close (loader->module);
    loader->module = NULL;
  }

  for (i = 0; i < loader->module_names->len; i++) {
    const gchar *const module_name =
        g_ptr_array_index (loader->module_names, i);

    loader->module = g_module_open (module_name,
        G_MODULE_BIND_LAZY | G_MODULE_BIND_LOCAL);
    if (loader->module)
      return TRUE;
  }
  return FALSE;
}

static gboolean
gst_vaapi_display_loader_try_load (GstVaapiDisplayLoader * loader,
    const GstVaapiDisplayLoaderInfo * loader_info)
{
  guint has_errors = 0;

  if (!gst_vaapi_display_loader_try_load_module (loader, loader_info))
    return FALSE;
  GST_DEBUG ("loaded backend: %s", g_module_name (loader->module));

  has_errors |= !g_module_symbol (loader->module,
      loader_info->create_display, (gpointer *) & loader->create_display);
  has_errors |= !g_module_symbol (loader->module,
      loader_info->create_display_from_native,
      (gpointer *) & loader->create_display_from_native);

  return has_errors == 0;
}

static GstVaapiDisplay *
gst_vaapi_display_loader_try_load_any (GstVaapiDisplayLoader * loader)
{
  GstVaapiDisplay *display;
  const GstVaapiDisplayLoaderInfo *loader_info;

  for (loader_info = g_loader_info; loader_info->name != NULL; loader_info++) {
    if (!gst_vaapi_display_loader_try_load (loader, loader_info))
      continue;

    display = loader->create_display (NULL);
    if (display) {
      GST_INFO ("selected backend: %s", loader_info->name);
      return display;
    }
  }
  return NULL;
}

#define gst_vaapi_display_loader_ref(loader) \
  ((GstVaapiDisplayLoader *) gst_vaapi_mini_object_ref (GST_VAAPI_MINI_OBJECT (loader)))
#define gst_vaapi_display_loader_unref(loader) \
  gst_vaapi_mini_object_unref (GST_VAAPI_MINI_OBJECT (loader))
#define gst_vaapi_display_loader_replace(old_loader_ptr, new_loader) \
  gst_vaapi_mini_object_replace ((GstVaapiMiniObject **)(old_loader_ptr), \
      GST_VAAPI_MINI_OBJECT (new_loader))

static GstVaapiDisplayLoader *
gst_vaapi_display_loader_acquire_global (void)
{
  GstVaapiDisplayLoader *loader;

  g_mutex_lock (&g_loader_lock);
  loader = g_loader ? gst_vaapi_display_loader_ref (g_loader) :
      gst_vaapi_display_loader_new ();
  g_loader = loader;
  g_mutex_unlock (&g_loader_lock);
  return loader;
}

static void
gst_vaapi_display_loader_release_global (void)
{
  g_mutex_lock (&g_loader_lock);
  gst_vaapi_display_loader_replace (&g_loader, NULL);
  g_mutex_unlock (&g_loader_lock);
}

static const GstVaapiDisplayLoaderInfo *
gst_vaapi_display_loader_map_lookup_type (GstVaapiDisplayType type)
{
  const GstVaapiDisplayLoaderInfo *loader_info;

  for (loader_info = g_loader_info; loader_info->name != NULL; loader_info++) {
    if (loader_info->type == type)
      return loader_info;
  }
  return NULL;
}

/* ------------------------------------------------------------------------- */
/* --- EGL backend implementation                                        --- */
/* ------------------------------------------------------------------------- */

static const guint g_display_types = 1U << GST_VAAPI_DISPLAY_TYPE_EGL;

typedef struct
{
  gpointer display;
  guint display_type;
  guint gles_version;
} InitParams;

static gboolean
reset_context (GstVaapiDisplayEGL * display, EGLContext gl_context)
{
  EglConfig *config;
  EglContext *ctx;

  egl_object_replace (&display->egl_context, NULL);

  if (gl_context != EGL_NO_CONTEXT)
    ctx = egl_context_new_wrapped (display->egl_display, gl_context);
  else {
    config = egl_config_new (display->egl_display, display->gles_version,
        GST_VIDEO_FORMAT_RGB);
    if (!config)
      return FALSE;

    ctx = egl_context_new (display->egl_display, config, NULL);
    egl_object_unref (config);
  }
  if (!ctx)
    return FALSE;

  egl_object_replace (&display->egl_context, ctx);
  egl_object_unref (ctx);
  return TRUE;
}

static inline gboolean
ensure_context (GstVaapiDisplayEGL * display)
{
  return display->egl_context || reset_context (display, EGL_NO_CONTEXT);
}

static inline gboolean
ensure_context_is_wrapped (GstVaapiDisplayEGL * display, EGLContext gl_context)
{
  return (display->egl_context &&
      display->egl_context->base.handle.p == gl_context) ||
      reset_context (display, gl_context);
}

static gboolean
gst_vaapi_display_egl_bind_display (GstVaapiDisplayEGL * display,
    const InitParams * params)
{
  GstVaapiDisplay *native_display;
  GstVaapiDisplayLoader *loader;
  const GstVaapiDisplayLoaderInfo *loader_info;
  EglDisplay *egl_display;

  loader = gst_vaapi_display_loader_acquire_global ();
  if (params->display) {
    loader_info =
        gst_vaapi_display_loader_map_lookup_type (params->display_type);
    if (!loader_info)
      goto error_unsupported_display_type;

    loader = gst_vaapi_display_loader_new ();
    if (!loader || !gst_vaapi_display_loader_try_load (loader, loader_info))
      goto error_init_loader;

    native_display = loader->create_display_from_native (params->display);
  } else {
    gst_vaapi_display_loader_ref (loader);
    native_display = gst_vaapi_display_loader_try_load_any (loader);
  }
  gst_vaapi_display_loader_replace (&display->loader, loader);
  gst_vaapi_display_loader_unref (loader);
  if (!native_display)
    return FALSE;

  gst_vaapi_display_replace (&display->display, native_display);
  gst_vaapi_display_unref (native_display);

  egl_display = egl_display_new (GST_VAAPI_DISPLAY_NATIVE (display->display));
  if (!egl_display)
    return FALSE;

  egl_object_replace (&display->egl_display, egl_display);
  egl_object_unref (egl_display);
  display->gles_version = params->gles_version;
  return TRUE;

  /* ERRORS */
error_unsupported_display_type:
  GST_ERROR ("unsupported display type (%d)", params->display_type);
  return FALSE;
error_init_loader:
  GST_ERROR ("failed to initialize display backend loader");
  gst_vaapi_display_loader_replace (&loader, NULL);
  return FALSE;
}

static void
gst_vaapi_display_egl_close_display (GstVaapiDisplayEGL * display)
{
  gst_vaapi_display_replace (&display->display, NULL);
  gst_vaapi_display_loader_replace (&display->loader, NULL);
  gst_vaapi_display_loader_release_global ();
}

static void
gst_vaapi_display_egl_lock (GstVaapiDisplayEGL * display)
{
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->lock)
    klass->lock (display->display);
}

static void
gst_vaapi_display_egl_unlock (GstVaapiDisplayEGL * display)
{
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->unlock)
    klass->unlock (display->display);
}

static void
gst_vaapi_display_egl_sync (GstVaapiDisplayEGL * display)
{
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->sync)
    klass->sync (display->display);
  else if (klass->flush)
    klass->flush (display->display);
}

static void
gst_vaapi_display_egl_flush (GstVaapiDisplayEGL * display)
{
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->flush)
    klass->flush (display->display);
}

static gboolean
gst_vaapi_display_egl_get_display_info (GstVaapiDisplayEGL * display,
    GstVaapiDisplayInfo * info)
{
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->get_display && !klass->get_display (display->display, info))
    return FALSE;
  return TRUE;
}

static void
gst_vaapi_display_egl_get_size (GstVaapiDisplayEGL * display,
    guint * width_ptr, guint * height_ptr)
{
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->get_size)
    klass->get_size (display->display, width_ptr, height_ptr);
}

static void
gst_vaapi_display_egl_get_size_mm (GstVaapiDisplayEGL * display,
    guint * width_ptr, guint * height_ptr)
{
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->get_size_mm)
    klass->get_size_mm (display->display, width_ptr, height_ptr);
}

static guintptr
gst_vaapi_display_egl_get_visual_id (GstVaapiDisplayEGL * display,
    GstVaapiWindow * window)
{
  if (!ensure_context (display))
    return 0;
  return display->egl_context->config->visual_id;
}

static GstVaapiWindow *
gst_vaapi_display_egl_create_window (GstVaapiDisplay * display, GstVaapiID id,
    guint width, guint height)
{
  if (id != GST_VAAPI_ID_INVALID)
    return NULL;
  return gst_vaapi_window_egl_new (display, width, height);
}

static GstVaapiTexture *
gst_vaapi_display_egl_create_texture (GstVaapiDisplay * display, GstVaapiID id,
    guint target, guint format, guint width, guint height)
{
  return id != GST_VAAPI_ID_INVALID ?
      gst_vaapi_texture_egl_new_wrapped (display, id, target, format,
          width, height) :
      gst_vaapi_texture_egl_new (display, target, format, width, height);
}

static void
gst_vaapi_display_egl_class_init (GstVaapiDisplayEGLClass * klass)
{
  GstVaapiMiniObjectClass *const object_class =
      GST_VAAPI_MINI_OBJECT_CLASS (klass);
  GstVaapiDisplayClass *const dpy_class = GST_VAAPI_DISPLAY_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_debug_vaapidisplay_egl, "vaapidisplay_egl", 0,
      "VA/EGL backend");

  gst_vaapi_display_class_init (dpy_class);

  object_class->size = sizeof (GstVaapiDisplayEGL);
  dpy_class->display_type = GST_VAAPI_DISPLAY_TYPE_EGL;
  dpy_class->bind_display = (GstVaapiDisplayBindFunc)
      gst_vaapi_display_egl_bind_display;
  dpy_class->close_display = (GstVaapiDisplayCloseFunc)
      gst_vaapi_display_egl_close_display;
  dpy_class->lock = (GstVaapiDisplayLockFunc)
      gst_vaapi_display_egl_lock;
  dpy_class->unlock = (GstVaapiDisplayUnlockFunc)
      gst_vaapi_display_egl_unlock;
  dpy_class->sync = (GstVaapiDisplaySyncFunc)
      gst_vaapi_display_egl_sync;
  dpy_class->flush = (GstVaapiDisplayFlushFunc)
      gst_vaapi_display_egl_flush;
  dpy_class->get_display = (GstVaapiDisplayGetInfoFunc)
      gst_vaapi_display_egl_get_display_info;
  dpy_class->get_size = (GstVaapiDisplayGetSizeFunc)
      gst_vaapi_display_egl_get_size;
  dpy_class->get_size_mm = (GstVaapiDisplayGetSizeMFunc)
      gst_vaapi_display_egl_get_size_mm;
  dpy_class->get_visual_id = (GstVaapiDisplayGetVisualIdFunc)
      gst_vaapi_display_egl_get_visual_id;
  dpy_class->create_window = (GstVaapiDisplayCreateWindowFunc)
      gst_vaapi_display_egl_create_window;
  dpy_class->create_texture = (GstVaapiDisplayCreateTextureFunc)
      gst_vaapi_display_egl_create_texture;
}

static inline const GstVaapiDisplayClass *
gst_vaapi_display_egl_class (void)
{
  static GstVaapiDisplayEGLClass g_class;
  static gsize g_class_init = FALSE;

  if (g_once_init_enter (&g_class_init)) {
    gst_vaapi_display_egl_class_init (&g_class);
    g_once_init_leave (&g_class_init, TRUE);
  }
  return GST_VAAPI_DISPLAY_CLASS (&g_class);
}

/**
 * gst_vaapi_display_egl_new:
 * @display: a #GstVaapiDisplay, or %NULL to pick any one
 * @gles_version: the OpenGL ES version API to use
 *
 * Creates a new #GstVaapiDisplay object suitable in EGL context. If
 * the native @display is %NULL, then any type of display is picked,
 * i.e. one that can be successfully opened. The @gles_version will
 * further ensure the OpenGL ES API to use, or zero to indicate
 * "desktop" OpenGL.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_egl_new (GstVaapiDisplay * display, guint gles_version)
{
  InitParams params;

  if (display) {
    params.display = GST_VAAPI_DISPLAY_NATIVE (display);
    params.display_type = GST_VAAPI_DISPLAY_VADISPLAY_TYPE (display);
  } else {
    params.display = NULL;
    params.display_type = GST_VAAPI_DISPLAY_TYPE_ANY;
  }
  params.gles_version = gles_version;
  return gst_vaapi_display_new (gst_vaapi_display_egl_class (),
      GST_VAAPI_DISPLAY_INIT_FROM_NATIVE_DISPLAY, &params);
}

/**
 * gst_vaapi_display_egl_new_with_native_display:
 * @native_display: an EGLDisplay object
 * @display_type: the display type of @native_display
 * @gles_version: the OpenGL ES version API to use
 *
 * Creates a #GstVaapiDisplay based on the native display supplied in
 * as @native_display. The caller still owns the display and must call
 * native display close function when all #GstVaapiDisplay references
 * are released. Doing so too early can yield undefined behaviour.
 *
 * The @gles_version will further ensure the OpenGL ES API to use, or
 * zero to indicate "desktop" OpenGL.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_egl_new_with_native_display (gpointer native_display,
    GstVaapiDisplayType display_type, guint gles_version)
{
  InitParams params;

  g_return_val_if_fail (native_display != NULL, NULL);

  params.display = native_display;
  params.display_type = display_type;
  params.gles_version = gles_version;
  return gst_vaapi_display_new (gst_vaapi_display_egl_class (),
      GST_VAAPI_DISPLAY_INIT_FROM_NATIVE_DISPLAY, &params);
}

EglContext *
gst_vaapi_display_egl_get_context (GstVaapiDisplayEGL * display)
{
  return ensure_context (display) ? display->egl_context : NULL;
}

EGLDisplay
gst_vaapi_display_egl_get_gl_display (GstVaapiDisplayEGL * display)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_EGL (display), EGL_NO_DISPLAY);

  return display->egl_display->base.handle.p;
}

EGLContext
gst_vaapi_display_egl_get_gl_context (GstVaapiDisplayEGL * display)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_EGL (display), EGL_NO_CONTEXT);

  return ensure_context (display) ? display->egl_context->base.handle.p :
      EGL_NO_CONTEXT;
}

gboolean
gst_vaapi_display_egl_set_gl_context (GstVaapiDisplayEGL * display,
    EGLContext gl_context)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_EGL (display), FALSE);

  return ensure_context_is_wrapped (display, gl_context);
}
