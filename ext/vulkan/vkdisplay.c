/*
 * GStreamer
 * Copyright (C) 2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 * Copyright (C) 2008 Filippo Argiolas <filippo.argiolas@gmail.com>
 * Copyright (C) 2013 Matthew Waters <ystreet00@gmail.com>
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

#include "vkdisplay.h"

#if GST_VULKAN_HAVE_WINDOW_X11
#include "x11/vkdisplay_x11.h"
#endif
#if GST_VULKAN_HAVE_WINDOW_XCB
#include "xcb/vkdisplay_xcb.h"
#endif

#define GST_CAT_DEFAULT gst_vulkan_display_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void
_init_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkandisplay", 0,
        "Vulkan display");
    g_once_init_leave (&_init, 1);
  }
}

G_DEFINE_TYPE_WITH_CODE (GstVulkanDisplay, gst_vulkan_display, GST_TYPE_OBJECT,
    _init_debug ());

#define GST_VULKAN_DISPLAY_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_VULKAN_DISPLAY, GstVulkanDisplayPrivate))

enum
{
  SIGNAL_0,
  CREATE_CONTEXT,
  LAST_SIGNAL
};

/* static guint gst_vulkan_display_signals[LAST_SIGNAL] = { 0 }; */

static void gst_vulkan_display_finalize (GObject * object);
static gpointer gst_vulkan_display_default_get_handle (GstVulkanDisplay *
    display);
static gpointer gst_vulkan_display_default_get_platform_handle (GstVulkanDisplay
    * display);
static GstVulkanWindow
    * gst_vulkan_display_default_create_window (GstVulkanDisplay * display);

struct _GstVulkanDisplayPrivate
{
  gint dummy;
};

static void
gst_vulkan_display_class_init (GstVulkanDisplayClass * klass)
{
  g_type_class_add_private (klass, sizeof (GstVulkanDisplayPrivate));

  klass->get_handle = gst_vulkan_display_default_get_handle;
  klass->get_platform_handle = gst_vulkan_display_default_get_platform_handle;
  klass->create_window = gst_vulkan_display_default_create_window;

  G_OBJECT_CLASS (klass)->finalize = gst_vulkan_display_finalize;
}

static void
gst_vulkan_display_init (GstVulkanDisplay * display)
{
  display->priv = GST_VULKAN_DISPLAY_GET_PRIVATE (display);
  display->type = GST_VULKAN_DISPLAY_TYPE_ANY;
}

static void
gst_vulkan_display_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_vulkan_display_parent_class)->finalize (object);
}

/**
 * gst_vulkan_display_new:
 *
 * Returns: (transfer full): a new #GstVulkanDisplay
 *
 * Since: 1.10
 */
GstVulkanDisplay *
gst_vulkan_display_new (void)
{
  GstVulkanDisplay *display = NULL;
  const gchar *user_choice, *platform_choice;

  _init_debug ();

  user_choice = g_getenv ("GST_GL_WINDOW");
  platform_choice = g_getenv ("GST_GL_PLATFORM");
  GST_INFO ("creating a display, user choice:%s (platform: %s)",
      GST_STR_NULL (user_choice), GST_STR_NULL (platform_choice));

#if GST_VULKAN_HAVE_WINDOW_XCB
  if (!display && (!user_choice || g_strstr_len (user_choice, 3, "xcb")))
    display = GST_VULKAN_DISPLAY (gst_vulkan_display_xcb_new (NULL));
#endif
#if GST_VULKAN_HAVE_WINDOW_X11
  if (!display && (!user_choice || g_strstr_len (user_choice, 3, "x11")))
    display = GST_VULKAN_DISPLAY (gst_vulkan_display_x11_new (NULL));
#endif
  if (!display) {
    /* subclass returned a NULL window */
    GST_WARNING ("Could not create display. user specified %s "
        "(platform: %s), creating dummy",
        GST_STR_NULL (user_choice), GST_STR_NULL (platform_choice));

    return g_object_new (GST_TYPE_VULKAN_DISPLAY, NULL);
  }

  return display;
}

/**
 * gst_vulkan_display_get_handle:
 * @display: a #GstVulkanDisplay
 *
 * Returns: the winsys specific handle of @display
 *
 * Since: 1.10
 */
gpointer
gst_vulkan_display_get_handle (GstVulkanDisplay * display)
{
  GstVulkanDisplayClass *klass;

  g_return_val_if_fail (GST_IS_VULKAN_DISPLAY (display), NULL);
  klass = GST_VULKAN_DISPLAY_GET_CLASS (display);
  g_return_val_if_fail (klass->get_handle != NULL, NULL);

  return klass->get_handle (display);
}

static gpointer
gst_vulkan_display_default_get_handle (GstVulkanDisplay * display)
{
  return 0;
}

/**
 * gst_vulkan_display_get_platform_handle:
 * @display: a #GstVulkanDisplay
 *
 * Returns: the winsys specific handle of @display for use with the
 * VK_EXT_KHR_swapchain extension.
 *
 * Since: 1.10
 */
gpointer
gst_vulkan_display_get_platform_handle (GstVulkanDisplay * display)
{
  GstVulkanDisplayClass *klass;

  g_return_val_if_fail (GST_IS_VULKAN_DISPLAY (display), NULL);
  klass = GST_VULKAN_DISPLAY_GET_CLASS (display);
  g_return_val_if_fail (klass->get_handle != NULL, NULL);

  return klass->get_platform_handle (display);
}

static gpointer
gst_vulkan_display_default_get_platform_handle (GstVulkanDisplay * display)
{
  return (gpointer) gst_vulkan_display_get_handle (display);
}

/**
 * gst_vulkan_display_get_handle_type:
 * @display: a #GstVulkanDisplay
 *
 * Returns: the #GstVulkanDisplayType of @display
 *
 * Since: 1.10
 */
GstVulkanDisplayType
gst_vulkan_display_get_handle_type (GstVulkanDisplay * display)
{
  g_return_val_if_fail (GST_IS_VULKAN_DISPLAY (display),
      GST_VULKAN_DISPLAY_TYPE_NONE);

  return display->type;
}

/**
 * gst_vulkan_display_create_window:
 * @display: a #GstVulkanDisplay
 *
 * Returns: a new #GstVulkanWindow for @display or %NULL.
 */
GstVulkanWindow *
gst_vulkan_display_create_window (GstVulkanDisplay * display)
{
  GstVulkanDisplayClass *klass;
  GstVulkanWindow *window;

  g_return_val_if_fail (GST_IS_VULKAN_DISPLAY (display), NULL);
  klass = GST_VULKAN_DISPLAY_GET_CLASS (display);
  g_return_val_if_fail (klass->create_window != NULL, NULL);

  window = klass->create_window (display);

  if (window) {
    GST_OBJECT_LOCK (display);
    display->windows = g_list_prepend (display->windows, window);
    GST_OBJECT_UNLOCK (display);
  }

  return window;
}

static GstVulkanWindow *
gst_vulkan_display_default_create_window (GstVulkanDisplay * display)
{
  return gst_vulkan_window_new (display);
}
