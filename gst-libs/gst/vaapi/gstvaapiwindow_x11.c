/*
 *  gstvaapiwindow_x11.c - VA/X11 window abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2012 Intel Corporation
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
#include <string.h>
#include <X11/Xatom.h>
#include "gstvaapicompat.h"
#include "gstvaapiwindow_x11.h"
#include "gstvaapidisplay_x11.h"
#include "gstvaapidisplay_x11_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapiutils_x11.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiWindowX11, gst_vaapi_window_x11, GST_VAAPI_TYPE_WINDOW)

#define GST_VAAPI_WINDOW_X11_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_WINDOW_X11,     \
                                 GstVaapiWindowX11Private))

struct _GstVaapiWindowX11Private {
    Atom                atom_NET_WM_STATE;
    Atom                atom_NET_WM_STATE_FULLSCREEN;
    guint               create_window           : 1;
    guint               is_mapped               : 1;
    guint               fullscreen_on_map       : 1;
};

#define _NET_WM_STATE_REMOVE    0 /* remove/unset property */
#define _NET_WM_STATE_ADD       1 /* add/set property      */
#define _NET_WM_STATE_TOGGLE    2 /* toggle property       */

static void
send_wmspec_change_state(GstVaapiWindowX11 *window, Atom state, gboolean add)
{
    GstVaapiWindowX11Private * const priv = window->priv;
    Display * const dpy = GST_VAAPI_OBJECT_XDISPLAY(window);
    XClientMessageEvent xclient;

    memset(&xclient, 0, sizeof(xclient));

    xclient.type         = ClientMessage;
    xclient.window       = GST_VAAPI_OBJECT_ID(window);
    xclient.message_type = priv->atom_NET_WM_STATE;
    xclient.format       = 32;

    xclient.data.l[0] = add ? _NET_WM_STATE_ADD : _NET_WM_STATE_REMOVE;
    xclient.data.l[1] = state;
    xclient.data.l[2] = 0;
    xclient.data.l[3] = 0;
    xclient.data.l[4] = 0;

    XSendEvent(
        dpy,
        DefaultRootWindow(dpy),
        False,
        SubstructureRedirectMask|SubstructureNotifyMask,
        (XEvent *)&xclient
    );
}

static void
wait_event(GstVaapiWindow *window, int type)
{
    Display * const     dpy = GST_VAAPI_OBJECT_XDISPLAY(window);
    const Window        xid = GST_VAAPI_OBJECT_ID(window);
    XEvent              e;
    Bool                got_event;

    for (;;) {
        GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
        got_event = XCheckTypedWindowEvent(dpy, xid, type, &e);
        GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        if (got_event)
            break;
        g_usleep(10);
    }
}

static gboolean
timed_wait_event(GstVaapiWindow *window, int type, guint64 end_time, XEvent *e)
{
    Display * const     dpy = GST_VAAPI_OBJECT_XDISPLAY(window);
    const Window        xid = GST_VAAPI_OBJECT_ID(window);
    XEvent              tmp_event;
    GTimeVal            now;
    guint64             now_time;
    Bool                got_event;

    if (!e)
        e = &tmp_event;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    got_event = XCheckTypedWindowEvent(dpy, xid, type, e);
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
    if (got_event)
        return TRUE;

    do {
        g_usleep(10);
        GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
        got_event = XCheckTypedWindowEvent(dpy, xid, type, e);
        GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        if (got_event)
            return TRUE;
        g_get_current_time(&now);
        now_time = (guint64)now.tv_sec * 1000000 + now.tv_usec;
    } while (now_time < end_time);
    return FALSE;
}

