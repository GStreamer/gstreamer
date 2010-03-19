/*
 *  gstvaapiwindow_x11.c - VA/X11 window abstraction
 *
 *  gstreamer-vaapi (C) 2010 Splitted-Desktop Systems
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 */

/**
 * SECTION:gst-vaapi-window-x11
 * @short_description:
 */

#include "config.h"
#include "gstvaapiwindow_x11.h"
#include "gstvaapidisplay_x11.h"
#include "gstvaapiutils_x11.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiWindowX11, gst_vaapi_window_x11, GST_VAAPI_TYPE_WINDOW);

#define GST_VAAPI_WINDOW_X11_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_WINDOW_X11,     \
                                 GstVaapiWindowX11Private))

struct _GstVaapiWindowX11Private {
    GstVaapiDisplay    *display;
    Window              xid;
    guint               create_window   : 1;
    guint               is_visible      : 1;
};

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_XID,
};

static gboolean
gst_vaapi_window_x11_show(GstVaapiWindow *window)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const dpy = GST_VAAPI_DISPLAY_XDISPLAY(priv->display);
    gboolean has_errors;

    if (priv->is_visible)
        return TRUE;

    GST_VAAPI_DISPLAY_LOCK(priv->display);
    x11_trap_errors();
    XMapWindow(dpy, priv->xid);
    if (priv->create_window)
        x11_wait_event(dpy, priv->xid, MapNotify);
    has_errors = x11_untrap_errors() != 0;
    GST_VAAPI_DISPLAY_UNLOCK(priv->display);
    if (has_errors)
        return FALSE;

    priv->is_visible = TRUE;
    return TRUE;
}

static gboolean
gst_vaapi_window_x11_hide(GstVaapiWindow *window)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const dpy = GST_VAAPI_DISPLAY_XDISPLAY(priv->display);
    gboolean has_errors;

    if (!priv->is_visible)
        return TRUE;

    GST_VAAPI_DISPLAY_LOCK(priv->display);
    x11_trap_errors();
    XUnmapWindow(dpy, priv->xid);
    if (priv->create_window)
        x11_wait_event(dpy, priv->xid, UnmapNotify);
    has_errors = x11_untrap_errors() != 0;
    GST_VAAPI_DISPLAY_UNLOCK(priv->display);
    if (has_errors)
        return FALSE;

    priv->is_visible = FALSE;
    return TRUE;
}

static gboolean
gst_vaapi_window_x11_create(GstVaapiWindow *window, guint *width, guint *height)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const dpy = GST_VAAPI_DISPLAY_XDISPLAY(priv->display);
    gboolean ok;

    if (!priv->create_window && priv->xid) {
        GST_VAAPI_DISPLAY_LOCK(priv->display);
        ok = x11_get_geometry(dpy, priv->xid, NULL, NULL, width, height);
        GST_VAAPI_DISPLAY_UNLOCK(priv->display);
        return ok;
    }

    GST_VAAPI_DISPLAY_LOCK(priv->display);
    priv->xid = x11_create_window(dpy, *width, *height);
    if (priv->xid)
        XRaiseWindow(dpy, priv->xid);
    GST_VAAPI_DISPLAY_UNLOCK(priv->display);
    return priv->xid != None;
}

static void
gst_vaapi_window_x11_destroy(GstVaapiWindow *window)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const dpy = GST_VAAPI_DISPLAY_XDISPLAY(priv->display);

    if (priv->xid) {
        if (priv->create_window) {
            GST_VAAPI_DISPLAY_LOCK(priv->display);
            XDestroyWindow(dpy, priv->xid);
            GST_VAAPI_DISPLAY_UNLOCK(priv->display);
        }
        priv->xid = None;
    }

    if (priv->display) {
        g_object_unref(priv->display);
        priv->display = NULL;
    }
}

static gboolean
gst_vaapi_window_x11_resize(GstVaapiWindow *window, guint width, guint height)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    gboolean has_errors;

    if (!priv->xid)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(priv->display);
    x11_trap_errors();
    XResizeWindow(
        GST_VAAPI_DISPLAY_XDISPLAY(priv->display),
        priv->xid,
        width,
        height
    );
    has_errors = x11_untrap_errors() != 0;
    GST_VAAPI_DISPLAY_UNLOCK(priv->display);
    return !has_errors;
}

