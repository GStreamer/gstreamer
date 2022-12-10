/*
 *  gstvaapisink.c - VA-API video sink
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2011-2014 Intel Corporation
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

/**
 * SECTION:element-vaapisink
 * @short_description: A VA-API based video sink
 *
 * vaapisink renders video frames to a drawable (X Window) on a local
 * display using the Video Acceleration (VA) API. The element will
 * create its own internal window and render into it.
 *
 * ## Example launch line
 *
 * |[
 * gst-launch-1.0 videotestsrc ! vaapisink
 * ]|
 */

#include "gstcompat.h"
#include <gst/gst.h>
#include <gst/video/video.h>

#include <gst/vaapi/gstvaapivalue.h>

/* Supported interfaces */
# include <gst/video/videooverlay.h>
# include <gst/video/colorbalance.h>
# include <gst/video/navigation.h>

#include "gstvaapisink.h"
#include "gstvaapipluginutil.h"
#include "gstvaapivideometa.h"
#include "gstvaapivideobufferpool.h"
#include "gstvaapivideomemory.h"

#define GST_PLUGIN_NAME "vaapisink"
#define GST_PLUGIN_DESC "A VA-API based videosink"

GST_DEBUG_CATEGORY_STATIC (gst_debug_vaapisink);
#ifndef GST_DISABLE_GST_DEBUG
#define GST_CAT_DEFAULT gst_debug_vaapisink
#else
#define GST_CAT_DEFAULT NULL
#endif

/* Default template */
/* *INDENT-OFF* */
static const char gst_vaapisink_sink_caps_str[] =
    GST_VAAPI_MAKE_SURFACE_CAPS ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (
        GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE ","
            GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
        "{ ENCODED, NV12, I420, YV12, P010_10LE }") ";"
    GST_VIDEO_CAPS_MAKE_WITH_FEATURES (
        GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
        GST_VIDEO_FORMATS_ALL) ";"
    GST_VIDEO_CAPS_MAKE (GST_VIDEO_FORMATS_ALL);
/* *INDENT-ON* */

static GstStaticPadTemplate gst_vaapisink_sink_factory =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (gst_vaapisink_sink_caps_str));

static gboolean
gst_vaapisink_has_interface (GstVaapiPluginBase * plugin, GType type)
{
  return type == GST_TYPE_VIDEO_OVERLAY || type == GST_TYPE_COLOR_BALANCE;
}

static void
gst_vaapisink_video_overlay_iface_init (GstVideoOverlayInterface * iface);

static void
gst_vaapisink_color_balance_iface_init (GstColorBalanceInterface * iface);

static void
gst_vaapisink_navigation_iface_init (GstNavigationInterface * iface);

G_DEFINE_TYPE_WITH_CODE (GstVaapiSink,
    gst_vaapisink,
    GST_TYPE_VIDEO_SINK,
    GST_VAAPI_PLUGIN_BASE_INIT_INTERFACES
    G_IMPLEMENT_INTERFACE (GST_TYPE_VIDEO_OVERLAY,
        gst_vaapisink_video_overlay_iface_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_COLOR_BALANCE,
        gst_vaapisink_color_balance_iface_init);
    G_IMPLEMENT_INTERFACE (GST_TYPE_NAVIGATION,
        gst_vaapisink_navigation_iface_init));

GST_VAAPI_PLUGIN_BASE_DEFINE_SET_CONTEXT (gst_vaapisink_parent_class);

enum
{
  HANDOFF_SIGNAL,
  LAST_SIGNAL
};

static guint gst_vaapisink_signals[LAST_SIGNAL] = { 0 };

enum
{
  PROP_0,

  PROP_DISPLAY_TYPE,
  PROP_DISPLAY_NAME,
  PROP_FULLSCREEN,
  PROP_ROTATION,
  PROP_FORCE_ASPECT_RATIO,
  PROP_VIEW_ID,
  PROP_HUE,
  PROP_SATURATION,
  PROP_BRIGHTNESS,
  PROP_CONTRAST,
  PROP_SIGNAL_HANDOFFS,

  N_PROPERTIES
};

#define DEFAULT_DISPLAY_TYPE            GST_VAAPI_DISPLAY_TYPE_ANY
#define DEFAULT_ROTATION                GST_VAAPI_ROTATION_0
#define DEFAULT_SIGNAL_HANDOFFS         FALSE

static GParamSpec *g_properties[N_PROPERTIES] = { NULL, };

static void gst_vaapisink_video_overlay_expose (GstVideoOverlay * overlay);

static gboolean gst_vaapisink_reconfigure_window (GstVaapiSink * sink);

static void
gst_vaapisink_set_event_handling (GstVaapiSink * sink, gboolean handle_events);

static GstFlowReturn
gst_vaapisink_show_frame (GstVideoSink * video_sink, GstBuffer * buffer);

static gboolean
gst_vaapisink_ensure_render_rect (GstVaapiSink * sink, guint width,
    guint height);

static inline gboolean
gst_vaapisink_ensure_display (GstVaapiSink * sink)
{
  return gst_vaapi_plugin_base_ensure_display (GST_VAAPI_PLUGIN_BASE (sink));
}

static inline gboolean
gst_vaapisink_render_surface (GstVaapiSink * sink, GstVaapiSurface * surface,
    const GstVaapiRectangle * surface_rect, guint flags)
{
  return sink->window && gst_vaapi_window_put_surface (sink->window, surface,
      surface_rect, &sink->display_rect, flags);
}

/* ------------------------------------------------------------------------ */
/* --- DRM Backend                                                      --- */
/* ------------------------------------------------------------------------ */

#if GST_VAAPI_USE_DRM
#include <gst/vaapi/gstvaapidisplay_drm.h>

static gboolean
gst_vaapisink_drm_create_window (GstVaapiSink * sink, guint width, guint height)
{
  g_return_val_if_fail (sink->window == NULL, FALSE);

  GST_ERROR ("failed to create a window for VA/DRM display");
  return FALSE;
}

static gboolean
gst_vaapisink_drm_render_surface (GstVaapiSink * sink,
    GstVaapiSurface * surface, const GstVaapiRectangle * surface_rect,
    guint flags)
{
  return TRUE;
}

static const inline GstVaapiSinkBackend *
gst_vaapisink_backend_drm (void)
{
  static const GstVaapiSinkBackend GstVaapiSinkBackendDRM = {
    .create_window = gst_vaapisink_drm_create_window,
    .render_surface = gst_vaapisink_drm_render_surface,
  };
  return &GstVaapiSinkBackendDRM;
}
#endif

/* ------------------------------------------------------------------------ */
/* --- X11 Backend                                                      --- */
/* ------------------------------------------------------------------------ */

#if GST_VAAPI_USE_X11
#include <gst/vaapi/gstvaapidisplay_x11.h>
#include <gst/vaapi/gstvaapiwindow_x11.h>

#if HAVE_XKBLIB
# include <X11/XKBlib.h>
#endif

static inline KeySym
x11_keycode_to_keysym (Display * dpy, unsigned int kc)
{
#if HAVE_XKBLIB
  return XkbKeycodeToKeysym (dpy, kc, 0, 0);
#else
  return XKeycodeToKeysym (dpy, kc, 0);
#endif
}

/* Checks whether a ConfigureNotify event is in the queue */
typedef struct _ConfigureNotifyEventPendingArgs ConfigureNotifyEventPendingArgs;
struct _ConfigureNotifyEventPendingArgs
{
  Window window;
  guint width;
  guint height;
  gboolean match;
};

static Bool
configure_notify_event_pending_cb (Display * dpy, XEvent * xev, XPointer arg)
{
  ConfigureNotifyEventPendingArgs *const args =
      (ConfigureNotifyEventPendingArgs *) arg;

  if (xev->type == ConfigureNotify &&
      xev->xconfigure.window == args->window &&
      xev->xconfigure.width == args->width &&
      xev->xconfigure.height == args->height)
    args->match = TRUE;

  /* XXX: this is a hack to traverse the whole queue because we
     can't use XPeekIfEvent() since it could block */
  return False;
}

static gboolean
configure_notify_event_pending (GstVaapiSink * sink, Window window,
    guint width, guint height)
{
  GstVaapiDisplayX11 *const display =
      GST_VAAPI_DISPLAY_X11 (GST_VAAPI_PLUGIN_BASE_DISPLAY (sink));
  ConfigureNotifyEventPendingArgs args;
  XEvent xev;

  args.window = window;
  args.width = width;
  args.height = height;
  args.match = FALSE;

  /* XXX: don't use XPeekIfEvent() because it might block */
  XCheckIfEvent (gst_vaapi_display_x11_get_display (display),
      &xev, configure_notify_event_pending_cb, (XPointer) & args);
  return args.match;
}