static gboolean
gst_vaapi_window_x11_show(GstVaapiWindow *window)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const                  dpy  = GST_VAAPI_OBJECT_XDISPLAY(window);
    const Window                     xid  = GST_VAAPI_OBJECT_ID(window);
    XWindowAttributes                wattr;
    gboolean                         has_errors;

    if (priv->is_mapped)
        return TRUE;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    x11_trap_errors();
    if (!priv->create_window) {
        XGetWindowAttributes(dpy, xid, &wattr);
        if (!(wattr.your_event_mask & StructureNotifyMask))
            XSelectInput(dpy, xid, StructureNotifyMask);
    }
    XMapWindow(dpy, xid);
    has_errors = x11_untrap_errors() != 0;
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);

    if (!has_errors) {
        wait_event(window, MapNotify);
        if (!priv->create_window &&
            !(wattr.your_event_mask & StructureNotifyMask)) {
            GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
            x11_trap_errors();
            XSelectInput(dpy, xid, wattr.your_event_mask);
            has_errors = x11_untrap_errors() != 0;
            GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        }
        priv->is_mapped = TRUE;

        if (priv->fullscreen_on_map)
            gst_vaapi_window_set_fullscreen(window, TRUE);
    }
    return !has_errors;
}

static gboolean
gst_vaapi_window_x11_hide(GstVaapiWindow *window)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const                  dpy  = GST_VAAPI_OBJECT_XDISPLAY(window);
    const Window                     xid  = GST_VAAPI_OBJECT_ID(window);
    XWindowAttributes                wattr;
    gboolean                         has_errors;

    if (!priv->is_mapped)
        return TRUE;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    x11_trap_errors();
    if (!priv->create_window) {
        XGetWindowAttributes(dpy, xid, &wattr);
        if (!(wattr.your_event_mask & StructureNotifyMask))
            XSelectInput(dpy, xid, StructureNotifyMask);
    }
    XUnmapWindow(dpy, xid);
    has_errors = x11_untrap_errors() != 0;
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);

    if (!has_errors) {
        wait_event(window, UnmapNotify);
        if (!priv->create_window &&
            !(wattr.your_event_mask & StructureNotifyMask)) {
            GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
            x11_trap_errors();
            XSelectInput(dpy, xid, wattr.your_event_mask);
            has_errors = x11_untrap_errors() != 0;
            GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        }
        priv->is_mapped = FALSE;
    }
    return !has_errors;
}

static gboolean
gst_vaapi_window_x11_create(GstVaapiWindow *window, guint *width, guint *height)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const                  dpy  = GST_VAAPI_OBJECT_XDISPLAY(window);
    Window                           xid  = GST_VAAPI_OBJECT_ID(window);
    Visual                          *vis  = NULL;
    Colormap                         cmap = None;
    GstVaapiWindowX11Class          *klass;
    XWindowAttributes                wattr;
    Atom                             atoms[2];
    gboolean                         ok;

    static const char *atom_names[2] = {
        "_NET_WM_STATE",
        "_NET_WM_STATE_FULLSCREEN",
    };

    if (!priv->create_window && xid) {
        GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
        XGetWindowAttributes(dpy, xid, &wattr);
        priv->is_mapped = wattr.map_state == IsViewable;
        ok = x11_get_geometry(dpy, xid, NULL, NULL, width, height);
        GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        return ok;
    }

    klass = GST_VAAPI_WINDOW_X11_GET_CLASS(window);
    if (klass) {
        if (klass->get_visual)
            vis = klass->get_visual(window);
        if (klass->get_colormap)
            cmap = klass->get_colormap(window);
    }

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    XInternAtoms(
        dpy,
        (char **)atom_names, G_N_ELEMENTS(atom_names),
        False,
        atoms
    );
    priv->atom_NET_WM_STATE            = atoms[0];
    priv->atom_NET_WM_STATE_FULLSCREEN = atoms[1];

    xid = x11_create_window(dpy, *width, *height, vis, cmap);
    if (xid)
        XRaiseWindow(dpy, xid);
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);

    GST_DEBUG("xid %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(xid));
    GST_VAAPI_OBJECT_ID(window) = xid;
    return xid != None;
}

static void
gst_vaapi_window_x11_destroy(GstVaapiWindow *window)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const                  dpy  = GST_VAAPI_OBJECT_XDISPLAY(window);
    const Window                     xid  = GST_VAAPI_OBJECT_ID(window);

    if (xid) {
        if (priv->create_window) {
            GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
            XDestroyWindow(dpy, xid);
            GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
        }
        GST_VAAPI_OBJECT_ID(window) = None;
    }
}

