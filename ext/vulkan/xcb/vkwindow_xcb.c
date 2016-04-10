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

#include <gst/gst.h>
#include <locale.h>

#include "vkwindow_xcb.h"
#include "vkdisplay_xcb.h"

#define GST_VULKAN_WINDOW_XCB_GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE((o), GST_TYPE_VULKAN_WINDOW_XCB, GstVulkanWindowXCBPrivate))

#define GST_CAT_DEFAULT gst_vulkan_window_xcb_debug
GST_DEBUG_CATEGORY_STATIC (GST_CAT_DEFAULT);

static void
_init_debug (void)
{
  static volatile gsize _init = 0;

  if (g_once_init_enter (&_init)) {
    GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT, "vulkanwindowxcb", 0,
        "Vulkan XCB Window");
    g_once_init_leave (&_init, 1);
  }
}

#define gst_vulkan_window_xcb_parent_class parent_class
G_DEFINE_TYPE_WITH_CODE (GstVulkanWindowXCB, gst_vulkan_window_xcb,
    GST_TYPE_VULKAN_WINDOW, _init_debug ());

gboolean gst_vulkan_window_xcb_handle_event (GstVulkanWindowXCB * window_xcb);

enum
{
  PROP_0,
};

struct _GstVulkanWindowXCBPrivate
{
  gboolean activate;
  gboolean activate_result;

  gint preferred_width;
  gint preferred_height;

  xcb_intern_atom_reply_t *atom_wm_delete_window;
};

static VkSurfaceKHR gst_vulkan_window_xcb_get_surface (GstVulkanWindow * window,
    GError ** error);
static gboolean gst_vulkan_window_xcb_get_presentation_support (GstVulkanWindow
    * window, GstVulkanDevice * device, guint32 queue_family_idx);
static gboolean gst_vulkan_window_xcb_open (GstVulkanWindow * window,
    GError ** error);
static void gst_vulkan_window_xcb_close (GstVulkanWindow * window);

