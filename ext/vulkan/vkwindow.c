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

/**
 * SECTION:vkwindow
 * @short_description: window/surface abstraction
 * @title: GstVulkanWindow
 * @see_also: #GstGLContext, #GstGLDisplay
 *
 * GstVulkanWindow represents a window that elements can render into.  A window can
 * either be a user visible window (onscreen) or hidden (offscreen).
 */

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <gmodule.h>
#include <stdio.h>

#include "vkwindow.h"

#if GST_VULKAN_HAVE_WINDOW_X11
#include "x11/vkwindow_x11.h"
#endif
#if GST_VULKAN_HAVE_WINDOW_XCB
#include "xcb/vkwindow_xcb.h"
#endif
#if GST_VULKAN_HAVE_WINDOW_WAYLAND
#include "wayland/vkwindow_wayland.h"
#endif

#define GST_CAT_DEFAULT gst_vulkan_window_debug
GST_DEBUG_CATEGORY (GST_CAT_DEFAULT);

#define gst_vulkan_window_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstVulkanWindow, gst_vulkan_window, GST_TYPE_OBJECT);

#define GST_VULKAN_WINDOW_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_VULKAN_WINDOW, GstVulkanWindowPrivate))

struct _GstVulkanWindowPrivate
{
  guint surface_width;
  guint surface_height;
};

static void gst_vulkan_window_finalize (GObject * object);

typedef struct _GstVulkanDummyWindow
{
  GstVulkanWindow parent;

  guintptr handle;
} GstVulkanDummyWindow;

typedef struct _GstVulkanDummyWindowCass
{
  GstVulkanWindowClass parent;
} GstVulkanDummyWindowClass;

GstVulkanDummyWindow *gst_vulkan_dummy_window_new (void);

enum
{
  SIGNAL_0,
  SIGNAL_CLOSE,
  SIGNAL_DRAW,
  LAST_SIGNAL
};

static guint gst_vulkan_window_signals[LAST_SIGNAL] = { 0 };

static gboolean
_accum_logical_and (GSignalInvocationHint * ihint, GValue * return_accu,
    const GValue * handler_return, gpointer data)
{
  gboolean val = g_value_get_boolean (handler_return);
  gboolean val2 = g_value_get_boolean (return_accu);

  g_value_set_boolean (return_accu, val && val2);

  return TRUE;
}

GQuark
gst_vulkan_window_error_quark (void)
{
  return g_quark_from_static_string ("gst-gl-window-error-quark");
}

static gboolean
gst_vulkan_window_default_open (GstVulkanWindow * window, GError ** error)
{
  return TRUE;
}

static void
gst_vulkan_window_default_close (GstVulkanWindow * window)
{
}

static void
_init_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkanwindow", 0,
        "Vulkan Window");
    g_once_init_leave (&_init, 1);
  }
}

static void
gst_vulkan_window_init (GstVulkanWindow * window)
{
  window->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (window, GST_TYPE_VULKAN_WINDOW,
      GstVulkanWindowPrivate);
}

static void
gst_vulkan_window_class_init (GstVulkanWindowClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstVulkanWindowPrivate));

  klass->open = GST_DEBUG_FUNCPTR (gst_vulkan_window_default_open);
  klass->close = GST_DEBUG_FUNCPTR (gst_vulkan_window_default_close);

  gst_vulkan_window_signals[SIGNAL_CLOSE] =
      g_signal_new ("close", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
      (GSignalAccumulator) _accum_logical_and, NULL, NULL, G_TYPE_BOOLEAN, 0);

  gst_vulkan_window_signals[SIGNAL_DRAW] =
      g_signal_new ("draw", G_TYPE_FROM_CLASS (klass), G_SIGNAL_RUN_LAST, 0,
      NULL, NULL, NULL, G_TYPE_NONE, 0);

  G_OBJECT_CLASS (klass)->finalize = gst_vulkan_window_finalize;

  _init_debug ();
}

/**
 * gst_vulkan_window_new:
 * @display: a #GstGLDisplay
 *
 * Returns: (transfer full): a new #GstVulkanWindow using @display's connection
 *
 * Since: 1.10
 */