static gboolean
gst_vaapi_window_x11_get_geometry(
    GstVaapiWindow *window,
    gint           *px,
    gint           *py,
    guint          *pwidth,
    guint          *pheight)
{
    Display * const     dpy = GST_VAAPI_OBJECT_XDISPLAY(window);
    const Window        xid = GST_VAAPI_OBJECT_ID(window);

    return x11_get_geometry(dpy, xid, px, py, pwidth, pheight);
}

static gboolean
gst_vaapi_window_x11_set_fullscreen(GstVaapiWindow *window, gboolean fullscreen)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const                  dpy  = GST_VAAPI_OBJECT_XDISPLAY(window);
    const Window                     xid  = GST_VAAPI_OBJECT_ID(window);
    XEvent e;
    guint width, height;
    gboolean has_errors;
    GTimeVal now;
    guint64 end_time;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    x11_trap_errors();
    if (fullscreen) {
        if (!priv->is_mapped) {
            priv->fullscreen_on_map = TRUE;

            XChangeProperty(
                dpy,
                xid,
                priv->atom_NET_WM_STATE, XA_ATOM, 32,
                PropModeReplace,
                (unsigned char *)&priv->atom_NET_WM_STATE_FULLSCREEN, 1
            );
        }
        else {
            send_wmspec_change_state(
                GST_VAAPI_WINDOW_X11(window),
                priv->atom_NET_WM_STATE_FULLSCREEN,
                TRUE
            );
        }
    }
    else {
        if (!priv->is_mapped) {
            priv->fullscreen_on_map = FALSE;

            XDeleteProperty(
                dpy,
                xid,
                priv->atom_NET_WM_STATE
            );
        }
        else {
            send_wmspec_change_state(
                GST_VAAPI_WINDOW_X11(window),
                priv->atom_NET_WM_STATE_FULLSCREEN,
                FALSE
            );
        }
    }
    XSync(dpy, False);
    has_errors = x11_untrap_errors() != 0;
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
    if (has_errors)
        return FALSE;

    /* Try to wait for the completion of the fullscreen mode switch */
    if (priv->create_window && priv->is_mapped) {
        const guint DELAY = 100000; /* 100 ms */
        g_get_current_time(&now);
        end_time = DELAY + ((guint64)now.tv_sec * 1000000 + now.tv_usec);
        while (timed_wait_event(window, ConfigureNotify, end_time, &e)) {
            if (fullscreen) {
                gst_vaapi_display_get_size(
                    GST_VAAPI_OBJECT_DISPLAY(window),
                    &width,
                    &height
                );
                if (e.xconfigure.width == width && e.xconfigure.height == height)
                    return TRUE;
            }
            else {
                gst_vaapi_window_get_size(window, &width, &height);
                if (e.xconfigure.width != width || e.xconfigure.height != height)
                    return TRUE;
            }
        }
    }
    return FALSE;
}

static gboolean
gst_vaapi_window_x11_resize(GstVaapiWindow *window, guint width, guint height)
{
    gboolean has_errors;

    if (!GST_VAAPI_OBJECT_ID(window))
        return FALSE;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    x11_trap_errors();
    XResizeWindow(
        GST_VAAPI_OBJECT_XDISPLAY(window),
        GST_VAAPI_OBJECT_ID(window),
        width,
        height
    );
    has_errors = x11_untrap_errors() != 0;
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
    return !has_errors;
}

static gboolean
gst_vaapi_window_x11_render(
    GstVaapiWindow          *window,
    GstVaapiSurface         *surface,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect,
    guint                    flags
)
{
    VASurfaceID surface_id;
    VAStatus status;

    surface_id = GST_VAAPI_OBJECT_ID(surface);
    if (surface_id == VA_INVALID_ID)
        return FALSE;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);
    status = vaPutSurface(
        GST_VAAPI_OBJECT_VADISPLAY(window),
        surface_id,
        GST_VAAPI_OBJECT_ID(window),
        src_rect->x,
        src_rect->y,
        src_rect->width,
        src_rect->height,
        dst_rect->x,
        dst_rect->y,
        dst_rect->width,
        dst_rect->height,
        NULL, 0,
        from_GstVaapiSurfaceRenderFlags(flags)
    );
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
    if (!vaapi_check_status(status, "vaPutSurface()"))
        return FALSE;

    return TRUE;
}