static void
gst_vulkan_window_xcb_finalize (GObject * object)
{
  G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
gst_vulkan_window_xcb_class_init (GstVulkanWindowXCBClass * klass)
{
  GObjectClass *obj_class = G_OBJECT_CLASS (klass);
  GstVulkanWindowClass *window_class = (GstVulkanWindowClass *) klass;

  g_type_class_add_private (klass, sizeof (GstVulkanWindowXCBPrivate));

  obj_class->finalize = gst_vulkan_window_xcb_finalize;

  window_class->open = GST_DEBUG_FUNCPTR (gst_vulkan_window_xcb_open);
  window_class->close = GST_DEBUG_FUNCPTR (gst_vulkan_window_xcb_close);
  window_class->get_surface = gst_vulkan_window_xcb_get_surface;
  window_class->get_presentation_support =
      gst_vulkan_window_xcb_get_presentation_support;
}

static void
gst_vulkan_window_xcb_init (GstVulkanWindowXCB * window)
{
  window->priv = GST_VULKAN_WINDOW_XCB_GET_PRIVATE (window);
}

/* Must be called in the gl thread */
GstVulkanWindowXCB *
gst_vulkan_window_xcb_new (GstVulkanDisplay * display)
{
  _init_debug ();

  if ((gst_vulkan_display_get_handle_type (display) &
          GST_VULKAN_DISPLAY_TYPE_XCB)
      == GST_VULKAN_DISPLAY_TYPE_NONE) {
    GST_INFO ("Wrong display type %u for this window type %u", display->type,
        GST_VULKAN_DISPLAY_TYPE_XCB);
    return NULL;
  }

  return g_object_new (GST_TYPE_VULKAN_WINDOW_XCB, NULL);
}

static void
gst_vulkan_window_xcb_show (GstVulkanWindow * window)
{
  GstVulkanWindowXCB *window_xcb = GST_VULKAN_WINDOW_XCB (window);
  GstVulkanDisplayXCB *display_xcb = GST_VULKAN_DISPLAY_XCB (window->display);
  xcb_connection_t *connection =
      GST_VULKAN_DISPLAY_XCB_CONNECTION (display_xcb);

  if (!window_xcb->visible) {
    xcb_map_window (connection, window_xcb->win_id);
    xcb_flush (connection);
    window_xcb->visible = TRUE;
  }
}

static void
gst_vulkan_window_xcb_hide (GstVulkanWindow * window)
{
  GstVulkanWindowXCB *window_xcb = GST_VULKAN_WINDOW_XCB (window);
  GstVulkanDisplayXCB *display_xcb = GST_VULKAN_DISPLAY_XCB (window->display);
  xcb_connection_t *connection =
      GST_VULKAN_DISPLAY_XCB_CONNECTION (display_xcb);

  if (window_xcb->visible) {
    xcb_unmap_window (connection, window_xcb->win_id);
    window_xcb->visible = FALSE;
  }
}

gboolean
gst_vulkan_window_xcb_create_window (GstVulkanWindowXCB * window_xcb)
{
  GstVulkanDisplayXCB *display_xcb;
  xcb_connection_t *connection;
  xcb_screen_t *screen;
  xcb_window_t root_window;
  uint32_t value_mask, value_list[32];
  xcb_intern_atom_cookie_t cookie, cookie2;
  xcb_intern_atom_reply_t *reply, *reply2;
//  const gchar *title = "OpenGL renderer";
  gint x = 0, y = 0, width = 320, height = 240;

  display_xcb =
      GST_VULKAN_DISPLAY_XCB (GST_VULKAN_WINDOW (window_xcb)->display);
  connection = GST_VULKAN_DISPLAY_XCB_CONNECTION (display_xcb);
  root_window = GST_VULKAN_DISPLAY_XCB_ROOT_WINDOW (display_xcb);
  screen = GST_VULKAN_DISPLAY_XCB_SCREEN (display_xcb);

  window_xcb->win_id = xcb_generate_id (connection);

  value_mask = XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK;
  value_list[0] = screen->black_pixel;
  value_list[1] =
      XCB_EVENT_MASK_KEY_RELEASE | XCB_EVENT_MASK_EXPOSURE |
      XCB_EVENT_MASK_STRUCTURE_NOTIFY;

  xcb_create_window (connection, XCB_COPY_FROM_PARENT, window_xcb->win_id,
      root_window, x, y, width, height, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
      screen->root_visual, value_mask, value_list);

  GST_LOG_OBJECT (window_xcb, "gl window id: %p",
      (gpointer) (guintptr) window_xcb->win_id);
  GST_LOG_OBJECT (window_xcb, "gl window props: x:%d y:%d", x, y);

  /* Magic code that will send notification when window is destroyed */
  cookie = xcb_intern_atom (connection, 1, 12, "WM_PROTOCOLS");
  reply = xcb_intern_atom_reply (connection, cookie, 0);

  cookie2 = xcb_intern_atom (connection, 0, 16, "WM_DELETE_WINDOW");
  reply2 = xcb_intern_atom_reply (connection, cookie2, 0);

  xcb_change_property (connection, XCB_PROP_MODE_REPLACE, window_xcb->win_id,
      reply->atom, 4, 32, 1, &reply2->atom);
  g_free (reply);
  g_free (reply2);

  gst_vulkan_window_xcb_show (GST_VULKAN_WINDOW (window_xcb));

  return TRUE;
}

static VkSurfaceKHR
gst_vulkan_window_xcb_get_surface (GstVulkanWindow * window, GError ** error)
{
  GstVulkanWindowXCB *window_xcb = GST_VULKAN_WINDOW_XCB (window);
  VkXcbSurfaceCreateInfoKHR info = { 0, };
  VkSurfaceKHR ret;
  VkResult err;

  info.sType = VK_STRUCTURE_TYPE_XCB_SURFACE_CREATE_INFO_KHR;
  info.pNext = NULL;
  info.flags = 0;
  info.connection = GST_VULKAN_DISPLAY_XCB_CONNECTION (window->display);
  info.window = GST_VULKAN_WINDOW_XCB (window)->win_id;

  if (!window_xcb->CreateXcbSurface)
    window_xcb->CreateXcbSurface =
        gst_vulkan_instance_get_proc_address (window->display->instance,
        "vkCreateXcbSurfaceKHR");
  if (!window_xcb->CreateXcbSurface) {
    g_set_error_literal (error, GST_VULKAN_ERROR, VK_ERROR_FEATURE_NOT_PRESENT,
        "Could not retrieve \"vkCreateXcbSurfaceKHR\" function pointer");
    return NULL;
  }

  err =
      window_xcb->CreateXcbSurface (window->display->instance->instance, &info,
      NULL, &ret);
  if (gst_vulkan_error_to_g_error (err, error, "vkCreateXcbSurfaceKHR") < 0)
    return NULL;

  return ret;
}

static gboolean
gst_vulkan_window_xcb_get_presentation_support (GstVulkanWindow * window,
    GstVulkanDevice * device, guint32 queue_family_idx)
{
  GstVulkanWindowXCB *window_xcb = GST_VULKAN_WINDOW_XCB (window);
  VkPhysicalDevice gpu;
  xcb_screen_t *screen;

  screen = GST_VULKAN_DISPLAY_XCB_SCREEN (window->display);

  if (!window_xcb->GetPhysicalDeviceXcbPresentationSupport)
    window_xcb->GetPhysicalDeviceXcbPresentationSupport =
        gst_vulkan_instance_get_proc_address (window->display->instance,
        "vkGetPhysicalDeviceXcbPresentationSupportKHR");
  if (!window_xcb->GetPhysicalDeviceXcbPresentationSupport) {
    GST_WARNING_OBJECT (window, "Could not retrieve "
        "\"vkGetPhysicalDeviceXcbPresentationSupportKHR\" " "function pointer");
    return FALSE;
  }

  gpu = gst_vulkan_device_get_physical_device (device);
  if (window_xcb->GetPhysicalDeviceXcbPresentationSupport (gpu,
          queue_family_idx, GST_VULKAN_DISPLAY_XCB_CONNECTION (window->display),
          screen->root_visual))
    return TRUE;
  return FALSE;
}

static gboolean
gst_vulkan_window_xcb_open (GstVulkanWindow * window, GError ** error)
{
  GstVulkanWindowXCB *window_xcb = GST_VULKAN_WINDOW_XCB (window);
  GstVulkanDisplayXCB *display_xcb = (GstVulkanDisplayXCB *) window->display;
  xcb_connection_t *connection;

  connection = GST_VULKAN_DISPLAY_XCB_CONNECTION (display_xcb);
  if (connection == NULL) {
    g_set_error (error, GST_VULKAN_WINDOW_ERROR,
        GST_VULKAN_WINDOW_ERROR_RESOURCE_UNAVAILABLE,
        "Failed to connect to X display server with XCB");
    goto failure;
  }

  if (!GST_VULKAN_WINDOW_CLASS (parent_class)->open (window, error))
    return FALSE;

  return gst_vulkan_window_xcb_create_window (window_xcb);

failure:
  return FALSE;
}

static void
gst_vulkan_window_xcb_close (GstVulkanWindow * window)
{
  GstVulkanWindowXCB *window_xcb = GST_VULKAN_WINDOW_XCB (window);
  GstVulkanDisplayXCB *display_xcb = (GstVulkanDisplayXCB *) window->display;
  xcb_connection_t *connection =
      GST_VULKAN_DISPLAY_XCB_CONNECTION (display_xcb);

  if (connection) {
    gst_vulkan_window_xcb_hide (window);

    g_free (window_xcb->priv->atom_wm_delete_window);
    window_xcb->priv->atom_wm_delete_window = NULL;
    GST_DEBUG ("display receiver closed");
  }

  GST_VULKAN_WINDOW_CLASS (parent_class)->close (window);
}
