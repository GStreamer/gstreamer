/*
 *  gstvaapidisplay_wayland.c - VA/Wayland display abstraction
 *
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
 * SECTION:gstvaapidisplay_wayland
 * @short_description: VA/Wayland display abstraction
 */

#include "sysdeps.h"
#include <string.h>
#include "gstvaapidisplay_priv.h"
#include "gstvaapidisplay_wayland.h"
#include "gstvaapidisplay_wayland_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiDisplayWayland,
              gst_vaapi_display_wayland,
              GST_VAAPI_TYPE_DISPLAY);

enum {
    PROP_0,

    PROP_DISPLAY_NAME,
    PROP_WL_DISPLAY
};

#define NAME_PREFIX "WLD:"
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
        g_display_name = getenv("WAYLAND_DISPLAY");
    return g_display_name;
}

static inline guint
get_display_name_length(const gchar *display_name)
{
    const gchar *str;

    str = strchr(display_name, '-');
    if (str)
        return str - display_name;
    return strlen(display_name);
}

static gboolean
compare_display_name(gconstpointer a, gconstpointer b, gpointer user_data)
{
    const gchar *cached_name = a;
    const gchar *tested_name = b;
    guint cached_name_length, tested_name_length;

    if (!cached_name || !is_display_name(cached_name))
        return FALSE;
    cached_name += NAME_PREFIX_LENGTH;
    cached_name_length = get_display_name_length(cached_name);

    g_return_val_if_fail(tested_name && is_display_name(tested_name), FALSE);
    tested_name += NAME_PREFIX_LENGTH;
    tested_name_length = get_display_name_length(tested_name);

    /* XXX: handle screen number and default WAYLAND_DISPLAY name */
    if (cached_name_length != tested_name_length)
        return FALSE;
    if (strncmp(cached_name, tested_name, cached_name_length) != 0)
        return FALSE;
    return TRUE;
}

static void
gst_vaapi_display_wayland_finalize(GObject *object)
{
    G_OBJECT_CLASS(gst_vaapi_display_wayland_parent_class)->finalize(object);
}

