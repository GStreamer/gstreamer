/*
 *  gstvaapiwindow_wayland.c - VA/Wayland window abstraction
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
 * SECTION:gstvaapiwindow_wayland
 * @short_description: VA/Wayland window abstraction
 */

#include "sysdeps.h"
#include <string.h>
#include "gstvaapicompat.h"
#include "gstvaapiwindow_wayland.h"
#include "gstvaapidisplay_wayland.h"
#include "gstvaapidisplay_wayland_priv.h"
#include "gstvaapiutils.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiWindowWayland,
              gst_vaapi_window_wayland,
              GST_VAAPI_TYPE_WINDOW);

#define GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE(obj)               \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_WINDOW_WAYLAND, \
                                 GstVaapiWindowWaylandPrivate))

struct _GstVaapiWindowWaylandPrivate {
    struct wl_shell_surface    *shell_surface;
    struct wl_surface          *surface;
    struct wl_buffer           *buffer;
    guint                       redraw_pending  : 1;
};

static gboolean
gst_vaapi_window_wayland_show(GstVaapiWindow *window)
{
    GST_WARNING("unimplemented GstVaapiWindowWayland::show()");

    return TRUE;
}

static gboolean
gst_vaapi_window_wayland_hide(GstVaapiWindow *window)
{
    GST_WARNING("unimplemented GstVaapiWindowWayland::hide()");

    return TRUE;
}

static void
handle_ping(void *data, struct wl_shell_surface *shell_surface,
            uint32_t serial)
{
    wl_shell_surface_pong(shell_surface, serial);
}

static void
handle_configure(void *data, struct wl_shell_surface *shell_surface,
                 uint32_t edges, int32_t width, int32_t height)
{
}

static void
handle_popup_done(void *data, struct wl_shell_surface *shell_surface)
{
}

static const struct wl_shell_surface_listener shell_surface_listener = {
    handle_ping,
    handle_configure,
    handle_popup_done
};

static gboolean
gst_vaapi_window_wayland_create(
    GstVaapiWindow *window,
    guint          *width,
    guint          *height
)
{
    GstVaapiWindowWaylandPrivate * const priv =
        GST_VAAPI_WINDOW_WAYLAND(window)->priv;
    GstVaapiDisplayWaylandPrivate * const priv_display =
        GST_VAAPI_OBJECT_DISPLAY_WAYLAND(window)->priv;

    GST_DEBUG("create window, size %ux%u", *width, *height);

    g_return_val_if_fail(priv_display->compositor != NULL, FALSE);
    g_return_val_if_fail(priv_display->shell != NULL, FALSE);

    priv->surface = wl_compositor_create_surface(priv_display->compositor);
    if (!priv->surface)
        return FALSE;

    priv->shell_surface =
        wl_shell_get_shell_surface(priv_display->shell, priv->surface);
    if (!priv->shell_surface)
        return FALSE;

    wl_shell_surface_add_listener(priv->shell_surface,
                                  &shell_surface_listener, priv);
    wl_shell_surface_set_toplevel(priv->shell_surface);
    wl_shell_surface_set_fullscreen(
        priv->shell_surface,
        WL_SHELL_SURFACE_FULLSCREEN_METHOD_SCALE,
        0,
        NULL
    );

    priv->redraw_pending = FALSE;
    return TRUE;
}

static void
gst_vaapi_window_wayland_destroy(GstVaapiWindow * window)
{
    GstVaapiWindowWaylandPrivate * const priv =
        GST_VAAPI_WINDOW_WAYLAND(window)->priv;

    if (priv->shell_surface) {
  	wl_shell_surface_destroy(priv->shell_surface);
        priv->shell_surface = NULL;
    }

    if (priv->surface) {
        wl_surface_destroy(priv->surface);
        priv->surface = NULL;
    }

    if (priv->buffer) {
   	wl_buffer_destroy(priv->buffer);
        priv->buffer = NULL;
    }
}

static gboolean
gst_vaapi_window_wayland_resize(
    GstVaapiWindow * window,
    guint            width,
    guint            height
)
{
    GST_DEBUG("resize window, new size %ux%u", width, height);
    return TRUE;
}

static void
frame_redraw_callback(void *data, struct wl_callback *callback, uint32_t time)
{
    GstVaapiWindowWaylandPrivate * const priv = data;

    priv->redraw_pending = FALSE;
    wl_buffer_destroy(priv->buffer);
    priv->buffer = NULL;
    wl_callback_destroy(callback);
}

static const struct wl_callback_listener frame_callback_listener = {
    frame_redraw_callback
};

