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

#include <stdint.h>
#include <stdlib.h>

#include "xcb_event_source.h"
#include "vkdisplay_xcb.h"
#include "vkwindow_xcb.h"

static gint
_compare_xcb_window (GstVulkanWindowXCB * window_xcb, xcb_window_t * window_id)
{
  return window_xcb->win_id - *window_id;
}

static GstVulkanWindowXCB *
_find_window_from_xcb_window (GstVulkanDisplayXCB * display_xcb,
    xcb_window_t window_id)
{
  GstVulkanDisplay *display = GST_VULKAN_DISPLAY (display_xcb);
  GstVulkanWindowXCB *ret = NULL;
  GList *l;

  if (!window_id)
    return NULL;

  GST_OBJECT_LOCK (display);
  l = g_list_find_custom (display->windows, &window_id,
      (GCompareFunc) _compare_xcb_window);
  if (l)
    ret = gst_object_ref (l->data);
  GST_OBJECT_UNLOCK (display);

  return ret;
}

static gboolean
_xcb_handle_event (GstVulkanDisplayXCB * display_xcb)
{
  xcb_connection_t *connection =
      GST_VULKAN_DISPLAY_XCB_CONNECTION (display_xcb);
  xcb_generic_event_t *event;
  gboolean ret = TRUE;

  while ((event = xcb_poll_for_event (connection))) {
    uint8_t event_code = event->response_type & 0x7f;

    switch (event_code) {
      case XCB_CLIENT_MESSAGE:{
        xcb_client_message_event_t *client_event;
        xcb_intern_atom_cookie_t cookie;
        xcb_intern_atom_reply_t *reply;

        client_event = (xcb_client_message_event_t *) event;
        cookie = xcb_intern_atom (connection, 0, 16, "WM_DELETE_WINDOW");
        reply = xcb_intern_atom_reply (connection, cookie, 0);

        if (client_event->data.data32[0] == reply->atom) {
          GstVulkanWindowXCB *window_xcb;

          window_xcb =
              _find_window_from_xcb_window (display_xcb, client_event->window);

          if (window_xcb) {
            GST_INFO_OBJECT (window_xcb, "Close requested");

            gst_vulkan_window_close (GST_VULKAN_WINDOW (window_xcb));
            gst_vulkan_display_remove_window (GST_VULKAN_DISPLAY (display_xcb),
                GST_VULKAN_WINDOW (window_xcb));
            gst_object_unref (window_xcb);
          }
        }

        g_free (reply);
        break;
      }
      case XCB_CONFIGURE_NOTIFY:{
        xcb_configure_notify_event_t *configure_event;
        GstVulkanWindowXCB *window_xcb;

        configure_event = (xcb_configure_notify_event_t *) event;
        window_xcb =
            _find_window_from_xcb_window (display_xcb, configure_event->window);

        if (window_xcb) {
          gst_vulkan_window_resize (GST_VULKAN_WINDOW (window_xcb),
              configure_event->width, configure_event->height);

          gst_object_unref (window_xcb);
        }
        break;
      }
      case XCB_EXPOSE:{
        xcb_expose_event_t *expose_event = (xcb_expose_event_t *) event;
        GstVulkanWindowXCB *window_xcb;

        /* non-zero means that other Expose follows
         * so just wait for the last one
         * in theory we should not receive non-zero because
         * we have no sub areas here but just in case */
        if (expose_event->count != 0)
          break;

        window_xcb =
            _find_window_from_xcb_window (display_xcb, expose_event->window);

        if (window_xcb) {
          gst_vulkan_window_redraw (GST_VULKAN_WINDOW (window_xcb));
          gst_object_unref (window_xcb);
        }
        break;
      }
#if 0
      case KeyPress:
      case KeyRelease:
        keysym = XkbKeycodeToKeysym (window_xcb->device,
            event.xkey.keycode, 0, 0);
        key_str = XKeysymToString (keysym);
        key_data = g_slice_new (struct key_event);
        key_data->window = window;
        key_data->key_str = XKeysymToString (keysym);
        key_data->event_type =
            event.type == KeyPress ? "key-press" : "key-release";
        GST_DEBUG ("input event key %d pressed over window at %d,%d (%s)",
            event.xkey.keycode, event.xkey.x, event.xkey.y, key_str);
        g_main_context_invoke (window->navigation_context,
            (GSourceFunc) gst_vulkan_window_key_event_cb, key_data);
        break;
      case ButtonPress:
      case ButtonRelease:
        GST_DEBUG ("input event mouse button %d pressed over window at %d,%d",
            event.xbutton.button, event.xbutton.x, event.xbutton.y);
        mouse_data = g_slice_new (struct mouse_event);
        mouse_data->window = window;
        mouse_data->event_type =
            event.type ==
            ButtonPress ? "mouse-button-press" : "mouse-button-release";
        mouse_data->button = event.xbutton.button;
        mouse_data->posx = (double) event.xbutton.x;
        mouse_data->posy = (double) event.xbutton.y;

        g_main_context_invoke (window->navigation_context,
            (GSourceFunc) gst_vulkan_window_mouse_event_cb, mouse_data);
        break;
      case MotionNotify:
        GST_DEBUG ("input event pointer moved over window at %d,%d",
            event.xmotion.x, event.xmotion.y);
        mouse_data = g_slice_new (struct mouse_event);
        mouse_data->window = window;
        mouse_data->event_type = "mouse-move";
        mouse_data->button = 0;
        mouse_data->posx = (double) event.xbutton.x;
        mouse_data->posy = (double) event.xbutton.y;

        g_main_context_invoke (window->navigation_context, (GSourceFunc)
            gst_vulkan_window_mouse_event_cb, mouse_data);
        break;
#endif
      default:
        GST_DEBUG ("unhandled XCB type: %u", event_code);
        break;
    }

    g_free (event);
  }

  return ret;
}

typedef struct _XCBEventSource
{
  GSource source;
  GPollFD pfd;
  uint32_t mask;
  GstVulkanDisplayXCB *display_xcb;
} XCBEventSource;

static gboolean
xcb_event_source_prepare (GSource * base, gint * timeout)
{
  *timeout = -1;
  return FALSE;
}

static gboolean
xcb_event_source_check (GSource * base)
{
  XCBEventSource *source = (XCBEventSource *) base;
  gboolean retval;

  retval = source->pfd.revents;

  return retval;
}

static gboolean
xcb_event_source_dispatch (GSource * base, GSourceFunc callback, gpointer data)
{
  XCBEventSource *source = (XCBEventSource *) base;

  gboolean ret = _xcb_handle_event (source->display_xcb);

  if (callback)
    callback (data);

  return ret;
}

static GSourceFuncs xcb_event_source_funcs = {
  xcb_event_source_prepare,
  xcb_event_source_check,
  xcb_event_source_dispatch,
  NULL
};

GSource *
xcb_event_source_new (GstVulkanDisplayXCB * display_xcb)
{
  xcb_connection_t *connection;
  XCBEventSource *source;

  connection = GST_VULKAN_DISPLAY_XCB_CONNECTION (display_xcb);
  g_return_val_if_fail (connection != NULL, NULL);

  source = (XCBEventSource *)
      g_source_new (&xcb_event_source_funcs, sizeof (XCBEventSource));
  source->display_xcb = display_xcb;
  source->pfd.fd = xcb_get_file_descriptor (connection);
  source->pfd.events = G_IO_IN | G_IO_ERR;
  g_source_add_poll (&source->source, &source->pfd);

  return &source->source;
}
