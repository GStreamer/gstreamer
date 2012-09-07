/*
 *  gstvaapidisplay_x11.c - VA/X11 display abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2012 Intel Corporation
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
 * SECTION:gstvaapidisplay_x11
 * @short_description: VA/X11 display abstraction
 */

#include "sysdeps.h"
#include <string.h>
#include "gstvaapiutils.h"
#include "gstvaapidisplay_priv.h"
#include "gstvaapidisplay_x11.h"
#include "gstvaapidisplay_x11_priv.h"

#ifdef HAVE_XRANDR
# include <X11/extensions/Xrandr.h>
#endif

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDisplayX11,
              gst_vaapi_display_x11,
              GST_VAAPI_TYPE_DISPLAY)

enum {
    PROP_0,

    PROP_SYNCHRONOUS,
    PROP_DISPLAY_NAME,
    PROP_X11_DISPLAY,
    PROP_X11_SCREEN
};

#define NAME_PREFIX "X11:"
#define NAME_PREFIX_LENGTH 4

static inline gboolean
is_display_name(const gchar *display_name)
{
    return strncmp(display_name, NAME_PREFIX, NAME_PREFIX_LENGTH) == 0;
}

static inline const gchar *
get_default_display_name(void)
{
    static const gchar *g_display_name;

    if (!g_display_name)
        g_display_name = getenv("DISPLAY");
    return g_display_name;
}

static gboolean
compare_display_name(gconstpointer a, gconstpointer b, gpointer user_data)
{
    const gchar *cached_name = a, *cached_name_end;
    const gchar *tested_name = b, *tested_name_end;
    guint cached_name_length, tested_name_length;

    if (!cached_name || !is_display_name(cached_name))
        return FALSE;
    g_return_val_if_fail(tested_name && is_display_name(tested_name), FALSE);

    cached_name += NAME_PREFIX_LENGTH;
    cached_name_end = strchr(cached_name, ':');
    if (cached_name_end)
        cached_name_length = cached_name_end - cached_name;
    else
        cached_name_length = strlen(cached_name);

    tested_name += NAME_PREFIX_LENGTH;
    tested_name_end = strchr(tested_name, ':');
    if (tested_name_end)
        tested_name_length = tested_name_end - tested_name;
    else
        tested_name_length = strlen(tested_name);

    if (cached_name_length != tested_name_length)
        return FALSE;
    if (strncmp(cached_name, tested_name, cached_name_length) != 0)
        return FALSE;

    /* XXX: handle screen number? */
    return TRUE;
}

static void
gst_vaapi_display_x11_finalize(GObject *object)
{
    G_OBJECT_CLASS(gst_vaapi_display_x11_parent_class)->finalize(object);
}

/* Reconstruct a display name without our prefix */
static const gchar *
get_display_name(gpointer ptr)
{
    GstVaapiDisplayX11 * const display = GST_VAAPI_DISPLAY_X11(ptr);
    const gchar *display_name = display->priv->display_name;

    if (!display_name)
        return NULL;

    if (is_display_name(display_name)) {
        display_name += NAME_PREFIX_LENGTH;
        if (*display_name == '\0')
            return NULL;
        return display_name;
    }

    /* XXX: this should not happen */
    g_assert(0 && "display name without prefix");
    return display_name;
}

/* Mangle display name with our prefix */
static void
set_display_name(GstVaapiDisplayX11 *display, const gchar *display_name)
{
    GstVaapiDisplayX11Private * const priv = display->priv;

    g_free(priv->display_name);

    if (!display_name) {
        display_name = get_default_display_name();
        if (!display_name)
            display_name = "";
    }
    priv->display_name = g_strdup_printf("%s%s", NAME_PREFIX, display_name);
}

static void
set_synchronous(GstVaapiDisplayX11 *display, gboolean synchronous)
{
    GstVaapiDisplayX11Private * const priv = display->priv;

    if (priv->synchronous != synchronous) {
        priv->synchronous = synchronous;
        if (priv->x11_display)
            XSynchronize(priv->x11_display, synchronous);
    }
}