static gboolean
gst_vaapisink_x11_create_window (GstVaapiSink * sink, guint width, guint height)
{
  GstVaapiDisplay *const display = GST_VAAPI_PLUGIN_BASE_DISPLAY (sink);

  g_return_val_if_fail (sink->window == NULL, FALSE);

  sink->window = gst_vaapi_window_x11_new (display, width, height);
  if (!sink->window)
    return FALSE;

  gst_video_overlay_got_window_handle (GST_VIDEO_OVERLAY (sink),
      gst_vaapi_window_x11_get_xid (GST_VAAPI_WINDOW_X11 (sink->window)));
  return TRUE;
}

static gboolean
gst_vaapisink_x11_create_window_from_handle (GstVaapiSink * sink,
    guintptr window)
{
  GstVaapiDisplay *display;
  Window rootwin;
  unsigned int width, height, border_width, depth;
  int x, y;
  XID xid = window;

  if (!gst_vaapisink_ensure_display (sink))
    return FALSE;
  display = GST_VAAPI_PLUGIN_BASE_DISPLAY (sink);

  gst_vaapi_display_lock (display);
  XGetGeometry (gst_vaapi_display_x11_get_display (GST_VAAPI_DISPLAY_X11
          (display)), xid, &rootwin, &x, &y, &width, &height, &border_width,
      &depth);
  gst_vaapi_display_unlock (display);

  if ((width != sink->window_width || height != sink->window_height) &&
      !configure_notify_event_pending (sink, xid, width, height)) {
    if (!gst_vaapisink_ensure_render_rect (sink, width, height))
      return FALSE;
    sink->window_width = width;
    sink->window_height = height;
  }

  if (!sink->window
      || gst_vaapi_window_x11_get_xid (GST_VAAPI_WINDOW_X11 (sink->window)) !=
      xid) {
    gst_vaapi_window_replace (&sink->window, NULL);
    sink->window = gst_vaapi_window_x11_new_with_xid (display, xid);
    if (!sink->window)
      return FALSE;
  }

  gst_vaapisink_set_event_handling (sink, sink->handle_events);
  return TRUE;
}

static gboolean
gst_vaapisink_x11_handle_events (GstVaapiSink * sink)
{
  GstVaapiDisplay *const display = GST_VAAPI_PLUGIN_BASE_DISPLAY (sink);
  gboolean has_events, do_expose = FALSE;
  guint pointer_x = 0, pointer_y = 0;
  gboolean pointer_moved = FALSE;
  XEvent e;

  if (sink->window) {
    Display *const x11_dpy =
        gst_vaapi_display_x11_get_display (GST_VAAPI_DISPLAY_X11 (display));
    Window x11_win =
        gst_vaapi_window_x11_get_xid (GST_VAAPI_WINDOW_X11 (sink->window));

    /* Track MousePointer interaction */
    for (;;) {
      gst_vaapi_display_lock (display);
      has_events = XCheckWindowEvent (x11_dpy, x11_win, PointerMotionMask, &e);
      gst_vaapi_display_unlock (display);
      if (!has_events)
        break;

      switch (e.type) {
        case MotionNotify:
          pointer_x = e.xmotion.x;
          pointer_y = e.xmotion.y;
          pointer_moved = TRUE;
          break;
        default:
          break;
      }
    }
    if (pointer_moved) {
      gst_vaapi_display_lock (display);
      gst_navigation_send_mouse_event (GST_NAVIGATION (sink),
          "mouse-move", 0, pointer_x, pointer_y);
      gst_vaapi_display_unlock (display);
    }

    /* Track KeyPress, KeyRelease, ButtonPress, ButtonRelease */
    for (;;) {
      KeySym keysym;
      const char *key_str = NULL;
      gst_vaapi_display_lock (display);
      has_events = XCheckWindowEvent (x11_dpy, x11_win,
          KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask,
          &e);
      gst_vaapi_display_unlock (display);
      if (!has_events)
        break;

      switch (e.type) {
        case ButtonPress:
          gst_navigation_send_mouse_event (GST_NAVIGATION (sink),
              "mouse-button-press", e.xbutton.button, e.xbutton.x, e.xbutton.y);
          break;
        case ButtonRelease:
          gst_navigation_send_mouse_event (GST_NAVIGATION (sink),
              "mouse-button-release", e.xbutton.button, e.xbutton.x,
              e.xbutton.y);
          break;
        case KeyPress:
        case KeyRelease:
          gst_vaapi_display_lock (display);
          keysym = x11_keycode_to_keysym (x11_dpy, e.xkey.keycode);
          if (keysym != NoSymbol) {
            key_str = XKeysymToString (keysym);
          } else {
            key_str = "unknown";
          }
          gst_vaapi_display_unlock (display);
          gst_navigation_send_key_event (GST_NAVIGATION (sink),
              e.type == KeyPress ? "key-press" : "key-release", key_str);
          break;
        default:
          break;
      }
    }

    /* Handle Expose + ConfigureNotify */
    /* Need to lock whole loop or we corrupt the XEvent queue: */
    for (;;) {
      gst_vaapi_display_lock (display);
      has_events = XCheckWindowEvent (x11_dpy, x11_win,
          StructureNotifyMask | ExposureMask, &e);
      gst_vaapi_display_unlock (display);
      if (!has_events)
        break;

      switch (e.type) {
        case Expose:
          do_expose = TRUE;
          break;
        case ConfigureNotify:
          if (gst_vaapisink_reconfigure_window (sink))
            do_expose = TRUE;
          break;
        default:
          break;
      }
    }
    if (do_expose)
      gst_vaapisink_video_overlay_expose (GST_VIDEO_OVERLAY (sink));

    /* Handle Display events */
    for (;;) {
      gst_vaapi_display_lock (display);
      if (XPending (x11_dpy) == 0) {
        gst_vaapi_display_unlock (display);
        break;
      }
      XNextEvent (x11_dpy, &e);
      gst_vaapi_display_unlock (display);

      switch (e.type) {
        case ClientMessage:{
          Atom wm_delete;

          wm_delete = XInternAtom (x11_dpy, "WM_DELETE_WINDOW", False);
          if (wm_delete == (Atom) e.xclient.data.l[0]) {
            /* Handle window deletion by posting an error on the bus */
            GST_ELEMENT_ERROR (sink, RESOURCE, NOT_FOUND,
                ("Output window was closed"), (NULL));
            return FALSE;
          }
          break;
        }
        default:
          break;
      }
    }

  }
  return TRUE;
}

static gboolean
gst_vaapisink_x11_pre_start_event_thread (GstVaapiSink * sink)
{
  GstVaapiDisplayX11 *const display =
      GST_VAAPI_DISPLAY_X11 (GST_VAAPI_PLUGIN_BASE_DISPLAY (sink));
  int x11_event_mask = (KeyPressMask | KeyReleaseMask |
      PointerMotionMask | ExposureMask | StructureNotifyMask);

  if (!sink->foreign_window)
    x11_event_mask |= ButtonPressMask | ButtonReleaseMask;

  if (sink->window) {
    gst_vaapi_display_lock (GST_VAAPI_DISPLAY (display));
    XSelectInput (gst_vaapi_display_x11_get_display (display),
        gst_vaapi_window_x11_get_xid (GST_VAAPI_WINDOW_X11 (sink->window)),
        x11_event_mask);
    gst_vaapi_display_unlock (GST_VAAPI_DISPLAY (display));
  }
  return TRUE;
}

static gboolean
gst_vaapisink_x11_pre_stop_event_thread (GstVaapiSink * sink)
{
  GstVaapiDisplayX11 *const display =
      GST_VAAPI_DISPLAY_X11 (GST_VAAPI_PLUGIN_BASE_DISPLAY (sink));

  if (sink->window) {
    gst_vaapi_display_lock (GST_VAAPI_DISPLAY (display));
    XSelectInput (gst_vaapi_display_x11_get_display (display),
        gst_vaapi_window_x11_get_xid (GST_VAAPI_WINDOW_X11 (sink->window)), 0);
    gst_vaapi_display_unlock (GST_VAAPI_DISPLAY (display));
  }
  return TRUE;
}

static const inline GstVaapiSinkBackend *
gst_vaapisink_backend_x11 (void)
{
  static const GstVaapiSinkBackend GstVaapiSinkBackendX11 = {
    .create_window = gst_vaapisink_x11_create_window,
    .create_window_from_handle = gst_vaapisink_x11_create_window_from_handle,
    .render_surface = gst_vaapisink_render_surface,

    .event_thread_needed = TRUE,
    .handle_events = gst_vaapisink_x11_handle_events,
    .pre_start_event_thread = gst_vaapisink_x11_pre_start_event_thread,
    .pre_stop_event_thread = gst_vaapisink_x11_pre_stop_event_thread,
  };
  return &GstVaapiSinkBackendX11;
}
#endif

