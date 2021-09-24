/*
 *  gstvaapiwindow_x11.c - VA/X11 window abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *    Author: Gwenole Beauchesne <gwenole.beauchesne@splitted-desktop.com>
 *  Copyright (C) 2012-2014 Intel Corporation
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
 * SECTION:gstvaapiwindow_x11
 * @short_description: VA/X11 window abstraction
 */

#include "sysdeps.h"
#include <X11/Xatom.h>
#include "gstvaapicompat.h"
#include "gstvaapiwindow_x11.h"
#include "gstvaapiwindow_x11_priv.h"
#include "gstvaapidisplay_x11.h"
#include "gstvaapidisplay_x11_priv.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_x11.h"

GST_DEBUG_CATEGORY_EXTERN (gst_debug_vaapi_window);
#define GST_CAT_DEFAULT gst_debug_vaapi_window

#define GST_VAAPI_WINDOW_X11_GET_CLASS(obj) \
   (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VAAPI_WINDOW_X11, GstVaapiWindowX11Class))

G_DEFINE_TYPE_WITH_PRIVATE (GstVaapiWindowX11, gst_vaapi_window_x11,
    GST_TYPE_VAAPI_WINDOW);

#define _NET_WM_STATE_REMOVE    0       /* remove/unset property */
#define _NET_WM_STATE_ADD       1       /* add/set property      */
#define _NET_WM_STATE_TOGGLE    2       /* toggle property       */

static void
send_wmspec_change_state (GstVaapiWindow * window, Atom state, gboolean add)
{
  GstVaapiWindowX11Private *const priv =
      GST_VAAPI_WINDOW_X11_GET_PRIVATE (window);
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  XClientMessageEvent xclient;

  memset (&xclient, 0, sizeof (xclient));

  xclient.type = ClientMessage;
  xclient.window = GST_VAAPI_WINDOW_ID (window);
  xclient.message_type = priv->atom_NET_WM_STATE;
  xclient.format = 32;

  xclient.data.l[0] = add ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
  xclient.data.l[1] = state;
  xclient.data.l[2] = 0;
  xclient.data.l[3] = 0;
  xclient.data.l[4] = 0;

  XSendEvent (dpy,
      DefaultRootWindow (dpy),
      False,
      SubstructureRedirectMask | SubstructureNotifyMask, (XEvent *) & xclient);
}

static void
wait_event (GstVaapiWindow * window, int type)
{
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  const Window xid = GST_VAAPI_WINDOW_ID (window);
  XEvent e;
  Bool got_event;

  for (;;) {
    GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
    got_event = XCheckTypedWindowEvent (dpy, xid, type, &e);
    GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
    if (got_event)
      break;
    g_usleep (10);
  }
}

static gboolean
timed_wait_event (GstVaapiWindow * window, int type, guint64 end_time,
    XEvent * e)
{
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  const Window xid = GST_VAAPI_WINDOW_ID (window);
  XEvent tmp_event;
  guint64 now_time;
  Bool got_event;

  if (!e)
    e = &tmp_event;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  got_event = XCheckTypedWindowEvent (dpy, xid, type, e);
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
  if (got_event)
    return TRUE;

  do {
    g_usleep (10);
    GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
    got_event = XCheckTypedWindowEvent (dpy, xid, type, e);
    GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
    if (got_event)
      return TRUE;
    now_time = g_get_real_time ();
  } while (now_time < end_time);
  return FALSE;
}

static gboolean
gst_vaapi_window_x11_show (GstVaapiWindow * window)
{
  GstVaapiWindowX11Private *const priv =
      GST_VAAPI_WINDOW_X11_GET_PRIVATE (window);
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  const Window xid = GST_VAAPI_WINDOW_ID (window);
  XWindowAttributes wattr;
  gboolean has_errors;

  if (priv->is_mapped)
    return TRUE;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  x11_trap_errors ();
  if (window->use_foreign_window) {
    XGetWindowAttributes (dpy, xid, &wattr);
    if (!(wattr.your_event_mask & StructureNotifyMask))
      XSelectInput (dpy, xid, StructureNotifyMask);
  }
  XMapWindow (dpy, xid);
  has_errors = x11_untrap_errors () != 0;
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);

  if (!has_errors) {
    wait_event (window, MapNotify);
    if (window->use_foreign_window &&
        !(wattr.your_event_mask & StructureNotifyMask)) {
      GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
      x11_trap_errors ();
      XSelectInput (dpy, xid, wattr.your_event_mask);
      has_errors = x11_untrap_errors () != 0;
      GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
    }
    priv->is_mapped = TRUE;

    if (priv->fullscreen_on_map)
      gst_vaapi_window_set_fullscreen (window, TRUE);
  }
  return !has_errors;
}