/* Reconstruct a display name without our prefix */
static const gchar *
get_display_name(gpointer ptr)
{
    GstVaapiDisplayWayland * const display = GST_VAAPI_DISPLAY_WAYLAND(ptr);
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
set_display_name(GstVaapiDisplayWayland *display, const gchar *display_name)
{
    GstVaapiDisplayWaylandPrivate * const priv = display->priv;

    g_free(priv->display_name);

    if (!display_name) {
        display_name = get_default_display_name();
        if (!display_name)
            display_name = "";
    }
    priv->display_name = g_strdup_printf("%s%s", NAME_PREFIX, display_name);
}

static void
gst_vaapi_display_wayland_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiDisplayWayland * const display = GST_VAAPI_DISPLAY_WAYLAND(object);

    switch (prop_id) {
    case PROP_DISPLAY_NAME:
        set_display_name(display, g_value_get_string(value));
        break;
    case PROP_WL_DISPLAY:
        display->priv->wl_display = g_value_get_pointer(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}
static void
gst_vaapi_display_wayland_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiDisplayWayland * const display = GST_VAAPI_DISPLAY_WAYLAND(object);

    switch (prop_id) {
    case PROP_DISPLAY_NAME:
        g_value_set_string(value, get_display_name(display));
        break;
    case PROP_WL_DISPLAY:
        g_value_set_pointer(value, gst_vaapi_display_wayland_get_display(display));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_display_wayland_constructed(GObject *object)
{
    GstVaapiDisplayWayland * const display = GST_VAAPI_DISPLAY_WAYLAND(object);
    GstVaapiDisplayWaylandPrivate * const priv = display->priv;
    GstVaapiDisplayCache * const cache = gst_vaapi_display_get_cache();
    const GstVaapiDisplayInfo *info;
    GObjectClass *parent_class;

    priv->create_display = priv->wl_display == NULL;

    /* Don't create Wayland display if there is one in the cache already */
    if (priv->create_display) {
        info = gst_vaapi_display_cache_lookup_by_name(
            cache,
            priv->display_name,
            compare_display_name, NULL
        );
        if (info) {
            priv->wl_display     = info->native_display;
            priv->create_display = FALSE;
        }
    }

    /* Reset display-name if the user provided his own Wayland display */
    if (!priv->create_display) {
        /* XXX: how to get socket/display name? */
        GST_WARNING("wayland: get display name");
        set_display_name(display, NULL);
    }

    parent_class = G_OBJECT_CLASS(gst_vaapi_display_wayland_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
output_handle_geometry(void *data, struct wl_output *output,
                       int x, int y, int physical_width, int physical_height,
                       int subpixel, const char *make, const char *model,
                       int transform)
{
    GstVaapiDisplayWaylandPrivate * const priv = data;

    priv->phys_width  = physical_width;
    priv->phys_height = physical_height;
}

static void
output_handle_mode(void *data, struct wl_output *wl_output,
                   uint32_t flags, int width, int height, int refresh)
{
    GstVaapiDisplayWaylandPrivate * const priv = data;

    if (flags & WL_OUTPUT_MODE_CURRENT) {
        priv->width  = width;
        priv->height = height;
    }
}

static const struct wl_output_listener output_listener = {
    output_handle_geometry,
    output_handle_mode,
};

static void
display_handle_global(
    struct wl_display *display,
    uint32_t           id,
    const char        *interface,
    uint32_t           version,
    void              *data
)
{
    GstVaapiDisplayWaylandPrivate * const priv = data;

    if (strcmp(interface, "wl_compositor") == 0)
        priv->compositor = wl_display_bind(display, id, &wl_compositor_interface);
    else if (strcmp(interface, "wl_shell") == 0)
        priv->shell = wl_display_bind(display, id, &wl_shell_interface);
    else if (strcmp(interface, "wl_output") == 0) {
        priv->output = wl_display_bind(display, id, &wl_output_interface);
        wl_output_add_listener(priv->output, &output_listener, priv);
    }
}

static int
event_mask_update(uint32_t mask, void *data)
{
    GstVaapiDisplayWaylandPrivate * const priv = data;

    priv->event_mask = mask;
    return 0;
}

static gboolean
gst_vaapi_display_wayland_open_display(GstVaapiDisplay * display)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;

    if (!priv->create_display)
        return priv->wl_display != NULL;

    priv->wl_display = wl_display_connect(get_display_name(display));
    if (!priv->wl_display)
        return FALSE;

    wl_display_set_user_data(priv->wl_display, priv);
    wl_display_add_global_listener(priv->wl_display, display_handle_global, priv);
    priv->event_fd = wl_display_get_fd(priv->wl_display, event_mask_update, priv);
    wl_display_iterate(priv->wl_display, priv->event_mask);
    wl_display_roundtrip(priv->wl_display);

    if (!priv->compositor) {
        GST_ERROR("failed to bind compositor interface");
        return FALSE;
    }

    if (!priv->shell) {
        GST_ERROR("failed to bind shell interface");
        return FALSE;
    }
    return TRUE;
}

static void
gst_vaapi_display_wayland_close_display(GstVaapiDisplay * display)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;

    if (priv->compositor) {
        wl_compositor_destroy(priv->compositor);
        priv->compositor = NULL;
    }

    if (priv->wl_display) {
        if (priv->create_display)
            wl_display_disconnect(priv->wl_display);
        priv->wl_display = NULL;
    }

    if (priv->display_name) {
        g_free(priv->display_name);
        priv->display_name = NULL;
    }
}

static gboolean
gst_vaapi_display_wayland_get_display_info(
    GstVaapiDisplay     *display,
    GstVaapiDisplayInfo *info
)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;
    GstVaapiDisplayCache *cache;
    const GstVaapiDisplayInfo *cached_info;

    /* Return any cached info even if child has its own VA display */
    cache = gst_vaapi_display_get_cache();
    if (!cache)
        return FALSE;
    cached_info =
        gst_vaapi_display_cache_lookup_by_native_display(cache, priv->wl_display);
    if (cached_info) {
        *info = *cached_info;
        return TRUE;
    }

    /* Otherwise, create VA display if there is none already */
    info->native_display = priv->wl_display;
    info->display_name   = priv->display_name;
    if (!info->va_display) {
        info->va_display = vaGetDisplayWl(priv->wl_display);
        if (!info->va_display)
            return FALSE;
        info->display_type = GST_VAAPI_DISPLAY_TYPE_WAYLAND;
    }
    return TRUE;
}

static void
gst_vaapi_display_wayland_get_size(
    GstVaapiDisplay *display,
    guint           *pwidth,
    guint           *pheight
)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;

    if (!priv->output)
        return;

    if (pwidth)
        *pwidth = priv->width;

    if (pheight)
        *pheight = priv->height;
}

static void
gst_vaapi_display_wayland_get_size_mm(
    GstVaapiDisplay *display,
    guint           *pwidth,
    guint           *pheight
)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND(display)->priv;

    if (!priv->output)
        return;

    if (pwidth)
        *pwidth = priv->phys_width;

    if (pheight)
        *pheight = priv->phys_height;
}