/* ------------------------------------------------------------------------ */
/* --- Wayland Backend                                                  --- */
/* ------------------------------------------------------------------------ */

#if GST_VAAPI_USE_WAYLAND
#include <gst/vaapi/gstvaapidisplay_wayland.h>
#include <gst/vaapi/gstvaapiwindow_wayland.h>

static void
on_window_wayland_size_changed (GstVaapiWindowWayland * window, gint width,
    gint height, gpointer user_data)
{
  GstVaapiSink *sink = GST_VAAPISINK (user_data);

  GST_DEBUG ("Wayland window size changed to: %dx%d", width, height);
  gst_vaapisink_reconfigure_window (sink);
  gst_vaapisink_show_frame (GST_VIDEO_SINK_CAST (sink), NULL);
}

static gboolean
gst_vaapisink_wayland_create_window (GstVaapiSink * sink, guint width,
    guint height)
{
  GstVaapiDisplay *const display = GST_VAAPI_PLUGIN_BASE_DISPLAY (sink);

  g_return_val_if_fail (sink->window == NULL, FALSE);

  sink->window = gst_vaapi_window_wayland_new (display, width, height);
  if (!sink->window)
    return FALSE;

  g_signal_connect_object (sink->window, "size-changed",
      G_CALLBACK (on_window_wayland_size_changed), sink, 0);

  return TRUE;
}

static gboolean
gst_vaapisink_wayland_create_window_from_handle (GstVaapiSink * sink,
    guintptr window)
{
  GstVaapiDisplay *display;

  if (!gst_vaapisink_ensure_display (sink))
    return FALSE;
  display = GST_VAAPI_PLUGIN_BASE_DISPLAY (sink);

  if (sink->window == NULL || (guintptr) sink->window != window) {
    gst_vaapi_window_replace (&sink->window, NULL);
    sink->window = gst_vaapi_window_wayland_new_with_surface (display, window);
  }

  return sink->window != NULL;
}

static const inline GstVaapiSinkBackend *
gst_vaapisink_backend_wayland (void)
{
  static const GstVaapiSinkBackend GstVaapiSinkBackendWayland = {
    .create_window = gst_vaapisink_wayland_create_window,
    .create_window_from_handle =
        gst_vaapisink_wayland_create_window_from_handle,
    .render_surface = gst_vaapisink_render_surface,
  };
  return &GstVaapiSinkBackendWayland;
}
#endif

/* ------------------------------------------------------------------------ */
/* --- GstVideoOverlay interface                                        --- */
/* ------------------------------------------------------------------------ */

static void
gst_vaapisink_video_overlay_set_window_handle (GstVideoOverlay * overlay,
    guintptr window)
{
  GstVaapiSink *const sink = GST_VAAPISINK (overlay);
  GstVaapiDisplayType display_type;

  if (!gst_vaapisink_ensure_display (sink))
    return;

  display_type = GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE (sink);

  /* Disable GLX rendering when vaapisink is using a foreign X
     window. It's pretty much useless */
  if (display_type == GST_VAAPI_DISPLAY_TYPE_GLX) {
    display_type = GST_VAAPI_DISPLAY_TYPE_X11;
    gst_vaapi_plugin_base_set_display_type (GST_VAAPI_PLUGIN_BASE (sink),
        display_type);
  }

  sink->foreign_window = TRUE;
  if (sink->backend->create_window_from_handle)
    sink->backend->create_window_from_handle (sink, window);
}

static void
gst_vaapisink_video_overlay_set_render_rectangle (GstVideoOverlay * overlay,
    gint x, gint y, gint width, gint height)
{
  GstVaapiSink *const sink = GST_VAAPISINK (overlay);
  GstVaapiRectangle *const display_rect = &sink->display_rect;

  display_rect->x = x;
  display_rect->y = y;
  display_rect->width = width;
  display_rect->height = height;

  if (gst_vaapisink_ensure_render_rect (sink, width, height) && sink->window) {
    gst_vaapi_window_set_render_rectangle (sink->window, x, y, width, height);
    gst_vaapi_window_set_size (sink->window, width, height);
    gst_vaapisink_reconfigure_window (sink);
  }

  GST_DEBUG ("render rect (%d,%d):%ux%u",
      display_rect->x, display_rect->y,
      display_rect->width, display_rect->height);
}

static void
gst_vaapisink_video_overlay_expose (GstVideoOverlay * overlay)
{
  GstVaapiSink *const sink = GST_VAAPISINK (overlay);

  gst_vaapisink_reconfigure_window (sink);
  gst_vaapisink_show_frame (GST_VIDEO_SINK_CAST (sink), NULL);
}

static void
gst_vaapisink_video_overlay_set_event_handling (GstVideoOverlay * overlay,
    gboolean handle_events)
{
  GstVaapiSink *const sink = GST_VAAPISINK (overlay);

  sink->handle_events = handle_events;
  gst_vaapisink_set_event_handling (sink, handle_events);
}

static void
gst_vaapisink_video_overlay_iface_init (GstVideoOverlayInterface * iface)
{
  iface->set_window_handle = gst_vaapisink_video_overlay_set_window_handle;
  iface->set_render_rectangle =
      gst_vaapisink_video_overlay_set_render_rectangle;
  iface->expose = gst_vaapisink_video_overlay_expose;
  iface->handle_events = gst_vaapisink_video_overlay_set_event_handling;
}

/* ------------------------------------------------------------------------ */
/* --- GstColorBalance interface                                        --- */
/* ------------------------------------------------------------------------ */

enum
{
  CB_HUE = 1,
  CB_SATURATION,
  CB_BRIGHTNESS,
  CB_CONTRAST
};

typedef struct
{
  guint cb_id;
  const gchar *prop_name;
  const gchar *channel_name;
} ColorBalanceMap;

static const ColorBalanceMap cb_map[4] = {
  {CB_HUE, GST_VAAPI_DISPLAY_PROP_HUE, "VA_HUE"},
  {CB_SATURATION, GST_VAAPI_DISPLAY_PROP_SATURATION, "VA_SATURATION"},
  {CB_BRIGHTNESS, GST_VAAPI_DISPLAY_PROP_BRIGHTNESS, "VA_BRIGHTNESS"},
  {CB_CONTRAST, GST_VAAPI_DISPLAY_PROP_CONTRAST, "VA_CONTRAST"}
};

static guint
cb_get_id_from_channel_name (GstVaapiSink * sink, const gchar * name)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (sink->cb_values); i++) {
    if (g_ascii_strcasecmp (cb_map[i].channel_name, name) == 0)
      return cb_map[i].cb_id;
  }

  GST_WARNING ("got an unknown channel %s", name);
  return 0;
}

static inline GValue *
cb_get_gvalue (GstVaapiSink * sink, guint id)
{
  g_return_val_if_fail ((guint) (id - CB_HUE) < G_N_ELEMENTS (sink->cb_values),
      NULL);

  return &sink->cb_values[id - CB_HUE];
}

static gboolean
cb_set_value (GstVaapiSink * sink, guint id, gfloat value)
{
  GValue *const v_value = cb_get_gvalue (sink, id);

  if (!v_value)
    return FALSE;

  g_value_set_float (v_value, value);
  sink->cb_changed |= (1U << id);
  return TRUE;
}

static inline gfloat
cb_get_value (GstVaapiSink * sink, guint id)
{
  const GValue *const v_value = cb_get_gvalue (sink, id);

  return v_value ? g_value_get_float (v_value) : 0.0;
}

static gboolean
cb_sync_values_from_display (GstVaapiSink * sink, GstVaapiDisplay * display)
{
  guint i;
  gfloat value;

  for (i = 0; i < G_N_ELEMENTS (sink->cb_values); i++) {
    const guint cb_id = CB_HUE + i;
    if (!gst_vaapi_display_has_property (display, cb_map[i].prop_name)) {
      GST_INFO_OBJECT (sink, "backend does not handle %s", cb_map[i].prop_name);
      continue;
    }

    value = 0.0;
    g_object_get (display, cb_map[i].prop_name, &value, NULL);
    cb_set_value (sink, cb_id, value);
  }
  sink->cb_changed = 0;
  return TRUE;
}