GstVulkanWindow *
gst_vulkan_window_new (GstVulkanDisplay * display)
{
  GstVulkanWindow *window = NULL;
  const gchar *user_choice;

  g_return_val_if_fail (display != NULL, NULL);

  _init_debug ();

  user_choice = g_getenv ("GST_VULKAN_WINDOW");
  GST_INFO ("creating a window, user choice:%s", user_choice);
#if GST_VULKAN_HAVE_WINDOW_X11
  if (!window && (!user_choice || g_strstr_len (user_choice, 3, "x11")))
    window = GST_VULKAN_WINDOW (gst_vulkan_window_x11_new (display));
#endif
#if GST_VULKAN_HAVE_WINDOW_XCB
  if (!window && (!user_choice || g_strstr_len (user_choice, 3, "xcb")))
    window = GST_VULKAN_WINDOW (gst_vulkan_window_xcb_new (display));
#endif
#if GST_VULKAN_HAVE_WINDOW_WAYLAND
  if (!window && (!user_choice || g_strstr_len (user_choice, 7, "wayland")))
    window = GST_VULKAN_WINDOW (gst_vulkan_window_wayland_new (display));
#endif
  if (!window) {
    /* subclass returned a NULL window */
    GST_WARNING ("Could not create window. user specified %s, creating dummy"
        " window", user_choice ? user_choice : "(null)");

    window = GST_VULKAN_WINDOW (gst_vulkan_dummy_window_new ());
  }

  window->display = gst_object_ref (display);

  return window;
}

static void
gst_vulkan_window_finalize (GObject * object)
{
  GstVulkanWindow *window = GST_VULKAN_WINDOW (object);

  gst_object_unref (window->display);

  G_OBJECT_CLASS (gst_vulkan_window_parent_class)->finalize (object);
}

GstVulkanDisplay *
gst_vulkan_window_get_display (GstVulkanWindow * window)
{
  g_return_val_if_fail (GST_IS_VULKAN_WINDOW (window), NULL);

  return gst_object_ref (window->display);
}

VkSurfaceKHR
gst_vulkan_window_get_surface (GstVulkanWindow * window, GError ** error)
{
  GstVulkanWindowClass *klass;

  g_return_val_if_fail (GST_IS_VULKAN_WINDOW (window), (VkSurfaceKHR) 0);
  klass = GST_VULKAN_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (klass->get_surface != NULL, (VkSurfaceKHR) 0);

  return klass->get_surface (window, error);
}

gboolean
gst_vulkan_window_get_presentation_support (GstVulkanWindow * window,
    GstVulkanDevice * device, guint32 queue_family_idx)
{
  GstVulkanWindowClass *klass;

  g_return_val_if_fail (GST_IS_VULKAN_WINDOW (window), FALSE);
  klass = GST_VULKAN_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (klass->get_presentation_support != NULL, FALSE);

  return klass->get_presentation_support (window, device, queue_family_idx);
}

gboolean
gst_vulkan_window_open (GstVulkanWindow * window, GError ** error)
{
  GstVulkanWindowClass *klass;

  g_return_val_if_fail (GST_IS_VULKAN_WINDOW (window), FALSE);
  klass = GST_VULKAN_WINDOW_GET_CLASS (window);
  g_return_val_if_fail (klass->open != NULL, FALSE);

  return klass->open (window, error);
}

void
gst_vulkan_window_close (GstVulkanWindow * window)
{
  GstVulkanWindowClass *klass;
  gboolean to_close;

  g_return_if_fail (GST_IS_VULKAN_WINDOW (window));
  klass = GST_VULKAN_WINDOW_GET_CLASS (window);
  g_return_if_fail (klass->close != NULL);

  g_signal_emit (window, gst_vulkan_window_signals[SIGNAL_CLOSE], 0, &to_close);

  if (to_close)
    klass->close (window);
}

void
gst_vulkan_window_resize (GstVulkanWindow * window, gint width, gint height)
{
  g_return_if_fail (GST_IS_VULKAN_WINDOW (window));

  window->priv->surface_width = width;
  window->priv->surface_height = height;

  /* XXX: possibly queue a resize/redraw */
}

void
gst_vulkan_window_redraw (GstVulkanWindow * window)
{
  g_return_if_fail (GST_IS_VULKAN_WINDOW (window));

  g_signal_emit (window, gst_vulkan_window_signals[SIGNAL_DRAW], 0);
}

GType gst_vulkan_dummy_window_get_type (void);
G_DEFINE_TYPE (GstVulkanDummyWindow, gst_vulkan_dummy_window,
    GST_TYPE_VULKAN_WINDOW);

static void
gst_vulkan_dummy_window_class_init (GstVulkanDummyWindowClass * klass)
{
}

static void
gst_vulkan_dummy_window_init (GstVulkanDummyWindow * dummy)
{
  dummy->handle = 0;
}

GstVulkanDummyWindow *
gst_vulkan_dummy_window_new (void)
{
  return g_object_new (gst_vulkan_dummy_window_get_type (), NULL);
}
