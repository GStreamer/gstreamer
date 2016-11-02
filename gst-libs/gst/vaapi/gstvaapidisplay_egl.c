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
#include "gstvaapidisplay_egl.h"
#include "gstvaapidisplay_egl_priv.h"
#include "gstvaapiwindow.h"
#include "gstvaapiwindow_egl.h"
#include "gstvaapiwindow_priv.h"
#include "gstvaapitexture_egl.h"

#if USE_X11
#include "gstvaapidisplay_x11.h"
#endif
#if USE_WAYLAND
#include "gstvaapidisplay_wayland.h"
#endif

#define DEBUG_VAAPI_DISPLAY 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE (GstVaapiDisplayEGL, gst_vaapi_display_egl,
    GST_TYPE_VAAPI_DISPLAY);

/* ------------------------------------------------------------------------- */
/* --- EGL backend implementation                                        --- */
/* ------------------------------------------------------------------------- */

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
gst_vaapi_display_egl_bind_display (GstVaapiDisplay * base_display,
    gpointer native_params)
{
  GstVaapiDisplay *native_display = NULL;
  GstVaapiDisplayEGL *display = GST_VAAPI_DISPLAY_EGL (base_display);
  EglDisplay *egl_display;
  const InitParams *params = (InitParams *) native_params;

  if (params->display) {
    native_display = params->display;
  } else {
#if USE_X11
    native_display = gst_vaapi_display_x11_new (NULL);
#endif
#if USE_WAYLAND
    if (!native_display)
      native_display = gst_vaapi_display_wayland_new (NULL);
#endif
  }
  if (!native_display)
    return FALSE;

  gst_vaapi_display_replace (&display->display, native_display);

  egl_display = egl_display_new (GST_VAAPI_DISPLAY_NATIVE (display->display));
  if (!egl_display)
    return FALSE;

  egl_object_replace (&display->egl_display, egl_display);
  egl_object_unref (egl_display);
  display->gles_version = params->gles_version;
  return TRUE;
}

static void
gst_vaapi_display_egl_close_display (GstVaapiDisplay * base_display)
{
  GstVaapiDisplayEGL *display = GST_VAAPI_DISPLAY_EGL (base_display);
  gst_vaapi_display_replace (&display->display, NULL);
}

static void
gst_vaapi_display_egl_lock (GstVaapiDisplay * base_display)
{
  GstVaapiDisplayEGL *display = GST_VAAPI_DISPLAY_EGL (base_display);
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->lock)
    klass->lock (display->display);
}

static void
gst_vaapi_display_egl_unlock (GstVaapiDisplay * base_display)
{
  GstVaapiDisplayEGL *display = GST_VAAPI_DISPLAY_EGL (base_display);
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->unlock)
    klass->unlock (display->display);
}

static void
gst_vaapi_display_egl_sync (GstVaapiDisplay * base_display)
{
  GstVaapiDisplayEGL *display = GST_VAAPI_DISPLAY_EGL (base_display);
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->sync)
    klass->sync (display->display);
  else if (klass->flush)
    klass->flush (display->display);
}

static void
gst_vaapi_display_egl_flush (GstVaapiDisplay * base_display)
{
  GstVaapiDisplayEGL *display = GST_VAAPI_DISPLAY_EGL (base_display);
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->flush)
    klass->flush (display->display);
}

static gboolean
gst_vaapi_display_egl_get_display_info (GstVaapiDisplay * base_display,
    GstVaapiDisplayInfo * info)
{
  GstVaapiDisplayEGL *display = GST_VAAPI_DISPLAY_EGL (base_display);
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->get_display && !klass->get_display (display->display, info))
    return FALSE;
  return TRUE;
}

static void
gst_vaapi_display_egl_get_size (GstVaapiDisplay * base_display,
    guint * width_ptr, guint * height_ptr)
{
  GstVaapiDisplayEGL *display = GST_VAAPI_DISPLAY_EGL (base_display);
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->get_size)
    klass->get_size (display->display, width_ptr, height_ptr);
}