static gboolean
cb_sync_values_to_display (GstVaapiSink * sink, GstVaapiDisplay * display)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (sink->cb_values); i++) {
    const guint cb_id = CB_HUE + i;
    if (!(sink->cb_changed & (1U << cb_id)))
      continue;
    if (!gst_vaapi_display_has_property (display, cb_map[i].prop_name)) {
      GST_INFO_OBJECT (sink, "backend does not handle %s", cb_map[i].prop_name);
      continue;
    }

    g_object_set_property (G_OBJECT (display), cb_map[i].prop_name,
        cb_get_gvalue (sink, cb_id));
  }
  sink->cb_changed = 0;
  return TRUE;
}

#define CB_CHANNEL_FACTOR (1000.0)

static void
cb_channels_init (GstVaapiSink * sink)
{
  GstVaapiDisplay *const display = GST_VAAPI_PLUGIN_BASE_DISPLAY (sink);
  GstColorBalanceChannel *channel;
  GParamSpecFloat *pspec;
  guint i;

  for (i = 0; i < G_N_ELEMENTS (sink->cb_values); i++) {
    if (!gst_vaapi_display_has_property (display, cb_map[i].prop_name))
      continue;

    pspec = G_PARAM_SPEC_FLOAT (g_properties[PROP_HUE + i]);
    if (!pspec)
      continue;

    channel = g_object_new (GST_TYPE_COLOR_BALANCE_CHANNEL, NULL);
    channel->label = g_strdup (cb_map[i].channel_name);
    channel->min_value = pspec->minimum * CB_CHANNEL_FACTOR;
    channel->max_value = pspec->maximum * CB_CHANNEL_FACTOR;

    sink->cb_channels = g_list_prepend (sink->cb_channels, channel);
  }

  if (sink->cb_channels)
    sink->cb_channels = g_list_reverse (sink->cb_channels);
}

static void
cb_channels_finalize (GstVaapiSink * sink)
{
  if (sink->cb_channels) {
    g_list_free_full (sink->cb_channels, g_object_unref);
    sink->cb_channels = NULL;
  }
}

static const GList *
gst_vaapisink_color_balance_list_channels (GstColorBalance * cb)
{
  GstVaapiSink *const sink = GST_VAAPISINK (cb);

  if (!gst_vaapisink_ensure_display (sink))
    return NULL;

  if (!sink->cb_channels)
    cb_channels_init (sink);
  return sink->cb_channels;
}

static void
gst_vaapisink_color_balance_set_value (GstColorBalance * cb,
    GstColorBalanceChannel * channel, gint value)
{
  GstVaapiSink *const sink = GST_VAAPISINK (cb);
  guint cb_id;

  g_return_if_fail (channel->label != NULL);

  if (!gst_vaapisink_ensure_display (sink))
    return;

  cb_id = cb_get_id_from_channel_name (sink, channel->label);
  if (!cb_id)
    return;

  cb_set_value (sink, cb_id, value / CB_CHANNEL_FACTOR);
}

static gint
gst_vaapisink_color_balance_get_value (GstColorBalance * cb,
    GstColorBalanceChannel * channel)
{
  GstVaapiSink *const sink = GST_VAAPISINK (cb);
  guint cb_id;

  g_return_val_if_fail (channel->label != NULL, 0);

  if (!gst_vaapisink_ensure_display (sink))
    return 0;

  cb_id = cb_get_id_from_channel_name (sink, channel->label);
  if (!cb_id)
    return 0;

  return cb_get_value (sink, cb_id) * CB_CHANNEL_FACTOR;
}

static GstColorBalanceType
gst_vaapisink_color_balance_get_type (GstColorBalance * cb)
{
  return GST_COLOR_BALANCE_HARDWARE;
}

static void
gst_vaapisink_color_balance_iface_init (GstColorBalanceInterface * iface)
{
  iface->list_channels = gst_vaapisink_color_balance_list_channels;
  iface->set_value = gst_vaapisink_color_balance_set_value;
  iface->get_value = gst_vaapisink_color_balance_get_value;
  iface->get_balance_type = gst_vaapisink_color_balance_get_type;
}

/* ------------------------------------------------------------------------ */
/* --- GstNavigation interface                                          --- */
/* ------------------------------------------------------------------------ */

static void
gst_vaapisink_navigation_send_event (GstNavigation * navigation,
    GstEvent * event)
{
  GstVaapiSink *const sink = GST_VAAPISINK (navigation);
  GstPad *peer;

  if (!sink->window) {
    gst_event_unref (event);
    return;
  }

  if ((peer = gst_pad_get_peer (GST_VAAPI_PLUGIN_BASE_SINK_PAD (sink)))) {
    GstVaapiRectangle *disp_rect = &sink->display_rect;
    gdouble x, y, xscale = 1.0, yscale = 1.0;

    /* We calculate scaling using the original video frames geometry to include
       pixel aspect ratio scaling. */
    xscale = (gdouble) sink->video_width / disp_rect->width;
    yscale = (gdouble) sink->video_height / disp_rect->height;

    event = gst_event_make_writable (event);

    /* Converting pointer coordinates to the non scaled geometry */
    if (gst_navigation_event_get_coordinates (event, &x, &y)) {
      x = MIN (x, disp_rect->x + disp_rect->width);
      x = MAX (x - disp_rect->x, 0);
      y = MIN (y, disp_rect->y + disp_rect->height);
      y = MAX (y - disp_rect->y, 0);
      gst_navigation_event_set_coordinates (event, x * xscale, y * yscale);
    }

    if (!gst_pad_send_event (peer, gst_event_ref (event))) {
      /* If upstream didn't handle the event we'll post a message with it
       * for the application in case it wants to do something with it */
      gst_element_post_message (GST_ELEMENT_CAST (sink),
          gst_navigation_message_new_event (GST_OBJECT_CAST (sink), event));
    }
    gst_event_unref (event);
    gst_object_unref (peer);
  }
}

static void
gst_vaapisink_navigation_iface_init (GstNavigationInterface * iface)
{
  iface->send_event_simple = gst_vaapisink_navigation_send_event;
}

/* ------------------------------------------------------------------------ */
/* --- Common implementation                                            --- */
/* ------------------------------------------------------------------------ */

static gboolean
gst_vaapisink_reconfigure_window (GstVaapiSink * sink)
{
  guint win_width, win_height;

  gst_vaapi_window_reconfigure (sink->window);
  gst_vaapi_window_get_size (sink->window, &win_width, &win_height);
  if (win_width != sink->window_width || win_height != sink->window_height) {
    if (!gst_vaapisink_ensure_render_rect (sink, win_width, win_height))
      return FALSE;
    GST_INFO ("window was resized from %ux%u to %ux%u",
        sink->window_width, sink->window_height, win_width, win_height);
    sink->window_width = win_width;
    sink->window_height = win_height;
    return TRUE;
  }
  return FALSE;
}

static gpointer
gst_vaapisink_event_thread (GstVaapiSink * sink)
{
  GST_OBJECT_LOCK (sink);
  while (!g_atomic_int_get (&sink->event_thread_cancel)) {
    GST_OBJECT_UNLOCK (sink);
    sink->backend->handle_events (sink);
    g_usleep (G_USEC_PER_SEC / 20);
    GST_OBJECT_LOCK (sink);
  }
  GST_OBJECT_UNLOCK (sink);
  return NULL;
}

static void
gst_vaapisink_set_event_handling (GstVaapiSink * sink, gboolean handle_events)
{
  GThread *thread = NULL;

  if (!sink->backend || !sink->backend->event_thread_needed)
    return;

  GST_OBJECT_LOCK (sink);
  if (handle_events && !sink->event_thread) {
    /* Setup our event listening thread */
    GST_DEBUG ("starting xevent thread");
    if (sink->backend->pre_start_event_thread)
      sink->backend->pre_start_event_thread (sink);

    g_atomic_int_set (&sink->event_thread_cancel, FALSE);
    sink->event_thread = g_thread_try_new ("vaapisink-events",
        (GThreadFunc) gst_vaapisink_event_thread, sink, NULL);
  } else if (!handle_events && sink->event_thread) {
    GST_DEBUG ("stopping xevent thread");
    if (sink->backend->pre_stop_event_thread)
      sink->backend->pre_stop_event_thread (sink);

    /* Grab thread and mark it as NULL */
    thread = sink->event_thread;
    sink->event_thread = NULL;
    g_atomic_int_set (&sink->event_thread_cancel, TRUE);
  }
  GST_OBJECT_UNLOCK (sink);

  /* Wait for our event thread to finish */
  if (thread) {
    g_thread_join (thread);
    GST_DEBUG ("xevent thread stopped");
  }
}