static gboolean
gst_vaapi_window_x11_hide (GstVaapiWindow * window)
{
  GstVaapiWindowX11Private *const priv =
      GST_VAAPI_WINDOW_X11_GET_PRIVATE (window);
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  const Window xid = GST_VAAPI_WINDOW_ID (window);
  XWindowAttributes wattr;
  gboolean has_errors;

  if (!priv->is_mapped)
    return TRUE;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  x11_trap_errors ();
  if (window->use_foreign_window) {
    XGetWindowAttributes (dpy, xid, &wattr);
    if (!(wattr.your_event_mask & StructureNotifyMask))
      XSelectInput (dpy, xid, StructureNotifyMask);
  }
  XUnmapWindow (dpy, xid);
  has_errors = x11_untrap_errors () != 0;
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);

  if (!has_errors) {
    wait_event (window, UnmapNotify);
    if (window->use_foreign_window &&
        !(wattr.your_event_mask & StructureNotifyMask)) {
      GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
      x11_trap_errors ();
      XSelectInput (dpy, xid, wattr.your_event_mask);
      has_errors = x11_untrap_errors () != 0;
      GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
    }
    priv->is_mapped = FALSE;
  }
  return !has_errors;
}

static gboolean
gst_vaapi_window_x11_create (GstVaapiWindow * window, guint * width,
    guint * height)
{
  GstVaapiWindowX11Private *const priv =
      GST_VAAPI_WINDOW_X11_GET_PRIVATE (window);
  GstVaapiDisplay *const display = GST_VAAPI_WINDOW_DISPLAY (window);
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  Window xid = GST_VAAPI_WINDOW_ID (window);
  guint vid = 0;
  Colormap cmap = None;
  const GstVaapiDisplayClass *display_class;
  const GstVaapiWindowClass *window_class;
  XWindowAttributes wattr;
  Atom wm_delete, atoms[2];
  gboolean ok;

  static const char *atom_names[2] = {
    "_NET_WM_STATE",
    "_NET_WM_STATE_FULLSCREEN",
  };

  if (window->use_foreign_window && xid) {
    GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
    XGetWindowAttributes (dpy, xid, &wattr);
    priv->is_mapped = wattr.map_state == IsViewable;
    ok = x11_get_geometry (dpy, xid, NULL, NULL, width, height, NULL);
    GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
    return ok;
  }

  display_class = GST_VAAPI_DISPLAY_GET_CLASS (display);
  if (display_class) {
    if (display_class->get_visual_id)
      vid = display_class->get_visual_id (display, window);
    if (display_class->get_colormap)
      cmap = display_class->get_colormap (display, window);
  }

  window_class = GST_VAAPI_WINDOW_GET_CLASS (window);
  if (window_class) {
    if (window_class->get_visual_id && !vid)
      vid = window_class->get_visual_id (window);
    if (window_class->get_colormap && !cmap)
      cmap = window_class->get_colormap (window);
  }

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  XInternAtoms (dpy,
      (char **) atom_names, G_N_ELEMENTS (atom_names), False, atoms);
  priv->atom_NET_WM_STATE = atoms[0];
  priv->atom_NET_WM_STATE_FULLSCREEN = atoms[1];

  xid = x11_create_window (dpy, *width, *height, vid, cmap);
  if (xid) {
    /* Tell the window manager we'd like delete client messages instead of
     * being killed */
    wm_delete = XInternAtom (dpy, "WM_DELETE_WINDOW", True);
    if (wm_delete != None) {
      (void) XSetWMProtocols (dpy, xid, &wm_delete, 1);
    }

    XRaiseWindow (dpy, xid);
  }
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);

  GST_DEBUG ("xid %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS (xid));
  GST_VAAPI_WINDOW_ID (window) = xid;
  return xid != None;
}

static void
gst_vaapi_window_x11_finalize (GObject * object)
{
  GstVaapiWindow *window = GST_VAAPI_WINDOW (object);
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  const Window xid = GST_VAAPI_WINDOW_ID (window);

  if (xid) {
    if (!window->use_foreign_window) {
      GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
      XDestroyWindow (dpy, xid);
      GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
    }
    GST_VAAPI_WINDOW_ID (window) = None;
  }

  G_OBJECT_CLASS (gst_vaapi_window_x11_parent_class)->finalize (object);
}

static gboolean
gst_vaapi_window_x11_get_geometry (GstVaapiWindow * window,
    gint * px, gint * py, guint * pwidth, guint * pheight)
{
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  const Window xid = GST_VAAPI_WINDOW_ID (window);
  gboolean success;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  success = x11_get_geometry (dpy, xid, px, py, pwidth, pheight, NULL);
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
  return success;
}