static void
gst_vaapi_display_egl_get_size_mm (GstVaapiDisplay * base_display,
    guint * width_ptr, guint * height_ptr)
{
  GstVaapiDisplayEGL *display = GST_VAAPI_DISPLAY_EGL (base_display);
  GstVaapiDisplayClass *const klass =
      GST_VAAPI_DISPLAY_GET_CLASS (display->display);

  if (klass->get_size_mm)
    klass->get_size_mm (display->display, width_ptr, height_ptr);
}

static guintptr
gst_vaapi_display_egl_get_visual_id (GstVaapiDisplay * base_display,
    GstVaapiWindow * window)
{
  GstVaapiDisplayEGL *display = GST_VAAPI_DISPLAY_EGL (base_display);
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

static void
ensure_texture_map (GstVaapiDisplayEGL * display)
{
  if (!display->texture_map)
    display->texture_map = gst_vaapi_texture_map_new ();
}

static GstVaapiTexture *
gst_vaapi_display_egl_create_texture (GstVaapiDisplay * display, GstVaapiID id,
    guint target, guint format, guint width, guint height)
{
  GstVaapiDisplayEGL *dpy = GST_VAAPI_DISPLAY_EGL (display);
  GstVaapiTexture *texture;

  if (id == GST_VAAPI_ID_INVALID)
    return gst_vaapi_texture_egl_new (display, target, format, width, height);

  ensure_texture_map (dpy);
  if (!(texture = gst_vaapi_texture_map_lookup (dpy->texture_map, id))) {
    if ((texture =
            gst_vaapi_texture_egl_new_wrapped (display, id, target, format,
                width, height))) {
      gst_vaapi_texture_map_add (dpy->texture_map, texture, id);
    }
  }

  return texture;
}

static GstVaapiTextureMap *
gst_vaapi_display_egl_get_texture_map (GstVaapiDisplay * display)
{
  return GST_VAAPI_DISPLAY_EGL (display)->texture_map;
}

static void
gst_vaapi_display_egl_finalize (GObject * object)
{
  GstVaapiDisplayEGL *dpy = GST_VAAPI_DISPLAY_EGL (object);

  if (dpy->texture_map)
    gst_object_unref (dpy->texture_map);
  G_OBJECT_CLASS (gst_vaapi_display_egl_parent_class)->finalize (object);
}

static void
gst_vaapi_display_egl_init (GstVaapiDisplayEGL * display)
{
}

static void
gst_vaapi_display_egl_class_init (GstVaapiDisplayEGLClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiDisplayClass *const dpy_class = GST_VAAPI_DISPLAY_CLASS (klass);

  object_class->finalize = gst_vaapi_display_egl_finalize;
  dpy_class->display_type = GST_VAAPI_DISPLAY_TYPE_EGL;
  dpy_class->bind_display = gst_vaapi_display_egl_bind_display;
  dpy_class->close_display = gst_vaapi_display_egl_close_display;
  dpy_class->lock = gst_vaapi_display_egl_lock;
  dpy_class->unlock = gst_vaapi_display_egl_unlock;
  dpy_class->sync = gst_vaapi_display_egl_sync;
  dpy_class->flush = gst_vaapi_display_egl_flush;
  dpy_class->get_display = gst_vaapi_display_egl_get_display_info;
  dpy_class->get_size = gst_vaapi_display_egl_get_size;
  dpy_class->get_size_mm = gst_vaapi_display_egl_get_size_mm;
  dpy_class->get_visual_id = gst_vaapi_display_egl_get_visual_id;
  dpy_class->create_window = gst_vaapi_display_egl_create_window;
  dpy_class->create_texture = gst_vaapi_display_egl_create_texture;
  dpy_class->get_texture_map = gst_vaapi_display_egl_get_texture_map;
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
    params.display = display;
    params.display_type = GST_VAAPI_DISPLAY_VADISPLAY_TYPE (display);
  } else {
    params.display = NULL;
    params.display_type = GST_VAAPI_DISPLAY_TYPE_ANY;
  }
  params.gles_version = gles_version;
  return gst_vaapi_display_new (g_object_new (GST_TYPE_VAAPI_DISPLAY_EGL, NULL),
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
  return gst_vaapi_display_new (g_object_new (GST_TYPE_VAAPI_DISPLAY_EGL, NULL),
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