static void
gst_vaapisink_ensure_backend (GstVaapiSink * sink)
{
  switch (GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE (sink)) {
#if GST_VAAPI_USE_DRM
    case GST_VAAPI_DISPLAY_TYPE_DRM:
      sink->backend = gst_vaapisink_backend_drm ();
      break;
#endif
#if GST_VAAPI_USE_X11
    case GST_VAAPI_DISPLAY_TYPE_X11:
      sink->backend = gst_vaapisink_backend_x11 ();
      break;
#endif
#if GST_VAAPI_USE_GLX
    case GST_VAAPI_DISPLAY_TYPE_GLX:
      sink->backend = gst_vaapisink_backend_x11 ();
      break;
#endif
#if GST_VAAPI_USE_WAYLAND
    case GST_VAAPI_DISPLAY_TYPE_WAYLAND:
      sink->backend = gst_vaapisink_backend_wayland ();
      break;
#endif
    default:
      GST_ERROR ("failed to initialize GstVaapiSink backend");
      g_assert_not_reached ();
      break;
  }
}

static gboolean
gst_vaapisink_ensure_render_rect (GstVaapiSink * sink, guint width,
    guint height)
{
  GstVaapiRectangle *const display_rect = &sink->display_rect;
  guint num, den, display_par_n, display_par_d;
  gboolean success;

  /* Return success if caps are not set yet */
  if (!sink->caps)
    return TRUE;

  if (!sink->keep_aspect) {
    display_rect->width = width;
    display_rect->height = height;
    display_rect->x = 0;
    display_rect->y = 0;

    GST_DEBUG ("force-aspect-ratio is false; distorting while scaling video");
    GST_DEBUG ("render rect (%d,%d):%ux%u",
        display_rect->x, display_rect->y,
        display_rect->width, display_rect->height);
    return TRUE;
  }

  GST_DEBUG ("ensure render rect within %ux%u bounds", width, height);

  gst_vaapi_display_get_pixel_aspect_ratio (GST_VAAPI_PLUGIN_BASE_DISPLAY
      (sink), &display_par_n, &display_par_d);
  GST_DEBUG ("display pixel-aspect-ratio %d/%d", display_par_n, display_par_d);

  success = gst_video_calculate_display_ratio (&num, &den,
      sink->video_width, sink->video_height,
      sink->video_par_n, sink->video_par_d, display_par_n, display_par_d);
  if (!success)
    return FALSE;
  GST_DEBUG ("video size %dx%d, calculated ratio %d/%d",
      sink->video_width, sink->video_height, num, den);

  display_rect->width = gst_util_uint64_scale_int (height, num, den);
  if (display_rect->width <= width) {
    GST_DEBUG ("keeping window height");
    display_rect->height = height;
  } else {
    GST_DEBUG ("keeping window width");
    display_rect->width = width;
    display_rect->height = gst_util_uint64_scale_int (width, den, num);
  }
  GST_DEBUG ("scaling video to %ux%u", display_rect->width,
      display_rect->height);

  g_assert (display_rect->width <= width);
  g_assert (display_rect->height <= height);

  display_rect->x = (width - display_rect->width) / 2;
  display_rect->y = (height - display_rect->height) / 2;

  GST_DEBUG ("render rect (%d,%d):%ux%u",
      display_rect->x, display_rect->y,
      display_rect->width, display_rect->height);
  return TRUE;
}

static inline gboolean
gst_vaapisink_ensure_window (GstVaapiSink * sink, guint width, guint height)
{
  return sink->window || sink->backend->create_window (sink, width, height);
}

static void
gst_vaapisink_ensure_window_size (GstVaapiSink * sink, guint * width_ptr,
    guint * height_ptr)
{
  GstVaapiDisplay *const display = GST_VAAPI_PLUGIN_BASE_DISPLAY (sink);
  GstVideoRectangle src_rect, dst_rect, out_rect;
  guint num, den, display_width, display_height, display_par_n, display_par_d;
  gboolean success, scale;

  if (sink->foreign_window) {
    *width_ptr = sink->window_width;
    *height_ptr = sink->window_height;
    return;
  }

  gst_vaapi_display_get_size (display, &display_width, &display_height);
  if (sink->fullscreen) {
    *width_ptr = display_width;
    *height_ptr = display_height;
    return;
  }

  gst_vaapi_display_get_pixel_aspect_ratio (display,
      &display_par_n, &display_par_d);

  success = gst_video_calculate_display_ratio (&num, &den,
      sink->video_width, sink->video_height,
      sink->video_par_n, sink->video_par_d, display_par_n, display_par_d);
  if (!success) {
    num = sink->video_par_n;
    den = sink->video_par_d;
  }

  src_rect.x = 0;
  src_rect.y = 0;
  src_rect.w = gst_util_uint64_scale_int (sink->video_height, num, den);
  src_rect.h = sink->video_height;
  dst_rect.x = 0;
  dst_rect.y = 0;
  dst_rect.w = display_width;
  dst_rect.h = display_height;
  scale = (src_rect.w > dst_rect.w || src_rect.h > dst_rect.h);
  gst_video_sink_center_rect (src_rect, dst_rect, &out_rect, scale);
  *width_ptr = out_rect.w;
  *height_ptr = out_rect.h;
}

static inline gboolean
gst_vaapisink_ensure_colorbalance (GstVaapiSink * sink)
{
  return cb_sync_values_to_display (sink, GST_VAAPI_PLUGIN_BASE_DISPLAY (sink));
}


static void
gst_vaapisink_set_rotation (GstVaapiSink * sink, GstVaapiRotation rotation,
    gboolean from_tag)
{
  GST_OBJECT_LOCK (sink);

  if (from_tag)
    sink->rotation_tag = rotation;
  else
    sink->rotation_prop = rotation;

  if (sink->rotation_prop == GST_VAAPI_ROTATION_AUTOMATIC)
    sink->rotation_req = sink->rotation_tag;
  else
    sink->rotation_req = sink->rotation_prop;

  GST_OBJECT_UNLOCK (sink);
}

static gboolean
gst_vaapisink_ensure_rotation (GstVaapiSink * sink,
    gboolean recalc_display_rect)
{
  GstVaapiDisplay *const display = GST_VAAPI_PLUGIN_BASE_DISPLAY (sink);
  gboolean success = FALSE;

  g_return_val_if_fail (display, FALSE);

  if (sink->rotation == sink->rotation_req)
    return TRUE;

  if (!sink->use_rotation) {
    GST_WARNING ("VA display does not support rotation");
    goto end;
  }

  gst_vaapi_display_lock (display);
  success = gst_vaapi_display_set_rotation (display, sink->rotation_req);
  gst_vaapi_display_unlock (display);
  if (!success) {
    GST_ERROR ("failed to change VA display rotation mode");
    goto end;
  }

  if (((sink->rotation + sink->rotation_req) % 180) == 90) {
    /* Orientation changed */
    G_PRIMITIVE_SWAP (guint, sink->video_width, sink->video_height);
    G_PRIMITIVE_SWAP (gint, sink->video_par_n, sink->video_par_d);
  }

  if (recalc_display_rect && !sink->foreign_window)
    gst_vaapisink_ensure_render_rect (sink, sink->window_width,
        sink->window_height);
  success = TRUE;

end:
  sink->rotation = sink->rotation_req;
  return success;
}

static const gchar *
get_display_type_name (GstVaapiDisplayType display_type)
{
  gpointer const klass = g_type_class_peek (GST_VAAPI_TYPE_DISPLAY_TYPE);
  GEnumValue *const e = g_enum_get_value (klass, display_type);

  if (e)
    return e->value_name;
  return "<unknown-type>";
}

static void
gst_vaapisink_display_changed (GstVaapiPluginBase * plugin)
{
  GstVaapiSink *const sink = GST_VAAPISINK_CAST (plugin);
  GstVaapiRenderMode render_mode;

  GST_INFO ("created %s %p", get_display_type_name (plugin->display_type),
      plugin->display);

  gst_vaapisink_ensure_backend (sink);

  sink->use_overlay =
      gst_vaapi_display_get_render_mode (plugin->display, &render_mode) &&
      render_mode == GST_VAAPI_RENDER_MODE_OVERLAY;
  GST_DEBUG ("use %s rendering mode",
      sink->use_overlay ? "overlay" : "texture");

  /* Keep our own colorbalance values, should we have any change pending */
  if (!sink->cb_changed)
    cb_sync_values_from_display (sink, plugin->display);

  sink->use_rotation = gst_vaapi_display_has_property (plugin->display,
      GST_VAAPI_DISPLAY_PROP_ROTATION);
}