static gboolean
gst_vaapi_window_x11_render(
    GstVaapiWindow          *window,
    GstVaapiSurface         *surface,
    const GstVideoRectangle *src_rect,
    const GstVideoRectangle *dst_rect,
    guint                    flags
)
{
    GstVaapiDisplay *display;
    VASurfaceID surface_id;
    VAStatus status;
    guint va_flags = 0;

    display = gst_vaapi_surface_get_display(surface);
    if (!display)
        return FALSE;

    surface_id = gst_vaapi_surface_get_id(surface);
    if (surface_id == VA_INVALID_ID)
        return FALSE;

    if (flags & GST_VAAPI_PICTURE_STRUCTURE_TOP_FIELD)
        va_flags |= VA_TOP_FIELD;
    if (flags & GST_VAAPI_PICTURE_STRUCTURE_BOTTOM_FIELD)
        va_flags |= VA_BOTTOM_FIELD;
    if ((va_flags ^ (VA_TOP_FIELD|VA_BOTTOM_FIELD)) == 0)
        va_flags  = VA_FRAME_PICTURE;

    if (flags & GST_VAAPI_COLOR_STANDARD_ITUR_BT_709)
        va_flags |= VA_SRC_BT709;
    else if (flags & GST_VAAPI_COLOR_STANDARD_ITUR_BT_601)
        va_flags |= VA_SRC_BT601;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaPutSurface(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        surface_id,
        GST_VAAPI_WINDOW_X11(window)->priv->xid,
        src_rect->x,
        src_rect->y,
        src_rect->w,
        src_rect->h,
        dst_rect->x,
        dst_rect->y,
        dst_rect->w,
        dst_rect->h,
        NULL, 0,
        va_flags
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
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
gst_vaapi_window_x11_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiWindowX11 * const window = GST_VAAPI_WINDOW_X11(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        window->priv->display = g_object_ref(g_value_get_object(value));
        break;
    case PROP_XID:
        window->priv->xid = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_window_x11_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiWindowX11 * const window = GST_VAAPI_WINDOW_X11(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, window->priv->display);
        break;
    case PROP_XID:
        g_value_set_uint(value, gst_vaapi_window_x11_get_xid(window));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_window_x11_constructed(GObject *object)
{
    GstVaapiWindowX11 * const window = GST_VAAPI_WINDOW_X11(object);
    GObjectClass *parent_class;

    window->priv->create_window = window->priv->xid == None;

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

    object_class->finalize      = gst_vaapi_window_x11_finalize;
    object_class->set_property  = gst_vaapi_window_x11_set_property;
    object_class->get_property  = gst_vaapi_window_x11_get_property;
    object_class->constructed   = gst_vaapi_window_x11_constructed;

    window_class->create        = gst_vaapi_window_x11_create;
    window_class->destroy       = gst_vaapi_window_x11_destroy;
    window_class->show          = gst_vaapi_window_x11_show;
    window_class->hide          = gst_vaapi_window_x11_hide;
    window_class->resize        = gst_vaapi_window_x11_resize;
    window_class->render        = gst_vaapi_window_x11_render;

    /**
     * GstVaapiWindowX11:display:
     *
     * The #GstVaapiDisplay this window is bound to
     */
    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "Display",
                             "The GstVaapiDisplay this window is bound to",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    /**
     * GstVaapiWindowX11:xid:
     *
     * The underlying X11 #Window XID.
     */
    g_object_class_install_property
        (object_class,
         PROP_XID,
         g_param_spec_uint("xid",
                           "X window id",
                           "The underlying X11 window id",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_window_x11_init(GstVaapiWindowX11 *window)
{
    GstVaapiWindowX11Private *priv = GST_VAAPI_WINDOW_X11_GET_PRIVATE(window);

    window->priv        = priv;
    priv->display       = NULL;
    priv->xid           = None;
    priv->create_window = TRUE;
    priv->is_visible    = FALSE;
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
    g_return_val_if_fail(width > 0, NULL);
    g_return_val_if_fail(height > 0, NULL);

    return g_object_new(GST_VAAPI_TYPE_WINDOW_X11,
                        "display", display,
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
                        "xid",     xid,
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
    g_return_val_if_fail(GST_VAAPI_WINDOW_X11(window), None);

    return window->priv->xid;
}