static gboolean
gst_vaapi_window_x11_set_fullscreen (GstVaapiWindow * window,
    gboolean fullscreen)
{
  GstVaapiWindowX11Private *const priv =
      GST_VAAPI_WINDOW_X11_GET_PRIVATE (window);
  Display *const dpy = GST_VAAPI_WINDOW_NATIVE_DISPLAY (window);
  const Window xid = GST_VAAPI_WINDOW_ID (window);
  XEvent e;
  guint width, height;
  gboolean has_errors;
  guint64 end_time;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  x11_trap_errors ();
  if (fullscreen) {
    if (!priv->is_mapped) {
      priv->fullscreen_on_map = TRUE;

      XChangeProperty (dpy,
          xid,
          priv->atom_NET_WM_STATE, XA_ATOM, 32,
          PropModeReplace,
          (unsigned char *) &priv->atom_NET_WM_STATE_FULLSCREEN, 1);
    } else {
      send_wmspec_change_state (window,
          priv->atom_NET_WM_STATE_FULLSCREEN, TRUE);
    }
  } else {
    if (!priv->is_mapped) {
      priv->fullscreen_on_map = FALSE;

      XDeleteProperty (dpy, xid, priv->atom_NET_WM_STATE);
    } else {
      send_wmspec_change_state (window,
          priv->atom_NET_WM_STATE_FULLSCREEN, FALSE);
    }
  }
  XSync (dpy, False);
  has_errors = x11_untrap_errors () != 0;
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
  if (has_errors)
    return FALSE;

  /* Try to wait for the completion of the fullscreen mode switch */
  if (!window->use_foreign_window && priv->is_mapped) {
    const guint DELAY = 100000; /* 100 ms */
    end_time = DELAY + g_get_real_time ();
    while (timed_wait_event (window, ConfigureNotify, end_time, &e)) {
      if (fullscreen) {
        gst_vaapi_display_get_size (GST_VAAPI_WINDOW_DISPLAY (window),
            &width, &height);
        if (e.xconfigure.width == width && e.xconfigure.height == height)
          return TRUE;
      } else {
        gst_vaapi_window_get_size (window, &width, &height);
        if (e.xconfigure.width != width || e.xconfigure.height != height)
          return TRUE;
      }
    }
  }
  return FALSE;
}

static gboolean
gst_vaapi_window_x11_resize (GstVaapiWindow * window, guint width, guint height)
{
  gboolean has_errors;

  if (!GST_VAAPI_WINDOW_ID (window))
    return FALSE;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  x11_trap_errors ();
  XResizeWindow (GST_VAAPI_WINDOW_NATIVE_DISPLAY (window),
      GST_VAAPI_WINDOW_ID (window), width, height);
  has_errors = x11_untrap_errors () != 0;
  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);
  return !has_errors;
}

static VAStatus
gst_vaapi_window_x11_put_surface (GstVaapiWindow * window,
    VASurfaceID surface_id,
    const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags)
{
  VAStatus status;

  GST_VAAPI_WINDOW_LOCK_DISPLAY (window);
  status = vaPutSurface (GST_VAAPI_WINDOW_VADISPLAY (window),
      surface_id,
      GST_VAAPI_WINDOW_ID (window),
      src_rect->x,
      src_rect->y,
      src_rect->width,
      src_rect->height,
      dst_rect->x,
      dst_rect->y,
      dst_rect->width,
      dst_rect->height, NULL, 0, from_GstVaapiSurfaceRenderFlags (flags)
      );

  GST_VAAPI_WINDOW_UNLOCK_DISPLAY (window);

  return status;
}

static gboolean
gst_vaapi_window_x11_render (GstVaapiWindow * window,
    GstVaapiSurface * surface,
    const GstVaapiRectangle * src_rect,
    const GstVaapiRectangle * dst_rect, guint flags)
{
  VASurfaceID surface_id;
  VAStatus status;
  GstVaapiWindowX11Private *const priv =
      GST_VAAPI_WINDOW_X11_GET_PRIVATE (window);
  gboolean ret = FALSE;

  surface_id = GST_VAAPI_SURFACE_ID (surface);
  if (surface_id == VA_INVALID_ID)
    return FALSE;

  if (window->has_vpp && priv->need_vpp)
    goto conversion;

  status =
      gst_vaapi_window_x11_put_surface (window, surface_id, src_rect, dst_rect,
      flags);

  if (status == VA_STATUS_ERROR_FLAG_NOT_SUPPORTED ||
      status == VA_STATUS_ERROR_UNIMPLEMENTED ||
      status == VA_STATUS_ERROR_INVALID_IMAGE_FORMAT) {
    priv->need_vpp = TRUE;
  } else {
    ret = vaapi_check_status (status, "vaPutSurface()");
  }

conversion:
  if (priv->need_vpp && window->has_vpp) {
    GstVaapiSurface *const vpp_surface =
        gst_vaapi_window_vpp_convert_internal (window, surface, NULL, NULL,
        flags);
    if (G_LIKELY (vpp_surface)) {
      GstVaapiRectangle vpp_src_rect;

      surface_id = GST_VAAPI_SURFACE_ID (vpp_surface);
      vpp_src_rect.x = vpp_src_rect.y = 0;
      vpp_src_rect.width = GST_VAAPI_SURFACE_WIDTH (vpp_surface);
      vpp_src_rect.height = GST_VAAPI_SURFACE_HEIGHT (vpp_surface);

      status =
          gst_vaapi_window_x11_put_surface (window, surface_id, &vpp_src_rect,
          dst_rect, flags);

      ret = vaapi_check_status (status, "vaPutSurface()");

      if (!gst_vaapi_surface_sync (vpp_surface)) {
        GST_WARNING ("failed to render surface");
        ret = FALSE;
      }

      gst_vaapi_video_pool_put_object (window->surface_pool, vpp_surface);
    } else {
      priv->need_vpp = FALSE;
    }
  }

  return ret;
}