static gboolean
gst_vaapisink_start (GstBaseSink * base_sink)
{
  GstVaapiSink *const sink = GST_VAAPISINK_CAST (base_sink);
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (base_sink);

  if (!gst_vaapisink_ensure_display (sink))
    return FALSE;

  /* Ensures possible raw caps earlier to avoid race conditions at
   * get_caps() */
  if (!gst_vaapi_plugin_base_get_allowed_sinkpad_raw_caps (plugin))
    return FALSE;

  return TRUE;
}

static gboolean
gst_vaapisink_stop (GstBaseSink * base_sink)
{
  GstVaapiSink *const sink = GST_VAAPISINK_CAST (base_sink);

  gst_vaapisink_set_event_handling (sink, FALSE);
  gst_buffer_replace (&sink->video_buffer, NULL);
  gst_vaapi_window_replace (&sink->window, NULL);

  gst_vaapi_plugin_base_close (GST_VAAPI_PLUGIN_BASE (sink));
  return TRUE;
}

static GstCaps *
gst_vaapisink_get_caps_impl (GstBaseSink * base_sink)
{
  GstVaapiSink *const sink = GST_VAAPISINK_CAST (base_sink);
  GstCaps *out_caps, *raw_caps, *feature_caps;
  static const char surface_caps_str[] =
      GST_VAAPI_MAKE_SURFACE_CAPS ";"
      GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_VAAPI_SURFACE
      "," GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION,
      "{ ENCODED, NV12, I420, YV12 }");
  GstCapsFeatures *features;

  if (!GST_VAAPI_PLUGIN_BASE_DISPLAY (sink))
    return gst_static_pad_template_get_caps (&gst_vaapisink_sink_factory);

  out_caps = gst_caps_from_string (surface_caps_str);
  raw_caps =
      gst_vaapi_plugin_base_get_allowed_sinkpad_raw_caps (GST_VAAPI_PLUGIN_BASE
      (sink));
  if (!raw_caps)
    return out_caps;

  out_caps = gst_caps_make_writable (out_caps);
  gst_caps_append (out_caps, gst_caps_copy (raw_caps));

  feature_caps = gst_caps_copy (raw_caps);
  features = gst_caps_features_new
      (GST_CAPS_FEATURE_META_GST_VIDEO_OVERLAY_COMPOSITION, NULL);
  gst_caps_set_features (feature_caps, 0, features);
  gst_caps_append (out_caps, feature_caps);

  return out_caps;
}

static inline GstCaps *
gst_vaapisink_get_caps (GstBaseSink * base_sink, GstCaps * filter)
{
  GstCaps *caps, *out_caps;

  caps = gst_vaapisink_get_caps_impl (base_sink);
  if (caps && filter) {
    out_caps = gst_caps_intersect_full (caps, filter, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (caps);
  } else
    out_caps = caps;
  return out_caps;
}

static void
update_colorimetry (GstVaapiSink * sink, GstVideoColorimetry * cinfo)
{
  if (gst_video_colorimetry_matches (cinfo, GST_VIDEO_COLORIMETRY_BT601))
    sink->color_standard = GST_VAAPI_COLOR_STANDARD_ITUR_BT_601;
  else if (gst_video_colorimetry_matches (cinfo, GST_VIDEO_COLORIMETRY_BT709))
    sink->color_standard = GST_VAAPI_COLOR_STANDARD_ITUR_BT_709;
  else if (gst_video_colorimetry_matches (cinfo,
          GST_VIDEO_COLORIMETRY_SMPTE240M))
    sink->color_standard = GST_VAAPI_COLOR_STANDARD_SMPTE_240M;
  else
    sink->color_standard = 0;

#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *const colorimetry_string = gst_video_colorimetry_to_string (cinfo);
    GST_DEBUG ("colorimetry %s", colorimetry_string);
    g_free (colorimetry_string);
  }
#endif
}

static gboolean
gst_vaapisink_set_caps (GstBaseSink * base_sink, GstCaps * caps)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (base_sink);
  GstVaapiSink *const sink = GST_VAAPISINK_CAST (base_sink);
  GstVideoInfo *const vip = GST_VAAPI_PLUGIN_BASE_SINK_PAD_INFO (sink);
  GstVaapiDisplay *display;
  guint win_width, win_height;

  if (!gst_vaapisink_ensure_display (sink))
    return FALSE;
  display = GST_VAAPI_PLUGIN_BASE_DISPLAY (sink);

  if (!gst_vaapi_plugin_base_set_caps (plugin, caps, NULL))
    return FALSE;

  sink->video_width = GST_VIDEO_INFO_WIDTH (vip);
  sink->video_height = GST_VIDEO_INFO_HEIGHT (vip);
  sink->video_par_n = GST_VIDEO_INFO_PAR_N (vip);
  sink->video_par_d = GST_VIDEO_INFO_PAR_D (vip);
  if (sink->video_par_n == 0)
    sink->video_par_n = 1;
  GST_DEBUG ("video pixel-aspect-ratio %d/%d",
      sink->video_par_n, sink->video_par_d);

  update_colorimetry (sink, &GST_VIDEO_INFO_COLORIMETRY (vip));
  gst_caps_replace (&sink->caps, caps);

  /* Reset the rotation to the default when new caps are coming in. This
   * forces re-evaluating if the rotation needs to be done */
  sink->rotation = DEFAULT_ROTATION;
  gst_vaapisink_ensure_colorbalance (sink);
  gst_vaapisink_ensure_rotation (sink, FALSE);

  if (GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE (sink) == GST_VAAPI_DISPLAY_TYPE_DRM)
    return TRUE;

  gst_vaapisink_ensure_window_size (sink, &win_width, &win_height);
  if (sink->window) {
    if (!sink->foreign_window || sink->fullscreen)
      gst_vaapi_window_set_size (sink->window, win_width, win_height);
  } else {
    gst_vaapi_display_lock (display);
    gst_video_overlay_prepare_window_handle (GST_VIDEO_OVERLAY (sink));
    gst_vaapi_display_unlock (display);
    if (sink->window)
      return TRUE;
    if (!gst_vaapisink_ensure_window (sink, win_width, win_height))
      return FALSE;
    gst_vaapi_window_set_fullscreen (sink->window, sink->fullscreen);
    gst_vaapi_window_show (sink->window);
    gst_vaapi_window_get_size (sink->window, &win_width, &win_height);
    gst_vaapisink_set_event_handling (sink, sink->handle_events);
  }
  sink->window_width = win_width;
  sink->window_height = win_height;
  GST_DEBUG ("window size %ux%u", win_width, win_height);

  return gst_vaapisink_ensure_render_rect (sink, win_width, win_height);
}