static void
gst_vaapi_display_x11_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDisplayX11 * const display = GST_VAAPI_DISPLAY_X11(object);

    switch (prop_id) {
    case PROP_SYNCHRONOUS:
        set_synchronous(display, g_value_get_boolean(value));
        break;
    case PROP_DISPLAY_NAME:
        set_display_name(display, g_value_get_string(value));
        break;
    case PROP_X11_DISPLAY:
        display->priv->x11_display = g_value_get_pointer(value);
        break;
    case PROP_X11_SCREEN:
        display->priv->x11_screen = g_value_get_int(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_x11_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDisplayX11 * const display = GST_VAAPI_DISPLAY_X11(object);

    switch (prop_id) {
    case PROP_SYNCHRONOUS:
        g_value_set_boolean(value, display->priv->synchronous);
        break;
    case PROP_DISPLAY_NAME:
        g_value_set_string(value, get_display_name(display));
        break;
    case PROP_X11_DISPLAY:
        g_value_set_pointer(value, gst_vaapi_display_x11_get_display(display));
        break;
    case PROP_X11_SCREEN:
        g_value_set_int(value, gst_vaapi_display_x11_get_screen(display));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_x11_constructed(GObject *object)
{
    GstVaapiDisplayX11 * const display = GST_VAAPI_DISPLAY_X11(object);
    GstVaapiDisplayX11Private * const priv = display->priv;
    GstVaapiDisplayCache * const cache = gst_vaapi_display_get_cache();
    const GstVaapiDisplayInfo *info;
    GObjectClass *parent_class;

    priv->create_display = priv->x11_display == NULL;

    /* Don't create X11 display if there is one in the cache already */
    if (priv->create_display) {
        info = gst_vaapi_display_cache_lookup_by_name(
            cache,
            priv->display_name,
            compare_display_name, NULL
        );
        if (info) {
            priv->x11_display    = info->native_display;
            priv->create_display = FALSE;
        }
    }

    /* Reset display-name if the user provided his own X11 display */
    if (!priv->create_display)
        set_display_name(display, XDisplayString(priv->x11_display));

    parent_class = G_OBJECT_CLASS(gst_vaapi_display_x11_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static gboolean
gst_vaapi_display_x11_open_display(GstVaapiDisplay *display)
{
    GstVaapiDisplayX11Private * const priv =
        GST_VAAPI_DISPLAY_X11(display)->priv;

    if (priv->create_display) {
        priv->x11_display = XOpenDisplay(get_display_name(display));
        if (!priv->x11_display)
            return FALSE;
        priv->x11_screen = DefaultScreen(priv->x11_display);
    }
    if (!priv->x11_display)
        return FALSE;

    if (priv->synchronous)
        XSynchronize(priv->x11_display, True);

#ifdef HAVE_XRANDR
    {
        int evt_base, err_base;
        priv->use_xrandr = XRRQueryExtension(
            priv->x11_display, &evt_base, &err_base);
    }
#endif
    return TRUE;
}

static void
gst_vaapi_display_x11_close_display(GstVaapiDisplay *display)
{
    GstVaapiDisplayX11Private * const priv =
        GST_VAAPI_DISPLAY_X11(display)->priv;

    if (priv->x11_display) {
        if (priv->create_display)
            XCloseDisplay(priv->x11_display);
        priv->x11_display = NULL;
    }

    if (priv->display_name) {
        g_free(priv->display_name);
        priv->display_name = NULL;
    }
}

static void
gst_vaapi_display_x11_sync(GstVaapiDisplay *display)
{
    GstVaapiDisplayX11Private * const priv =
        GST_VAAPI_DISPLAY_X11(display)->priv;

    if (priv->x11_display) {
        GST_VAAPI_DISPLAY_LOCK(display);
        XSync(priv->x11_display, False);
        GST_VAAPI_DISPLAY_UNLOCK(display);
    }
}

static void
gst_vaapi_display_x11_flush(GstVaapiDisplay *display)
{
    GstVaapiDisplayX11Private * const priv =
        GST_VAAPI_DISPLAY_X11(display)->priv;

    if (priv->x11_display) {
        GST_VAAPI_DISPLAY_LOCK(display);
        XFlush(priv->x11_display);
        GST_VAAPI_DISPLAY_UNLOCK(display);
    }
}

static gboolean
gst_vaapi_display_x11_get_display_info(
    GstVaapiDisplay     *display,
    GstVaapiDisplayInfo *info
)
{
    GstVaapiDisplayX11Private * const priv =
        GST_VAAPI_DISPLAY_X11(display)->priv;
    GstVaapiDisplayCache *cache;
    const GstVaapiDisplayInfo *cached_info;

    /* Return any cached info even if child has its own VA display */
    cache = gst_vaapi_display_get_cache();
    if (!cache)
        return FALSE;
    cached_info = gst_vaapi_display_cache_lookup_by_native_display(
        cache, priv->x11_display);
    if (cached_info) {
        *info = *cached_info;
        return TRUE;
    }

    /* Otherwise, create VA display if there is none already */
    info->native_display = priv->x11_display;
    info->display_name   = priv->display_name;
    if (!info->va_display) {
        info->va_display = vaGetDisplay(priv->x11_display);
        if (!info->va_display)
            return FALSE;
        info->display_type = GST_VAAPI_DISPLAY_TYPE_X11;
    }
    return TRUE;
}

static void
gst_vaapi_display_x11_get_size(
    GstVaapiDisplay *display,
    guint           *pwidth,
    guint           *pheight
)
{
    GstVaapiDisplayX11Private * const priv =
        GST_VAAPI_DISPLAY_X11(display)->priv;

    if (!priv->x11_display)
        return;

    if (pwidth)
        *pwidth = DisplayWidth(priv->x11_display, priv->x11_screen);

    if (pheight)
        *pheight = DisplayHeight(priv->x11_display, priv->x11_screen);
}

static void
gst_vaapi_display_x11_get_size_mm(
    GstVaapiDisplay *display,
    guint           *pwidth,
    guint           *pheight
)
{
    GstVaapiDisplayX11Private * const priv =
        GST_VAAPI_DISPLAY_X11(display)->priv;
    guint width_mm, height_mm;

    if (!priv->x11_display)
        return;

    width_mm  = DisplayWidthMM(priv->x11_display, priv->x11_screen);
    height_mm = DisplayHeightMM(priv->x11_display, priv->x11_screen);

#ifdef HAVE_XRANDR
    /* XXX: fix up physical size if the display is rotated */
    if (priv->use_xrandr) {
        XRRScreenConfiguration *xrr_config = NULL;
        XRRScreenSize *xrr_sizes;
        Window win;
        int num_xrr_sizes, size_id, screen;
        Rotation rotation;

        do {
            win    = DefaultRootWindow(priv->x11_display);
            screen = XRRRootToScreen(priv->x11_display, win);

            xrr_config = XRRGetScreenInfo(priv->x11_display, win);
            if (!xrr_config)
                break;

            size_id = XRRConfigCurrentConfiguration(xrr_config, &rotation);
            if (rotation == RR_Rotate_0 || rotation == RR_Rotate_180)
                break;

            xrr_sizes = XRRSizes(priv->x11_display, screen, &num_xrr_sizes);
            if (!xrr_sizes || size_id >= num_xrr_sizes)
                break;

            width_mm  = xrr_sizes[size_id].mheight;
            height_mm = xrr_sizes[size_id].mwidth;
        } while (0);
        if (xrr_config)
            XRRFreeScreenConfigInfo(xrr_config);
    }
#endif

    if (pwidth)
        *pwidth = width_mm;

    if (pheight)
        *pheight = height_mm;
}

static void
gst_vaapi_display_x11_class_init(GstVaapiDisplayX11Class *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDisplayClass * const dpy_class = GST_VAAPI_DISPLAY_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDisplayX11Private));

    object_class->finalize      = gst_vaapi_display_x11_finalize;
    object_class->set_property  = gst_vaapi_display_x11_set_property;
    object_class->get_property  = gst_vaapi_display_x11_get_property;
    object_class->constructed   = gst_vaapi_display_x11_constructed;

    dpy_class->open_display     = gst_vaapi_display_x11_open_display;
    dpy_class->close_display    = gst_vaapi_display_x11_close_display;
    dpy_class->sync             = gst_vaapi_display_x11_sync;
    dpy_class->flush            = gst_vaapi_display_x11_flush;
    dpy_class->get_display      = gst_vaapi_display_x11_get_display_info;
    dpy_class->get_size         = gst_vaapi_display_x11_get_size;
    dpy_class->get_size_mm      = gst_vaapi_display_x11_get_size_mm;

    /**
     * GstVaapiDisplayX11:synchronous:
     *
     * When enabled, runs the X display in synchronous mode. Note that
     * this is used only for debugging.
     */
    g_object_class_install_property
        (object_class,
         PROP_SYNCHRONOUS,
         g_param_spec_boolean("synchronous",
                              "Synchronous mode",
                              "Toggles X display synchronous mode",
                              FALSE,
                              G_PARAM_READWRITE));

    /**
     * GstVaapiDisplayX11:x11-display:
     *
     * The X11 #Display that was created by gst_vaapi_display_x11_new()
     * or that was bound from gst_vaapi_display_x11_new_with_display().
     */
    g_object_class_install_property
        (object_class,
         PROP_X11_DISPLAY,
         g_param_spec_pointer("x11-display",
                              "X11 display",
                              "X11 display",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    /**
     * GstVaapiDisplayX11:x11-screen:
     *
     * The X11 screen that was created by gst_vaapi_display_x11_new()
     * or that was bound from gst_vaapi_display_x11_new_with_display().
     */
    g_object_class_install_property
        (object_class,
         PROP_X11_SCREEN,
         g_param_spec_int("x11-screen",
                          "X11 screen",
                          "X11 screen",
                          0, G_MAXINT32, 0,
                          G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    /**
     * GstVaapiDisplayX11:display-name:
     *
     * The X11 display name.
     */
    g_object_class_install_property
        (object_class,
         PROP_DISPLAY_NAME,
         g_param_spec_string("display-name",
                             "X11 display name",
                             "X11 display name",
                             NULL,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_display_x11_init(GstVaapiDisplayX11 *display)
{
    GstVaapiDisplayX11Private *priv = GST_VAAPI_DISPLAY_X11_GET_PRIVATE(display);

    display->priv        = priv;
    priv->create_display = TRUE;
    priv->x11_display    = NULL;
    priv->x11_screen     = 0;
    priv->display_name   = NULL;
    priv->use_xrandr     = FALSE;
}

/**
 * gst_vaapi_display_x11_new:
 * @display_name: the X11 display name
 *
 * Opens an X11 #Display using @display_name and returns a newly
 * allocated #GstVaapiDisplay object. The X11 display will be cloed
 * when the reference count of the object reaches zero.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_x11_new(const gchar *display_name)
{
    return g_object_new(GST_VAAPI_TYPE_DISPLAY_X11,
                        "display-name", display_name,
                        NULL);
}

/**
 * gst_vaapi_display_x11_new_with_display:
 * @x11_display: an X11 #Display
 *
 * Creates a #GstVaapiDisplay based on the X11 @x11_display
 * display. The caller still owns the display and must call
 * XCloseDisplay() when all #GstVaapiDisplay references are
 * released. Doing so too early can yield undefined behaviour.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_x11_new_with_display(Display *x11_display)
{
    g_return_val_if_fail(x11_display, NULL);

    return g_object_new(GST_VAAPI_TYPE_DISPLAY_X11,
                        "x11-display", x11_display,
                        "x11-screen",  DefaultScreen(x11_display),
                        NULL);
}

/**
 * gst_vaapi_display_x11_get_display:
 * @display: a #GstVaapiDisplayX11
 *
 * Returns the underlying X11 #Display that was created by
 * gst_vaapi_display_x11_new() or that was bound from
 * gst_vaapi_display_x11_new_with_display().
 *
 * Return value: the X11 #Display attached to @display
 */
Display *
gst_vaapi_display_x11_get_display(GstVaapiDisplayX11 *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_X11(display), NULL);

    return display->priv->x11_display;
}

/**
 * gst_vaapi_display_x11_get_screen:
 * @display: a #GstVaapiDisplayX11
 *
 * Returns the default X11 screen that was created by
 * gst_vaapi_display_x11_new() or that was bound from
 * gst_vaapi_display_x11_new_with_display().
 *
 * Return value: the X11 #Display attached to @display
 */
int
gst_vaapi_display_x11_get_screen(GstVaapiDisplayX11 *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_X11(display), -1);

    return display->priv->x11_screen;
}
