/* Copyright (C) <2025> Philippe Normand <philn@igalia.com>
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
#include <config.h>
#endif

#include "gstwpedisplay.h"
#include "gstwpeview.h"
#include "gstwpetoplevel.h"
#include <EGL/egl.h>
#include <gst/gl/gstglfeature.h>
#include <EGL/eglext.h>

GST_DEBUG_CATEGORY_EXTERN (wpe_view_debug);
#define GST_CAT_DEFAULT wpe_view_debug

enum
{
  SIGNAL_WPE_VIEW_CREATED,
  LAST_SIGNAL
};
static guint gst_wpe_display_signals[LAST_SIGNAL] = { 0 };

struct _WPEDisplayGStreamer
{
  WPEDisplay parent;

  GstGLDisplay *gstDisplay;
  GstGLContext *gstContext;
  GstGLDisplayEGL *gstEGLDisplay;

  EGLDisplay eglDisplay;
  gchar *drm_device;
  gchar *drm_render_node;
};

#define wpe_display_gstreamer_parent_class parent_class
G_DEFINE_TYPE (WPEDisplayGStreamer, wpe_display_gstreamer, WPE_TYPE_DISPLAY);

typedef EGLBoolean (*eglQueryDisplayAttribEXTFunc) (EGLDisplay, EGLint,
    EGLAttrib *);
typedef const char *(*eglQueryDeviceStringEXTFunc) (EGLDeviceEXT device,
    EGLint name);

typedef struct _VTable
{
  eglQueryDisplayAttribEXTFunc eglQueryDisplayAttribEXT;
  eglQueryDeviceStringEXTFunc eglQueryDeviceStringEXT;
} VTable;

static gboolean
wpe_display_gstreamer_connect (WPEDisplay * display, GError ** error)
{
  auto self = WPE_DISPLAY_GSTREAMER (display);

  if (!self->gstDisplay)
    return TRUE;

  if (gst_gl_context_get_gl_platform (self->gstContext) == GST_GL_PLATFORM_EGL) {
    self->gstEGLDisplay = gst_gl_display_egl_from_gl_display (self->gstDisplay);
  } else {
    g_set_error_literal (error, WPE_VIEW_ERROR, WPE_VIEW_ERROR_RENDER_FAILED,
        "Available GStreamer GL Context is not EGL - not creating an EGL display from it");
    return FALSE;
  }

  const gchar *egl_exts = eglQueryString (EGL_NO_DISPLAY, EGL_EXTENSIONS);

  self->eglDisplay = (EGLDisplay)
      gst_gl_display_get_handle (GST_GL_DISPLAY (self->gstEGLDisplay));

  if (!gst_gl_check_extension ("EGL_EXT_device_query", egl_exts)) {
    g_set_error_literal (error, WPE_VIEW_ERROR, WPE_VIEW_ERROR_RENDER_FAILED,
        "Failed to initialize rendering: 'EGL_EXT_device_query' not available");
    return FALSE;
  }

  EGLDeviceEXT eglDevice;
  VTable vt;
  vt.eglQueryDisplayAttribEXT = (eglQueryDisplayAttribEXTFunc)
      gst_gl_context_get_proc_address (self->gstContext,
      "eglQueryDisplayAttribEXT");
  if (!vt.eglQueryDisplayAttribEXT (self->eglDisplay, EGL_DEVICE_EXT,
          reinterpret_cast < EGLAttrib * >(&eglDevice))) {
    g_set_error_literal (error, WPE_VIEW_ERROR, WPE_VIEW_ERROR_RENDER_FAILED,
        "Failed to initialize rendering: 'EGLDeviceEXT' not available");
    return FALSE;
  }

  vt.eglQueryDeviceStringEXT = (eglQueryDeviceStringEXTFunc)
      gst_gl_context_get_proc_address (self->gstContext,
      "eglQueryDeviceStringEXT");

  const char *extensions =
      vt.eglQueryDeviceStringEXT (eglDevice, EGL_EXTENSIONS);
  if (gst_gl_check_extension ("EGL_EXT_device_drm", extensions))
    self->drm_device =
        g_strdup (vt.eglQueryDeviceStringEXT (eglDevice,
            EGL_DRM_DEVICE_FILE_EXT));
  else {
    // FIXME: This kind of hack is needed when using gtkglsink. glimagesink somehow works as expected.
    const gchar *render_node_path = g_getenv ("GST_WPE_DRM_RENDER_NODE_PATH");
    if (render_node_path) {
      GST_DEBUG ("Setting render node path from GST_WPE_DRM_RENDER_NODE_PATH "
          "environment variable");
      self->drm_render_node = g_strdup (render_node_path);
    } else {
      GST_WARNING ("'EGL_EXT_device_drm' not available, hardcoding render node "
          "to /dev/dri/renderD128");
      self->drm_render_node = g_strdup ("/dev/dri/renderD128");
    }
    return TRUE;
  }

  if (gst_gl_check_extension ("EGL_EXT_device_drm_render_node", extensions))
    self->drm_render_node =
        g_strdup (vt.eglQueryDeviceStringEXT (eglDevice,
            EGL_DRM_RENDER_NODE_FILE_EXT));
  else {
    g_set_error_literal (error, WPE_VIEW_ERROR, WPE_VIEW_ERROR_RENDER_FAILED,
        "Failed to initialize rendering: 'EGL_EXT_device_drm_render_node' not available");
    return FALSE;
  }

  return TRUE;
}

static WPEView *
wpe_display_gstreamer_create_view (WPEDisplay * display)
{
  auto gst_display = WPE_DISPLAY_GSTREAMER (display);
  auto view = wpe_view_gstreamer_new (gst_display);
  GValue args[2] = { {0}, {0} };

  g_value_init (&args[0], WPE_TYPE_DISPLAY_GSTREAMER);
  g_value_set_object (&args[0], gst_display);

  g_value_init (&args[1], WPE_TYPE_VIEW);
  g_value_set_object (&args[1], view);

  g_signal_emitv (args, gst_wpe_display_signals[SIGNAL_WPE_VIEW_CREATED], 0,
      NULL);

  g_value_unset (&args[0]);
  g_value_unset (&args[1]);

  auto toplevel = wpe_toplevel_gstreamer_new (gst_display);
  wpe_view_set_toplevel (view, toplevel);
  g_object_unref (toplevel);

  return view;
}

static gpointer
wpe_display_gstreamer_get_egl_display (WPEDisplay * display, GError **)
{
  return WPE_DISPLAY_GSTREAMER (display)->eglDisplay;
}

static const char *
wpe_display_gstreamer_get_drm_device (WPEDisplay * display)
{
  return WPE_DISPLAY_GSTREAMER (display)->drm_device;
}

static const char *
wpe_display_gstreamer_get_drm_render_node (WPEDisplay * display)
{
  auto self = WPE_DISPLAY_GSTREAMER (display);
  if (self->drm_render_node)
    return self->drm_render_node;
  return self->drm_device;
}

static void
wpe_display_gstreamer_init (WPEDisplayGStreamer * display)
{
  display->drm_render_node = nullptr;
  display->drm_device = nullptr;
}

static void
wpe_display_gstreamer_finalize (GObject * object)
{
  auto self = WPE_DISPLAY_GSTREAMER (object);

  g_clear_pointer (&self->drm_device, g_free);
  g_clear_pointer (&self->drm_render_node, g_free);

  gst_clear_object (&self->gstEGLDisplay);

  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
wpe_display_gstreamer_class_init (WPEDisplayGStreamerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = wpe_display_gstreamer_finalize;

  WPEDisplayClass *displayClass = WPE_DISPLAY_CLASS (klass);
  displayClass->connect = wpe_display_gstreamer_connect;
  displayClass->create_view = wpe_display_gstreamer_create_view;
  displayClass->get_egl_display = wpe_display_gstreamer_get_egl_display;
  displayClass->get_drm_device = wpe_display_gstreamer_get_drm_device;
  displayClass->get_drm_render_node = wpe_display_gstreamer_get_drm_render_node;

  gst_wpe_display_signals[SIGNAL_WPE_VIEW_CREATED] =
      g_signal_new ("wpe-view-created", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, NULL, G_TYPE_NONE, 1, WPE_TYPE_VIEW);
}

WPEDisplay *
wpe_display_gstreamer_new ()
{
  auto display =
      WPE_DISPLAY_GSTREAMER (g_object_new (WPE_TYPE_DISPLAY_GSTREAMER,
          nullptr));
  return WPE_DISPLAY (display);
}

void
wpe_display_gstreamer_set_gl (WPEDisplay * display, GstGLDisplay * glDisplay,
    GstGLContext * context)
{
  auto self = WPE_DISPLAY_GSTREAMER (display);
  self->gstDisplay = glDisplay;
  self->gstContext = context;
}