static GstFlowReturn
gst_vaapisink_show_frame_unlocked (GstVaapiSink * sink, GstBuffer * src_buffer)
{
  GstVaapiVideoMeta *meta;
  GstVaapiSurfaceProxy *proxy;
  GstVaapiSurface *surface;
  GstBuffer *buffer;
  GstBuffer *old_buf;
  guint flags;
  GstVaapiRectangle *surface_rect = NULL;
  GstVaapiRectangle tmp_rect;
  GstFlowReturn ret;
  gint32 view_id;
  GstVideoCropMeta *crop_meta;

  if (!src_buffer) {
    if (sink->video_buffer)
      src_buffer = sink->video_buffer;
    else
      return GST_FLOW_OK;
  }

  crop_meta = gst_buffer_get_video_crop_meta (src_buffer);
  if (crop_meta) {
    surface_rect = &tmp_rect;
    surface_rect->x = crop_meta->x;
    surface_rect->y = crop_meta->y;
    surface_rect->width = crop_meta->width;
    surface_rect->height = crop_meta->height;
  }

  ret = gst_vaapi_plugin_base_get_input_buffer (GST_VAAPI_PLUGIN_BASE (sink),
      src_buffer, &buffer);
  if (ret == GST_FLOW_NOT_SUPPORTED)
    return GST_FLOW_OK;         /* let's ignore the frame if it couldn't be uploaded */
  if (ret != GST_FLOW_OK)
    return ret;

  meta = gst_buffer_get_vaapi_video_meta (buffer);
  if (gst_vaapi_video_meta_get_display (meta) !=
      GST_VAAPI_PLUGIN_BASE_DISPLAY (sink))
    goto different_display;

  proxy = gst_vaapi_video_meta_get_surface_proxy (meta);
  if (!proxy)
    goto no_surface;

  surface = gst_vaapi_video_meta_get_surface (meta);
  if (!surface)
    goto no_surface;

  /* Validate view component to display */
  view_id = GST_VAAPI_SURFACE_PROXY_VIEW_ID (proxy);
  if (G_UNLIKELY (sink->view_id == -1))
    sink->view_id = view_id;
  else if (sink->view_id != view_id) {
    ret = GST_FLOW_OK;
    goto done;
  }

  gst_vaapisink_ensure_colorbalance (sink);
  gst_vaapisink_ensure_rotation (sink, TRUE);

  GST_TRACE_OBJECT (sink, "render surface %" GST_VAAPI_ID_FORMAT,
      GST_VAAPI_ID_ARGS (gst_vaapi_surface_get_id (surface)));

  if (!surface_rect)
    surface_rect = (GstVaapiRectangle *)
        gst_vaapi_video_meta_get_render_rect (meta);

  if (surface_rect)
    GST_DEBUG ("render rect (%d,%d), size %ux%u",
        surface_rect->x, surface_rect->y,
        surface_rect->width, surface_rect->height);

  flags = gst_vaapi_video_meta_get_render_flags (meta);

  /* Append default color standard obtained from caps if none was
     available on a per-buffer basis */
  if (!(flags & GST_VAAPI_COLOR_STANDARD_MASK))
    flags |= sink->color_standard;

  if (!gst_vaapi_apply_composition (surface, src_buffer))
    GST_WARNING ("could not update subtitles");

  if (!sink->backend->render_surface (sink, surface, surface_rect, flags))
    goto error;

  if (sink->signal_handoffs)
    g_signal_emit (sink, gst_vaapisink_signals[HANDOFF_SIGNAL], 0, buffer);

  /* Retain VA surface until the next one is displayed */
  old_buf = sink->video_buffer;
  sink->video_buffer = gst_buffer_ref (buffer);
  /* Need to release the lock while releasing old buffer, otherwise a
   * deadlock is possible */
  gst_vaapi_display_unlock (GST_VAAPI_PLUGIN_BASE_DISPLAY (sink));
  if (old_buf)
    gst_buffer_unref (old_buf);
  gst_vaapi_display_lock (GST_VAAPI_PLUGIN_BASE_DISPLAY (sink));

  ret = GST_FLOW_OK;

done:
  gst_buffer_unref (buffer);
  return ret;

  /* ERRORS */
error:
  {
    GST_ELEMENT_ERROR (sink, RESOURCE, WRITE,
        ("Internal error: could not render surface"), (NULL));
    ret = GST_FLOW_ERROR;
    goto done;
  }

no_surface:
  {
    /* No surface or surface proxy. That's very bad! */
    GST_WARNING_OBJECT (sink, "could not get surface");
    ret = GST_FLOW_ERROR;
    goto done;
  }

different_display:
  {
    GST_WARNING_OBJECT (sink, "incoming surface has different VAAPI Display");
    ret = GST_FLOW_ERROR;
    goto done;
  }
}

static GstFlowReturn
gst_vaapisink_show_frame (GstVideoSink * video_sink, GstBuffer * src_buffer)
{
  GstVaapiSink *const sink = GST_VAAPISINK_CAST (video_sink);
  GstFlowReturn ret;

  /* We need at least to protect the gst_vaapi_aplpy_composition()
   * call to prevent a race during subpicture destruction.
   * FIXME: a less coarse grained lock could be used, though */
  gst_vaapi_display_lock (GST_VAAPI_PLUGIN_BASE_DISPLAY (sink));
  ret = gst_vaapisink_show_frame_unlocked (sink, src_buffer);
  gst_vaapi_display_unlock (GST_VAAPI_PLUGIN_BASE_DISPLAY (sink));

  return ret;
}

static gboolean
gst_vaapisink_propose_allocation (GstBaseSink * base_sink, GstQuery * query)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (base_sink);

  if (!gst_vaapi_plugin_base_propose_allocation (plugin, query))
    return FALSE;

  gst_query_add_allocation_meta (query, GST_VIDEO_CROP_META_API_TYPE, NULL);
  gst_query_add_allocation_meta (query,
      GST_VIDEO_OVERLAY_COMPOSITION_META_API_TYPE, NULL);
  return TRUE;
}

static gboolean
gst_vaapisink_query (GstBaseSink * base_sink, GstQuery * query)
{
  GstElement *const element = GST_ELEMENT (base_sink);
  gboolean ret = FALSE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
      ret = gst_vaapi_handle_context_query (element, query);
      break;
    default:
      ret = GST_BASE_SINK_CLASS (gst_vaapisink_parent_class)->query (base_sink,
          query);
      break;
  }

  return ret;
}

static void
gst_vaapisink_destroy (GstVaapiSink * sink)
{
  cb_channels_finalize (sink);
  gst_buffer_replace (&sink->video_buffer, NULL);
  gst_caps_replace (&sink->caps, NULL);
}

static void
gst_vaapisink_finalize (GObject * object)
{
  gst_vaapisink_destroy (GST_VAAPISINK_CAST (object));

  gst_vaapi_plugin_base_finalize (GST_VAAPI_PLUGIN_BASE (object));
  G_OBJECT_CLASS (gst_vaapisink_parent_class)->finalize (object);
}