static gboolean
gst_vaapi_window_wayland_render(
    GstVaapiWindow          *window,
    GstVaapiSurface         *surface,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect,
    guint                    flags
)
{
    GstVaapiWindowWaylandPrivate * const priv =
        GST_VAAPI_WINDOW_WAYLAND(window)->priv;
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(window);
    struct wl_display * const wl_display = GST_VAAPI_OBJECT_WL_DISPLAY(window);
    struct wl_buffer *buffer;
    struct wl_callback *callback;
    guint width, height, va_flags;
    VASurfaceID surface_id;
    VAStatus status;

    /* XXX: use VPP to support unusual source and destination rectangles */
    gst_vaapi_surface_get_size(surface, &width, &height);
    if (src_rect->x      != 0     ||
        src_rect->y      != 0     ||
        src_rect->width  != width ||
        src_rect->height != height) {
        GST_ERROR("unsupported source rectangle for rendering");
        return FALSE;
    }

    if (0 && (dst_rect->width != width || dst_rect->height != height)) {
        GST_ERROR("unsupported target rectangle for rendering");
        return FALSE;
    }

    surface_id = GST_VAAPI_OBJECT_ID(surface);
    if (surface_id == VA_INVALID_ID)
        return FALSE;

    GST_VAAPI_OBJECT_LOCK_DISPLAY(window);

    /* Wait for the previous frame to complete redraw */
    if (priv->redraw_pending) 
	wl_display_iterate(wl_display, WL_DISPLAY_READABLE);

    /* XXX: use VA/VPP for other filters */
    va_flags = from_GstVaapiSurfaceRenderFlags(flags);
    status = vaGetSurfaceBufferWl(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        surface_id,
        va_flags & (VA_TOP_FIELD|VA_BOTTOM_FIELD),
        &buffer
    );
    if (status == VA_STATUS_ERROR_FLAG_NOT_SUPPORTED) {
        /* XXX: de-interlacing flags not supported, try with VPP? */
        status = vaGetSurfaceBufferWl(
            GST_VAAPI_DISPLAY_VADISPLAY(display),
            surface_id,
            VA_FRAME_PICTURE,
            &buffer
        );
    }
    if (!vaapi_check_status(status, "vaGetSurfaceBufferWl()"))
        return FALSE;

    /* XXX: attach to the specified target rectangle */
    wl_surface_attach(priv->surface, buffer, 0, 0);
    wl_surface_damage(priv->surface, 0, 0, width, height);

    wl_display_iterate(wl_display, WL_DISPLAY_WRITABLE);
    priv->redraw_pending = TRUE;
    priv->buffer = buffer;

    callback = wl_surface_frame(priv->surface);
    wl_callback_add_listener(callback, &frame_callback_listener, priv);
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(window);
    return TRUE;
}

static void
gst_vaapi_window_wayland_finalize(GObject *object)
{
    G_OBJECT_CLASS(gst_vaapi_window_wayland_parent_class)->finalize(object);
}

static void
gst_vaapi_window_wayland_constructed(GObject *object)
{
    GObjectClass *parent_class;

    parent_class = G_OBJECT_CLASS(gst_vaapi_window_wayland_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
gst_vaapi_window_wayland_class_init(GstVaapiWindowWaylandClass * klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);
    GstVaapiWindowClass * const window_class = GST_VAAPI_WINDOW_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiWindowWaylandPrivate));

    object_class->finalize      = gst_vaapi_window_wayland_finalize;
    object_class->constructed   = gst_vaapi_window_wayland_constructed;

    window_class->create        = gst_vaapi_window_wayland_create;
    window_class->destroy       = gst_vaapi_window_wayland_destroy;
    window_class->show          = gst_vaapi_window_wayland_show;
    window_class->hide          = gst_vaapi_window_wayland_hide;
    window_class->render        = gst_vaapi_window_wayland_render;
    window_class->resize        = gst_vaapi_window_wayland_resize;
}

static void
gst_vaapi_window_wayland_init(GstVaapiWindowWayland * window)
{
    GstVaapiWindowWaylandPrivate *priv =
        GST_VAAPI_WINDOW_WAYLAND_GET_PRIVATE(window);

    window->priv         = priv;
    priv->shell_surface  = NULL;
    priv->surface        = NULL;
    priv->buffer         = NULL;
    priv->redraw_pending = FALSE;
}

/**
 * gst_vaapi_window_wayland_new:
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
gst_vaapi_window_wayland_new(
    GstVaapiDisplay *display,
    guint            width,
    guint            height
)
{
    GST_DEBUG("new window, size %ux%u", width, height);

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(width  > 0, NULL);
    g_return_val_if_fail(height > 0, NULL);

    return g_object_new(GST_VAAPI_TYPE_WINDOW_WAYLAND,
                        "display", display,
                        "id",      GST_VAAPI_ID(0),
                        "width",   width,
                        "height",  height,
                        NULL);
}
