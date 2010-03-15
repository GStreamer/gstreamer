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

#include "config.h"
#include "gstvaapiwindow_x11.h"
#include "gstvaapidisplay_x11.h"

#define DEBUG 1
#include "vaapi_debug.h"

G_DEFINE_TYPE(GstVaapiWindowX11, gst_vaapi_window_x11, GST_VAAPI_TYPE_WINDOW);

#define GST_VAAPI_WINDOW_X11_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_WINDOW_X11,     \
                                 GstVaapiWindowX11Private))

struct _GstVaapiWindowX11Private {
    gboolean            create_window;
    GstVaapiDisplay    *display;
    Window              xid;
};

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_XID,
};

// X error trap
static int x11_error_code = 0;
static int (*old_error_handler)(Display *, XErrorEvent *);

static int error_handler(Display *dpy, XErrorEvent *error)
{
    x11_error_code = error->error_code;
    return 0;
}

static void x11_trap_errors(void)
{
    x11_error_code    = 0;
    old_error_handler = XSetErrorHandler(error_handler);
}

static int x11_untrap_errors(void)
{
    XSetErrorHandler(old_error_handler);
    return x11_error_code;
}

// X window management
static const int x11_event_mask = (KeyPressMask |
                                   KeyReleaseMask |
                                   ButtonPressMask |
                                   ButtonReleaseMask |
                                   PointerMotionMask |
                                   EnterWindowMask |
                                   ExposureMask |
                                   StructureNotifyMask);

static Window
x11_create_window(Display *display, unsigned int width, unsigned int height)
{
    Window root_window, window;
    int screen, depth;
    Visual *vis;
    XSetWindowAttributes xswa;
    unsigned long xswa_mask;
    XWindowAttributes wattr;
    unsigned long black_pixel, white_pixel;

    screen      = DefaultScreen(display);
    vis         = DefaultVisual(display, screen);
    root_window = RootWindow(display, screen);
    black_pixel = BlackPixel(display, screen);
    white_pixel = WhitePixel(display, screen);

    XGetWindowAttributes(display, root_window, &wattr);
    depth = wattr.depth;
    if (depth != 15 && depth != 16 && depth != 24 && depth != 32)
        depth = 24;

    xswa_mask             = CWBorderPixel | CWBackPixel;
    xswa.border_pixel     = black_pixel;
    xswa.background_pixel = white_pixel;

    window = XCreateWindow(
        display,
        root_window,
        0, 0, width, height,
        0,
        depth,
        InputOutput,
        vis,
        xswa_mask, &xswa
    );
    if (!window)
        return None;

    XSelectInput(display, window, x11_event_mask);
    return window;
}

static gboolean
x11_get_geometry(
    Display    *dpy,
    Drawable    drawable,
    gint       *px,
    gint       *py,
    guint      *pwidth,
    guint      *pheight
)
{
    Window rootwin;
    int x, y;
    unsigned int width, height, border_width, depth;

    x11_trap_errors();
    XGetGeometry(
        dpy,
        drawable,
        &rootwin,
        &x, &y, &width, &height,
        &border_width,
        &depth
    );
    if (x11_untrap_errors())
        return FALSE;

    if (px)      *px      = x;
    if (py)      *py      = y;
    if (pwidth)  *pwidth  = width;
    if (pheight) *pheight = height;
    return TRUE;
}

static void x11_wait_event(Display *dpy, Window w, int type)
{
    XEvent e;
    while (!XCheckTypedWindowEvent(dpy, w, type, &e))
        g_usleep(10);
}

static gboolean
gst_vaapi_window_x11_show(GstVaapiWindow *window)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const dpy = GST_VAAPI_DISPLAY_XDISPLAY(priv->display);

    x11_trap_errors();
    XMapWindow(dpy, priv->xid);
    x11_wait_event(dpy, priv->xid, MapNotify);
    return x11_untrap_errors() == 0;
}

static gboolean
gst_vaapi_window_x11_hide(GstVaapiWindow *window)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const dpy = GST_VAAPI_DISPLAY_XDISPLAY(priv->display);

    x11_trap_errors();
    XUnmapWindow(dpy, priv->xid);
    x11_wait_event(dpy, priv->xid, UnmapNotify);
    return x11_untrap_errors() == 0;
}

static gboolean
gst_vaapi_window_x11_create(GstVaapiWindow *window, guint width, guint height)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display *dpy;

    if (!priv->create_window && priv->xid)
        return TRUE;

    dpy       = GST_VAAPI_DISPLAY_XDISPLAY(priv->display);
    priv->xid = x11_create_window(dpy, width, height);

    if (!gst_vaapi_window_x11_show(window))
        return FALSE;

    x11_trap_errors();
    XRaiseWindow(dpy, priv->xid);
    XSync(dpy, False);
    return x11_untrap_errors() == 0;
}

static void
gst_vaapi_window_x11_destroy(GstVaapiWindow *window)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;
    Display * const dpy = GST_VAAPI_DISPLAY_XDISPLAY(priv->display);

    if (priv->create_window && priv->xid) {
        gst_vaapi_window_x11_hide(window);
        XDestroyWindow(dpy, priv->xid);
        priv->xid = None;
    }

    g_object_unref(priv->display);
    priv->display = NULL;
}

static gboolean
gst_vaapi_window_x11_resize(GstVaapiWindow *window, guint width, guint height)
{
    GstVaapiWindowX11Private * const priv = GST_VAAPI_WINDOW_X11(window)->priv;

    if (!priv->xid)
        return FALSE;

    x11_trap_errors();
    XResizeWindow(
        GST_VAAPI_DISPLAY_XDISPLAY(priv->display),
        priv->xid,
        width,
        height
    );
    if (x11_untrap_errors() != 0)
        return FALSE;
    return TRUE;
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
    VASurfaceID surface_id;
    VAStatus status;
    unsigned int va_flags = 0;

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

    status = vaPutSurface(
        GST_VAAPI_DISPLAY_VADISPLAY(gst_vaapi_surface_get_display(surface)),
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

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "display",
                             "Display",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_XID,
         g_param_spec_uint("xid",
                           "X window id",
                           "X window ID",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_window_x11_init(GstVaapiWindowX11 *window)
{
    GstVaapiWindowX11Private *priv = GST_VAAPI_WINDOW_X11_GET_PRIVATE(window);

    window->priv        = priv;
    priv->create_window = TRUE;
    priv->display       = NULL;
    priv->xid           = None;
}

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

GstVaapiWindow *
gst_vaapi_window_x11_new_with_xid(GstVaapiDisplay *display, Window xid)
{
    Display *dpy;
    guint width, height;

    GST_DEBUG("new window from xid 0x%08x", xid);

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);

    dpy = GST_VAAPI_DISPLAY_XDISPLAY(display);
    if (!dpy || !x11_get_geometry(dpy, xid, NULL, NULL, &width, &height))
        return NULL;

    return g_object_new(GST_VAAPI_TYPE_WINDOW_X11,
                        "display", display,
                        "xid",     xid,
                        "width",   width,
                        "height",  height,
                        NULL);
}

Window
gst_vaapi_window_x11_get_xid(GstVaapiWindowX11 *window)
{
    g_return_val_if_fail(GST_VAAPI_WINDOW_X11(window), None);

    return window->priv->xid;
}
