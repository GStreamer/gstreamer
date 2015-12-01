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
  GThread *event_thread;

  GMutex thread_lock;
  GCond thread_cond;
};

static gpointer
_event_thread_main (GstVulkanDisplay * display)
{
  g_mutex_lock (&display->priv->thread_lock);

  display->main_context = g_main_context_new ();
  display->main_loop = g_main_loop_new (display->main_context, FALSE);

  g_cond_broadcast (&display->priv->thread_cond);
  g_mutex_unlock (&display->priv->thread_lock);

  g_main_loop_run (display->main_loop);

  g_mutex_lock (&display->priv->thread_lock);

  g_main_loop_unref (display->main_loop);
  g_main_context_unref (display->main_context);

  display->main_loop = NULL;
  display->main_context = NULL;

  g_cond_broadcast (&display->priv->thread_cond);
  g_mutex_unlock (&display->priv->thread_lock);

  return NULL;
}

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

  g_mutex_init (&display->priv->thread_lock);
  g_cond_init (&display->priv->thread_cond);

  display->priv->event_thread = g_thread_new ("vkdisplay-event",
      (GThreadFunc) _event_thread_main, display);

  g_mutex_lock (&display->priv->thread_lock);
  while (!display->main_loop)
    g_cond_wait (&display->priv->thread_cond, &display->priv->thread_lock);
  g_mutex_unlock (&display->priv->thread_lock);
}

static void
gst_vulkan_display_finalize (GObject * object)
{
  GstVulkanDisplay *display = GST_VULKAN_DISPLAY (object);

  g_mutex_lock (&display->priv->thread_lock);
  if (display->main_context && display->event_source) {
    g_source_destroy (display->event_source);
    g_source_unref (display->event_source);
  }
  display->event_source = NULL;

  if (display->main_loop)
    g_main_loop_quit (display->main_loop);

  while (display->main_loop)
    g_cond_wait (&display->priv->thread_cond, &display->priv->thread_lock);

  if (display->priv->event_thread)
    g_thread_unref (display->priv->event_thread);
  display->priv->event_thread = NULL;
  g_mutex_unlock (&display->priv->thread_lock);

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

    display = g_object_new (GST_TYPE_VULKAN_DISPLAY, NULL);
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

gboolean
gst_vulkan_display_remove_window (GstVulkanDisplay * display,
    GstVulkanWindow * window)
{
  gboolean ret = FALSE;
  GList *l;

  GST_OBJECT_LOCK (display);
  l = g_list_find (display->windows, window);
  if (l) {
    display->windows = g_list_delete_link (display->windows, l);
    ret = TRUE;
  }
  GST_OBJECT_UNLOCK (display);

  return ret;
}