static void
gst_vaapisink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec)
{
  GstVaapiSink *const sink = GST_VAAPISINK_CAST (object);

  switch (prop_id) {
    case PROP_DISPLAY_TYPE:
      gst_vaapi_plugin_base_set_display_type (GST_VAAPI_PLUGIN_BASE (sink),
          g_value_get_enum (value));
      break;
    case PROP_DISPLAY_NAME:
      gst_vaapi_plugin_base_set_display_name (GST_VAAPI_PLUGIN_BASE (sink),
          g_value_get_string (value));
      break;
    case PROP_FULLSCREEN:
      sink->fullscreen = g_value_get_boolean (value);
      break;
    case PROP_VIEW_ID:
      sink->view_id = g_value_get_int (value);
      break;
    case PROP_ROTATION:
      gst_vaapisink_set_rotation (sink, g_value_get_enum (value), FALSE);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      sink->keep_aspect = g_value_get_boolean (value);
      break;
    case PROP_SIGNAL_HANDOFFS:
      sink->signal_handoffs = g_value_get_boolean (value);
      break;
    case PROP_HUE:
    case PROP_SATURATION:
    case PROP_BRIGHTNESS:
    case PROP_CONTRAST:
      cb_set_value (sink, (prop_id - PROP_HUE) + CB_HUE,
          g_value_get_float (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_vaapisink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec)
{
  GstVaapiSink *const sink = GST_VAAPISINK_CAST (object);

  switch (prop_id) {
    case PROP_DISPLAY_TYPE:
      g_value_set_enum (value, GST_VAAPI_PLUGIN_BASE_DISPLAY_TYPE (sink));
      break;
    case PROP_DISPLAY_NAME:
      g_value_set_string (value, GST_VAAPI_PLUGIN_BASE_DISPLAY_NAME (sink));
      break;
    case PROP_FULLSCREEN:
      g_value_set_boolean (value, sink->fullscreen);
      break;
    case PROP_VIEW_ID:
      g_value_set_int (value, sink->view_id);
      break;
    case PROP_ROTATION:
      g_value_set_enum (value, sink->rotation);
      break;
    case PROP_FORCE_ASPECT_RATIO:
      g_value_set_boolean (value, sink->keep_aspect);
      break;
    case PROP_SIGNAL_HANDOFFS:
      g_value_set_boolean (value, sink->signal_handoffs);
      break;
    case PROP_HUE:
    case PROP_SATURATION:
    case PROP_BRIGHTNESS:
    case PROP_CONTRAST:
      g_value_set_float (value, cb_get_value (sink,
              (prop_id - PROP_HUE) + CB_HUE));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_vaapisink_unlock (GstBaseSink * base_sink)
{
  GstVaapiSink *const sink = GST_VAAPISINK_CAST (base_sink);

  if (sink->window)
    return gst_vaapi_window_unblock (sink->window);

  return TRUE;
}

static gboolean
gst_vaapisink_unlock_stop (GstBaseSink * base_sink)
{
  GstVaapiSink *const sink = GST_VAAPISINK_CAST (base_sink);

  if (sink->window)
    return gst_vaapi_window_unblock_cancel (sink->window);

  return TRUE;
}

static gboolean
gst_vaapisink_event (GstBaseSink * base_sink, GstEvent * event)
{
  gboolean res = TRUE;
  GstTagList *taglist;
  gchar *orientation;

  GstVaapiSink *const sink = GST_VAAPISINK_CAST (base_sink);

  GST_DEBUG_OBJECT (sink, "handling event %s", GST_EVENT_TYPE_NAME (event));

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_TAG:
      gst_event_parse_tag (event, &taglist);

      if (gst_tag_list_get_string (taglist, GST_TAG_IMAGE_ORIENTATION,
              &orientation)) {
        if (!g_strcmp0 ("rotate-0", orientation)) {
          gst_vaapisink_set_rotation (sink, GST_VAAPI_ROTATION_0, TRUE);
        } else if (!g_strcmp0 ("rotate-90", orientation)) {
          gst_vaapisink_set_rotation (sink, GST_VAAPI_ROTATION_90, TRUE);
        } else if (!g_strcmp0 ("rotate-180", orientation)) {
          gst_vaapisink_set_rotation (sink, GST_VAAPI_ROTATION_180, TRUE);
        } else if (!g_strcmp0 ("rotate-270", orientation)) {
          gst_vaapisink_set_rotation (sink, GST_VAAPI_ROTATION_270, TRUE);
        }

        /* Do not support for flip yet.
         * It should be implemented in the near future.
         * See https://bugs.freedesktop.org/show_bug.cgi?id=90654
         */
        g_free (orientation);
      }
      break;
    default:
      break;
  }

  res =
      GST_BASE_SINK_CLASS (gst_vaapisink_parent_class)->event (base_sink,
      event);

  return res;
}

static void
gst_vaapisink_class_init (GstVaapiSinkClass * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstElementClass *const element_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *const basesink_class = GST_BASE_SINK_CLASS (klass);
  GstVideoSinkClass *const videosink_class = GST_VIDEO_SINK_CLASS (klass);
  GstVaapiPluginBaseClass *const base_plugin_class =
      GST_VAAPI_PLUGIN_BASE_CLASS (klass);

  GST_DEBUG_CATEGORY_INIT (gst_debug_vaapisink,
      GST_PLUGIN_NAME, 0, GST_PLUGIN_DESC);

  gst_vaapi_plugin_base_class_init (base_plugin_class);
  base_plugin_class->has_interface = gst_vaapisink_has_interface;
  base_plugin_class->display_changed = gst_vaapisink_display_changed;

  object_class->finalize = gst_vaapisink_finalize;
  object_class->set_property = gst_vaapisink_set_property;
  object_class->get_property = gst_vaapisink_get_property;

  basesink_class->start = gst_vaapisink_start;
  basesink_class->stop = gst_vaapisink_stop;
  basesink_class->get_caps = gst_vaapisink_get_caps;
  basesink_class->set_caps = gst_vaapisink_set_caps;
  basesink_class->query = GST_DEBUG_FUNCPTR (gst_vaapisink_query);
  basesink_class->propose_allocation = gst_vaapisink_propose_allocation;
  basesink_class->unlock = gst_vaapisink_unlock;
  basesink_class->unlock_stop = gst_vaapisink_unlock_stop;
  basesink_class->event = gst_vaapisink_event;

  videosink_class->show_frame = GST_DEBUG_FUNCPTR (gst_vaapisink_show_frame);

  element_class->set_context = gst_vaapi_base_set_context;
  gst_element_class_set_static_metadata (element_class,
      "VA-API sink", "Sink/Video", GST_PLUGIN_DESC,
      "Gwenole Beauchesne <gwenole.beauchesne@intel.com>");

  gst_element_class_add_static_pad_template (element_class,
      &gst_vaapisink_sink_factory);

  /**
   * GstVaapiSink:display:
   *
   * The type of display to use.
   */
  g_properties[PROP_DISPLAY_TYPE] =
      g_param_spec_enum ("display",
      "display type",
      "display type to use",
      GST_VAAPI_TYPE_DISPLAY_TYPE,
      GST_VAAPI_DISPLAY_TYPE_ANY, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiSink:display-name:
   *
   * The native display name.
   */
  g_properties[PROP_DISPLAY_NAME] =
      g_param_spec_string ("display-name",
      "display name",
      "display name to use", NULL, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiSink:fullscreen:
   *
   * Selects whether fullscreen mode is enabled or not.
   */
  g_properties[PROP_FULLSCREEN] =
      g_param_spec_boolean ("fullscreen",
      "Fullscreen",
      "Requests window in fullscreen state",
      FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiSink:rotation:
   *
   * The VA display rotation mode, expressed as a #GstVaapiRotation.
   */
  g_properties[PROP_ROTATION] =
      g_param_spec_enum (GST_VAAPI_DISPLAY_PROP_ROTATION,
      "rotation",
      "The display rotation mode",
      GST_VAAPI_TYPE_ROTATION,
      DEFAULT_ROTATION, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiSink:force-aspect-ratio:
   *
   * When enabled, scaling respects video aspect ratio; when disabled,
   * the video is distorted to fit the window.
   */
  g_properties[PROP_FORCE_ASPECT_RATIO] =
      g_param_spec_boolean ("force-aspect-ratio",
      "Force aspect ratio",
      "When enabled, scaling will respect original aspect ratio",
      TRUE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiSink:signal-handoffs:
   *
   * Send a signal after rendering the buffer.
   */
  g_properties[PROP_SIGNAL_HANDOFFS] =
      g_param_spec_boolean ("signal-handoffs", "Signal handoffs",
      "Send a signal after rendering the buffer", DEFAULT_SIGNAL_HANDOFFS,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiSink:view-id:
   *
   * When not set to -1, the displayed frame will always be the one
   * that matches the view-id of the very first displayed frame. Any
   * other number will indicate the desire to display the supplied
   * view-id only.
   */
  g_properties[PROP_VIEW_ID] =
      g_param_spec_int ("view-id",
      "View ID",
      "ID of the view component of interest to display",
      -1, G_MAXINT32, -1, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  /**
   * GstVaapiSink:hue:
   *
   * The VA display hue, expressed as a float value. Range is -180.0
   * to 180.0. Default value is 0.0 and represents no modification.
   */
  g_properties[PROP_HUE] =
      g_param_spec_float (GST_VAAPI_DISPLAY_PROP_HUE,
      "hue", "The display hue value", -180.0, 180.0, 0.0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaapiSink:saturation:
   *
   * The VA display saturation, expressed as a float value. Range is
   * 0.0 to 2.0. Default value is 1.0 and represents no modification.
   */
  g_properties[PROP_SATURATION] =
      g_param_spec_float (GST_VAAPI_DISPLAY_PROP_SATURATION,
      "saturation",
      "The display saturation value", 0.0, 2.0, 1.0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaapiSink:brightness:
   *
   * The VA display brightness, expressed as a float value. Range is
   * -1.0 to 1.0. Default value is 0.0 and represents no modification.
   */
  g_properties[PROP_BRIGHTNESS] =
      g_param_spec_float (GST_VAAPI_DISPLAY_PROP_BRIGHTNESS,
      "brightness",
      "The display brightness value", -1.0, 1.0, 0.0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  /**
   * GstVaapiSink:contrast:
   *
   * The VA display contrast, expressed as a float value. Range is 0.0
   * to 2.0. Default value is 1.0 and represents no modification.
   */
  g_properties[PROP_CONTRAST] =
      g_param_spec_float (GST_VAAPI_DISPLAY_PROP_CONTRAST,
      "contrast",
      "The display contrast value", 0.0, 2.0, 1.0,
      G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS | G_PARAM_CONSTRUCT);

  g_object_class_install_properties (object_class, N_PROPERTIES, g_properties);

  /**
   * GstVaapiSink::handoff:
   * @object: the #GstVaapiSink instance
   * @buffer: the buffer that was rendered
   *
   * This signal gets emitted after rendering the frame.
   */
  gst_vaapisink_signals[HANDOFF_SIGNAL] =
      g_signal_new ("handoff", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, 0, NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_NONE, 1, GST_TYPE_BUFFER | G_SIGNAL_TYPE_STATIC_SCOPE);
}

static void
gst_vaapisink_init (GstVaapiSink * sink)
{
  GstVaapiPluginBase *const plugin = GST_VAAPI_PLUGIN_BASE (sink);
  guint i;

  gst_vaapi_plugin_base_init (plugin, GST_CAT_DEFAULT);
  gst_vaapi_plugin_base_set_display_type (plugin, DEFAULT_DISPLAY_TYPE);

  sink->video_par_n = 1;
  sink->video_par_d = 1;
  sink->view_id = -1;
  sink->handle_events = TRUE;
  sink->rotation = DEFAULT_ROTATION;
  sink->rotation_req = DEFAULT_ROTATION;
  sink->rotation_tag = DEFAULT_ROTATION;
  sink->keep_aspect = TRUE;
  sink->signal_handoffs = DEFAULT_SIGNAL_HANDOFFS;
  gst_video_info_init (&sink->video_info);

  for (i = 0; i < G_N_ELEMENTS (sink->cb_values); i++)
    g_value_init (&sink->cb_values[i], G_TYPE_FLOAT);
}