static void
gst_vaapi_window_x11_finalize(GObject *object)
{
    G_OBJECT_CLASS(gst_vaapi_window_x11_parent_class)->finalize(object);
}

static void
gst_vaapi_window_x11_constructed(GObject *object)
{
    GstVaapiWindowX11 * const window = GST_VAAPI_WINDOW_X11(object);
    GObjectClass *parent_class;

    window->priv->create_window = GST_VAAPI_OBJECT_ID(object) == None;

    parent_class = G_OBJECT_CLASS(gst_vaapi_window_x11_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
gst_vaapi_window_x11_class_init(GstVaapiWindowX11Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiWindowClass * const window_class = GST_VAAPI_WINDOW_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiWindowX11Private));

    object_class->finalize       = gst_vaapi_window_x11_finalize;
    object_class->constructed    = gst_vaapi_window_x11_constructed;

    window_class->create         = gst_vaapi_window_x11_create;
    window_class->destroy        = gst_vaapi_window_x11_destroy;
    window_class->show           = gst_vaapi_window_x11_show;
    window_class->hide           = gst_vaapi_window_x11_hide;
    window_class->get_geometry   = gst_vaapi_window_x11_get_geometry;
    window_class->set_fullscreen = gst_vaapi_window_x11_set_fullscreen;
    window_class->resize         = gst_vaapi_window_x11_resize;
    window_class->render         = gst_vaapi_window_x11_render;
}

static void
gst_vaapi_window_x11_init(GstVaapiWindowX11 *window)
{
    GstVaapiWindowX11Private *priv = GST_VAAPI_WINDOW_X11_GET_PRIVATE(window);

    window->priv                = priv;
    priv->create_window         = TRUE;
    priv->is_mapped             = FALSE;
    priv->fullscreen_on_map     = FALSE;
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
gst_vaapi_window_x11_new(GstVaapiDisplay *display, guint width, guint height)
{
    GST_DEBUG("new window, size %ux%u", width, height);

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(width  > 0, NULL);
    g_return_val_if_fail(height > 0, NULL);

    return g_object_new(GST_VAAPI_TYPE_WINDOW_X11,
                        "display", display,
                        "id",      GST_VAAPI_ID(None),
                        "width",   width,
                        "height",  height,
                        NULL);
}

/**
 * gst_vaapi_window_x11_new_with_xid:
 * @display: a #GstVaapiDisplay
 * @xid: an X11 #Window id
 *
 * Creates a #GstVaapiWindow using the X11 #Window @xid. The caller
 * still owns the window and must call XDestroyWindow() when all
 * #GstVaapiWindow references are released. Doing so too early can
 * yield undefined behaviour.
 *
 * Return value: the newly allocated #GstVaapiWindow object
 */
GstVaapiWindow *
gst_vaapi_window_x11_new_with_xid(GstVaapiDisplay *display, Window xid)
{
    GST_DEBUG("new window from xid 0x%08x", xid);

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(xid != None, NULL);

    return g_object_new(GST_VAAPI_TYPE_WINDOW_X11,
                        "display", display,
                        "id",      GST_VAAPI_ID(xid),
                        NULL);
}

/**
 * gst_vaapi_window_x11_get_xid:
 * @window: a #GstVaapiWindowX11
 *
 * Returns the underlying X11 #Window that was created by
 * gst_vaapi_window_x11_new() or that was bound with
 * gst_vaapi_window_x11_new_with_xid().
 *
 * Return value: the underlying X11 #Window bound to @window.
 */
Window
gst_vaapi_window_x11_get_xid(GstVaapiWindowX11 *window)
{
    g_return_val_if_fail(GST_VAAPI_IS_WINDOW_X11(window), None);

    return GST_VAAPI_OBJECT_ID(window);
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
gst_vaapi_window_x11_is_foreign_xid(GstVaapiWindowX11 *window)
{
    g_return_val_if_fail(GST_VAAPI_IS_WINDOW_X11(window), FALSE);

    return !window->priv->create_window;
}