static void
gst_vaapi_display_wayland_class_init(GstVaapiDisplayWaylandClass * klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiDisplayClass * const dpy_class = GST_VAAPI_DISPLAY_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiDisplayWaylandPrivate));

    object_class->finalize      = gst_vaapi_display_wayland_finalize;
    object_class->set_property  = gst_vaapi_display_wayland_set_property;
    object_class->get_property  = gst_vaapi_display_wayland_get_property;
    object_class->constructed   = gst_vaapi_display_wayland_constructed;

    dpy_class->open_display     = gst_vaapi_display_wayland_open_display;
    dpy_class->close_display    = gst_vaapi_display_wayland_close_display;
    dpy_class->get_display      = gst_vaapi_display_wayland_get_display_info;
    dpy_class->get_size         = gst_vaapi_display_wayland_get_size;
    dpy_class->get_size_mm      = gst_vaapi_display_wayland_get_size_mm;

    /**
     * GstVaapiDisplayWayland:wayland-display:
     *
     * The Wayland #wl_display that was created by
     * gst_vaapi_display_wayland_new() or that was bound from
     * gst_vaapi_display_wayland_new_with_display().
     */
    g_object_class_install_property
        (object_class,
         PROP_WL_DISPLAY,
         g_param_spec_pointer("wl-display",
                              "Wayland display",
                              "Wayland display",
                              G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    /**
     * GstVaapiDisplayWayland:display-name:
     *
     * The Wayland display name.
     */
    g_object_class_install_property
        (object_class,
         PROP_DISPLAY_NAME,
         g_param_spec_string("display-name",
                             "Wayland display name",
                             "Wayland display name",
                             NULL,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_display_wayland_init(GstVaapiDisplayWayland *display)
{
    GstVaapiDisplayWaylandPrivate * const priv =
        GST_VAAPI_DISPLAY_WAYLAND_GET_PRIVATE(display);

    display->priv        = priv;
    priv->create_display = TRUE;
    priv->display_name   = NULL;
    priv->wl_display     = NULL;
    priv->compositor     = NULL;
    priv->shell          = NULL;
    priv->output         = NULL;
    priv->width          = 0;
    priv->height         = 0;
    priv->phys_width     = 0;
    priv->phys_height    = 0;
    priv->event_fd       = -1;
    priv->event_mask     = 0;
}

/**
 * gst_vaapi_display_wayland_new:
 * @display_name: the Wayland display name
 *
 * Opens an Wayland #wl_display using @display_name and returns a
 * newly allocated #GstVaapiDisplay object. The Wayland display will
 * be cloed when the reference count of the object reaches zero.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_wayland_new(const gchar *display_name)
{
    return g_object_new(GST_VAAPI_TYPE_DISPLAY_WAYLAND,
                        "display-name", display_name,
                        NULL);
}

/**
 * gst_vaapi_display_wayland_new_with_display:
 * @wl_display: an Wayland #wl_display
 *
 * Creates a #GstVaapiDisplay based on the Wayland @wl_display
 * display. The caller still owns the display and must call
 * wl_display_disconnect() when all #GstVaapiDisplay references are
 * released. Doing so too early can yield undefined behaviour.
 *
 * Return value: a newly allocated #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_display_wayland_new_with_display(struct wl_display *wl_display)
{
    g_return_val_if_fail(wl_display, NULL);

    return g_object_new(GST_VAAPI_TYPE_DISPLAY_WAYLAND,
                        "wl-display", wl_display,
                        NULL);
}

/**
 * gst_vaapi_display_wayland_get_display:
 * @display: a #GstVaapiDisplayWayland
 *
 * Returns the underlying Wayland #wl_display that was created by
 * gst_vaapi_display_wayland_new() or that was bound from
 * gst_vaapi_display_wayland_new_with_display().
 *
 * Return value: the Wayland #wl_display attached to @display
 */
struct wl_display *
gst_vaapi_display_wayland_get_display(GstVaapiDisplayWayland *display)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY_WAYLAND(display), NULL);

    return display->priv->wl_display;
}
