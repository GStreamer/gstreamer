/*
 * GStreamer
 * Copyright (C) 2019 Matthew Waters <matthew@centricular.com>
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

#include <QuartzCore/QuartzCore.h>

#include <gst/gst.h>

#include "vkwindow_ios.h"
#include "vkdisplay_ios.h"

#include "vkios_utils.h"

#define GST_VULKAN_WINDOW_IOS_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_VULKAN_WINDOW_IOS, GstVulkanWindowIosPrivate))

#define GST_CAT_DEFAULT gst_vulkan_window_ios_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void
_init_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkanwindowios", 0,
        "Vulkan iOS Window");
    g_once_init_leave (&_init, 1);
  }
}

enum
{
  PROP_0,
};

struct _GstVulkanWindowIosPrivate
{
  gpointer internal_view;
  gpointer external_view;

  gint preferred_width;
  gint preferred_height;
};

#define gst_vulkan_window_ios_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanWindowIos, gst_vulkan_window_ios,
    GST_TYPE_VULKAN_WINDOW, G_ADD_PRIVATE (GstVulkanWindowIos) _init_debug ());

static VkSurfaceKHR gst_vulkan_window_ios_get_surface (GstVulkanWindow * window,
    GError ** error);
static gboolean gst_vulkan_window_ios_get_presentation_support (GstVulkanWindow
    * window, GstVulkanDevice * device, guint32 queue_family_idx);
static gboolean gst_vulkan_window_ios_open (GstVulkanWindow * window,
    GError ** error);
static void gst_vulkan_window_ios_close (GstVulkanWindow * window);
static void gst_vulkan_window_ios_set_window_handle (GstVulkanWindow * window,
    guintptr window_handle);

static void
gst_vulkan_window_ios_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vulkan_window_ios_class_init (GstVulkanWindowIosClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstVulkanWindowClass *window_class = (GstVulkanWindowClass *) klass;

  obj_class->finalize = gst_vulkan_window_ios_finalize;

  window_class->open = GST_DEBUG_FUNCPTR (gst_vulkan_window_ios_open);
  window_class->close = GST_DEBUG_FUNCPTR (gst_vulkan_window_ios_close);
  window_class->get_surface = gst_vulkan_window_ios_get_surface;
  window_class->get_presentation_support =
      gst_vulkan_window_ios_get_presentation_support;
  window_class->set_window_handle =
      gst_vulkan_window_ios_set_window_handle;
}

static void
gst_vulkan_window_ios_init (GstVulkanWindowIos * window)
{
  window->priv = gst_vulkan_window_ios_get_instance_private (window);

  window->priv->preferred_width = 320;
  window->priv->preferred_height = 240;
}

/* Must be called in the gl thread */
GstVulkanWindowIos *
gst_vulkan_window_ios_new (GstVulkanDisplay * display)
{
  GstVulkanWindowIos *window;

  _init_debug ();

  if ((gst_vulkan_display_get_handle_type (display) &
          GST_VULKAN_DISPLAY_TYPE_IOS)
      == GST_VULKAN_DISPLAY_TYPE_NONE) {
    GST_INFO ("Wrong display type %u for this window type %u", display->type,
        GST_VULKAN_DISPLAY_TYPE_IOS);
    return NULL;
  }

  window = g_object_new (GST_TYPE_VULKAN_WINDOW_IOS, NULL);
  gst_object_ref_sink (window);

  return window;
}

static void
_create_window (GstVulkanWindowIos * window_ios)
{
  GstVulkanWindowIosPrivate *priv = window_ios->priv;
  CGRect rect = CGRectMake (0, 0, priv->preferred_width, priv->preferred_height);
  UIView *external_view = (__bridge UIView *) priv->external_view;
  GstVulkanUIView *view;

  view = [[GstVulkanUIView alloc] initWithFrame:rect];
  view.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

  priv->internal_view = (__bridge_retained gpointer)view;
  [external_view addSubview:view];
}

gboolean
gst_vulkan_window_ios_create_window (GstVulkanWindowIos * window_ios)
{
  if (!window_ios->priv->external_view) {
    GST_WARNING_OBJECT(window_ios, "No external UIView provided");
    return FALSE;
  }

  _invoke_on_main ((GstVulkanWindowFunc) _create_window,
      gst_object_ref (window_ios), gst_object_unref);

  return TRUE;
}

static VkSurfaceKHR
gst_vulkan_window_ios_get_surface (GstVulkanWindow * window, GError ** error)
{
  GstVulkanWindowIos *window_ios = GST_VULKAN_WINDOW_IOS (window);
  VkIOSSurfaceCreateInfoMVK info = { 0, };
  VkSurfaceKHR ret;
  VkResult err;

  info.sType = VK_STRUCTURE_TYPE_IOS_SURFACE_CREATE_INFO_MVK;
  info.pNext = NULL;
  info.flags = 0;
  info.pView = window_ios->priv->internal_view;

  if (!window_ios->CreateIOSSurface)
    window_ios->CreateIOSSurface =
        gst_vulkan_instance_get_proc_address (window->display->instance,
        "vkCreateIOSSurfaceMVK");
  if (!window_ios->CreateIOSSurface) {
    g_set_error_literal (error, GST_VULKAN_ERROR, VK_ERROR_FEATURE_NOT_PRESENT,
        "Could not retrieve \"vkCreateIOSSurfaceMVK\" function pointer");
    return 0;
  }

  err =
      window_ios->CreateIOSSurface (window->display->instance->instance, &info,
      NULL, &ret);
  if (gst_vulkan_error_to_g_error (err, error, "vkCreateIOSSurfaceMVK") < 0)
    return 0;

  return ret;
}

static gboolean
gst_vulkan_window_ios_get_presentation_support (GstVulkanWindow * window,
    GstVulkanDevice * device, guint32 queue_family_idx)
{
  return TRUE;
}

static gboolean
gst_vulkan_window_ios_open (GstVulkanWindow * window, GError ** error)
{
  GstVulkanWindowIos *window_ios = GST_VULKAN_WINDOW_IOS (window);

  if (!GST_VULKAN_WINDOW_CLASS (parent_class)->open (window, error))
    return FALSE;

  return gst_vulkan_window_ios_create_window (window_ios);
}

static void
gst_vulkan_window_ios_close (GstVulkanWindow * window)
{
  GstVulkanWindowIos *window_ios = GST_VULKAN_WINDOW_IOS (window);

  CFBridgingRelease (window_ios->priv->internal_view);
  window_ios->priv->internal_view = NULL;

  GST_VULKAN_WINDOW_CLASS (parent_class)->close (window);
}

static void
gst_vulkan_window_ios_set_window_handle (GstVulkanWindow * window,
    guintptr window_handle)
{
  GstVulkanWindowIos *window_ios = GST_VULKAN_WINDOW_IOS (window);
  gpointer view = (gpointer) window_handle;

  g_return_if_fail (view != NULL);

  if (window_ios->priv->external_view && window_ios->priv->external_view != view) {
    GST_FIXME_OBJECT (window_ios, "View changes are not implemented");
    return;
  }

  window_ios->priv->external_view = view;
}

@implementation GstVulkanUIView

+(Class) layerClass
{
  return [CAMetalLayer class];
}

@end

void
_invoke_on_main (GstVulkanWindowFunc func, gpointer data, GDestroyNotify notify)
{
  if ([NSThread isMainThread]) {
    func (data);
    if (notify)
      notify (data);
  } else {
    dispatch_async (dispatch_get_main_queue (), ^{
      func (data);
      if (notify)
        notify (data);
    });
  }
}