static void
gst_vaapi_window_x11_class_init (GstVaapiWindowX11Class * klass)
{
  GObjectClass *const object_class = G_OBJECT_CLASS (klass);
  GstVaapiWindowClass *const window_class = GST_VAAPI_WINDOW_CLASS (klass);

  object_class->finalize = gst_vaapi_window_x11_finalize;

  window_class->create = gst_vaapi_window_x11_create;
  window_class->show = gst_vaapi_window_x11_show;
  window_class->hide = gst_vaapi_window_x11_hide;
  window_class->get_geometry = gst_vaapi_window_x11_get_geometry;
  window_class->set_fullscreen = gst_vaapi_window_x11_set_fullscreen;
  window_class->resize = gst_vaapi_window_x11_resize;
  window_class->render = gst_vaapi_window_x11_render;
}

static void
gst_vaapi_window_x11_init (GstVaapiWindowX11 * window)
{
}

/**
 * gst_vaapi_window_x11_new:
 * @display: a #GstVaapiDisplay
 * @width: the requested window width, in pixels
 * @height: the requested windo height, in pixels
 *
 * Creates a window with the specified @width and @height. The window
 * will be attached to the @display and remains invisible to the user
 * until gst_vaapi_window_show() is called.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_x11_new (GstVaapiDisplay * display, guint width, guint height)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_X11 (display), NULL);

  return gst_vaapi_window_new_internal (GST_TYPE_VAAPI_WINDOW_X11, display,
      GST_VAAPI_ID_INVALID, width, height);
}

/**
 * gst_vaapi_window_x11_new_with_xid:
 * @display: a #GstVaapiDisplay
 * @xid: an X11 Window id
 *
 * Creates a #GstVaapiWindow using the X11 Window @xid. The caller
 * still owns the window and must call XDestroyWindow() when all
 * #GstVaapiWindow references are released. Doing so too early can
 * yield undefined behaviour.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_x11_new_with_xid (GstVaapiDisplay * display, Window xid)
{
  g_return_val_if_fail (GST_VAAPI_IS_DISPLAY_X11 (display), NULL);
  g_return_val_if_fail (xid != None, NULL);

  return gst_vaapi_window_new_internal (GST_TYPE_VAAPI_WINDOW_X11, display,
      xid, 0, 0);
}

/**
 * gst_vaapi_window_x11_get_xid:
 * @window: a #GstVaapiWindowX11
 *
 * Returns the underlying X11 Window that was created by
 * gst_vaapi_window_x11_new() or that was bound with
 * gst_vaapi_window_x11_new_with_xid().
 *
 * Return value: the underlying X11 Window bound to @window.
 */
Window
gst_vaapi_window_x11_get_xid (GstVaapiWindowX11 * window)
{
  g_return_val_if_fail (GST_VAAPI_IS_WINDOW_X11 (window), None);

  return GST_VAAPI_WINDOW_ID (window);
}

/**
 * gst_vaapi_window_x11_is_foreign_xid:
 * @window: a #GstVaapiWindowX11
 *
 * Checks whether the @window XID was created by gst_vaapi_window_x11_new() or bound with gst_vaapi_window_x11_new_with_xid().
 *
 * Return value: %TRUE if the underlying X window is owned by the
 *   caller (foreign window)
 */
gboolean
gst_vaapi_window_x11_is_foreign_xid (GstVaapiWindowX11 * window)
{
  g_return_val_if_fail (GST_VAAPI_IS_WINDOW_X11 (window), FALSE);

  return GST_VAAPI_WINDOW (window)->use_foreign_window;
}
