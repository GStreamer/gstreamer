/*
 *  gstvaapiwindow.c - VA window abstraction
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
 * SECTION:gstvaapiwindow
 * @short_description: VA window abstraction
 */

#include "sysdeps.h"
#include "gstvaapiwindow.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiWindow, gst_vaapi_window, GST_VAAPI_TYPE_OBJECT);

#define GST_VAAPI_WINDOW_GET_PRIVATE(obj)                       \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_WINDOW,         \
                                 GstVaapiWindowPrivate))

struct _GstVaapiWindowPrivate {
    guint               width;
    guint               height;
    guint               display_width;
    guint               display_height;
    gboolean            is_constructed          : 1;
    guint               is_fullscreen           : 1;
    guint               check_geometry          : 1;
};

enum {
    PROP_0,

    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_FULLSCREEN
};

static void
gst_vaapi_window_ensure_size(GstVaapiWindow *window)
{
    GstVaapiWindowPrivate * const priv = window->priv;
    GstVaapiWindowClass * const klass  = GST_VAAPI_WINDOW_GET_CLASS(window);

    if (!priv->check_geometry)
        return;

    if (klass->get_geometry)
        klass->get_geometry(window, NULL, NULL, &priv->width, &priv->height);

    priv->check_geometry = FALSE;
    priv->is_fullscreen  = (priv->width  == priv->display_width &&
                            priv->height == priv->display_height);
}

static void
gst_vaapi_window_destroy(GstVaapiWindow *window)
{
    GST_VAAPI_WINDOW_GET_CLASS(window)->destroy(window);
}

static gboolean
gst_vaapi_window_create(GstVaapiWindow *window)
{
    GstVaapiWindowPrivate * const priv = window->priv;
    guint width, height;

    width  = priv->width;
    height = priv->height;

    gst_vaapi_display_get_size(
        GST_VAAPI_OBJECT_DISPLAY(window),
        &priv->display_width,
        &priv->display_height
    );

    if (!GST_VAAPI_WINDOW_GET_CLASS(window)->create(window, &width, &height))
        return FALSE;

    if (width != priv->width || height != priv->height) {
        GST_DEBUG("backend resized window to %ux%u", width, height);
        priv->width  = width;
        priv->height = height;
    }
    return TRUE;
}

static void
gst_vaapi_window_finalize(GObject *object)
{
    gst_vaapi_window_destroy(GST_VAAPI_WINDOW(object));

    G_OBJECT_CLASS(gst_vaapi_window_parent_class)->finalize(object);
}

static void
gst_vaapi_window_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiWindow * const window = GST_VAAPI_WINDOW(object);

    switch (prop_id) {
    case PROP_WIDTH:
        gst_vaapi_window_set_width(window, g_value_get_uint(value));
        break;
    case PROP_HEIGHT:
        gst_vaapi_window_set_height(window, g_value_get_uint(value));
        break;
    case PROP_FULLSCREEN:
        gst_vaapi_window_set_fullscreen(window, g_value_get_boolean(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_window_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiWindow * const window = GST_VAAPI_WINDOW(object);

    switch (prop_id) {
    case PROP_WIDTH:
        g_value_set_uint(value, gst_vaapi_window_get_width(window));
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, gst_vaapi_window_get_height(window));
        break;
    case PROP_FULLSCREEN:
        g_value_set_boolean(value, window->priv->is_fullscreen);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_window_constructed(GObject *object)
{
    GstVaapiWindow * const window = GST_VAAPI_WINDOW(object);
    GObjectClass *parent_class;

    window->priv->is_constructed = gst_vaapi_window_create(window);

    parent_class = G_OBJECT_CLASS(gst_vaapi_window_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
gst_vaapi_window_class_init(GstVaapiWindowClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiWindowPrivate));

    object_class->finalize     = gst_vaapi_window_finalize;
    object_class->set_property = gst_vaapi_window_set_property;
    object_class->get_property = gst_vaapi_window_get_property;
    object_class->constructed  = gst_vaapi_window_constructed;

    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "Width",
                           "The window width",
                           1, G_MAXUINT32, 1,
                           G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "height",
                           "The window height",
                           1, G_MAXUINT32, 1,
                           G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class,
         PROP_FULLSCREEN,
         g_param_spec_boolean("fullscreen",
                              "Fullscreen",
                              "The fullscreen state of the window",
                              FALSE,
                              G_PARAM_READWRITE));
}

static void
gst_vaapi_window_init(GstVaapiWindow *window)
{
    GstVaapiWindowPrivate *priv = GST_VAAPI_WINDOW_GET_PRIVATE(window);

    window->priv                = priv;
    priv->width                 = 1;
    priv->height                = 1;
    priv->is_constructed        = FALSE;
    priv->is_fullscreen         = FALSE;
    priv->check_geometry        = FALSE;
}

/**
 * gst_vaapi_window_get_display:
 * @window: a #GstVaapiWindow
 *
 * Returns the #GstVaapiDisplay this @window is bound to.
 *
 * Return value: the parent #GstVaapiDisplay object
 */
GstVaapiDisplay *
gst_vaapi_window_get_display(GstVaapiWindow *window)
{
    g_return_val_if_fail(GST_VAAPI_IS_WINDOW(window), NULL);

    return GST_VAAPI_OBJECT_DISPLAY(window);
}

/**
 * gst_vaapi_window_show:
 * @window: a #GstVaapiWindow
 *
 * Flags a window to be displayed. Any window that is not shown will
 * not appear on the screen.
 */
void
gst_vaapi_window_show(GstVaapiWindow *window)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));
    g_return_if_fail(window->priv->is_constructed);

    GST_VAAPI_WINDOW_GET_CLASS(window)->show(window);
    window->priv->check_geometry = TRUE;
}

/**
 * gst_vaapi_window_hide:
 * @window: a #GstVaapiWindow
 *
 * Reverses the effects of gst_vaapi_window_show(), causing the window
 * to be hidden (invisible to the user).
 */
void
gst_vaapi_window_hide(GstVaapiWindow *window)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));
    g_return_if_fail(window->priv->is_constructed);

    GST_VAAPI_WINDOW_GET_CLASS(window)->hide(window);
}

/**
 * gst_vaapi_window_get_fullscreen:
 * @window: a #GstVaapiWindow
 *
 * Retrieves whether the @window is fullscreen or not
 *
 * Return value: %TRUE if the window is fullscreen
 */
gboolean
gst_vaapi_window_get_fullscreen(GstVaapiWindow *window)
{
    g_return_val_if_fail(GST_VAAPI_IS_WINDOW(window), FALSE);

    gst_vaapi_window_ensure_size(window);

    return window->priv->is_fullscreen;
}

/**
 * gst_vaapi_window_set_fullscreen:
 * @window: a #GstVaapiWindow
 * @fullscreen: %TRUE to request window to get fullscreen
 *
 * Requests to place the @window in fullscreen or unfullscreen states.
 */
void
gst_vaapi_window_set_fullscreen(GstVaapiWindow *window, gboolean fullscreen)
{
    GstVaapiWindowClass *klass;

    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));

    klass = GST_VAAPI_WINDOW_GET_CLASS(window);

    if (window->priv->is_fullscreen != fullscreen &&
        klass->set_fullscreen && klass->set_fullscreen(window, fullscreen)) {
        window->priv->is_fullscreen  = fullscreen;
        window->priv->check_geometry = TRUE;
    }
}

/**
 * gst_vaapi_window_get_width:
 * @window: a #GstVaapiWindow
 *
 * Retrieves the width of a #GstVaapiWindow.
 *
 * Return value: the width of the @window, in pixels
 */
guint
gst_vaapi_window_get_width(GstVaapiWindow *window)
{
    g_return_val_if_fail(GST_VAAPI_IS_WINDOW(window), 0);
    g_return_val_if_fail(window->priv->is_constructed, 0);

    gst_vaapi_window_ensure_size(window);

    return window->priv->width;
}

/**
 * gst_vaapi_window_get_height:
 * @window: a #GstVaapiWindow
 *
 * Retrieves the height of a #GstVaapiWindow
 *
 * Return value: the height of the @window, in pixels
 */
guint
gst_vaapi_window_get_height(GstVaapiWindow *window)
{
    g_return_val_if_fail(GST_VAAPI_IS_WINDOW(window), 0);
    g_return_val_if_fail(window->priv->is_constructed, 0);

    gst_vaapi_window_ensure_size(window);

    return window->priv->height;
}

/**
 * gst_vaapi_window_get_size:
 * @window: a #GstVaapiWindow
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiWindow.
 */
void
gst_vaapi_window_get_size(GstVaapiWindow *window, guint *pwidth, guint *pheight)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));
    g_return_if_fail(window->priv->is_constructed);

    gst_vaapi_window_ensure_size(window);

    if (pwidth)
        *pwidth = window->priv->width;

    if (pheight)
        *pheight = window->priv->height;
}

/**
 * gst_vaapi_window_set_width:
 * @window: a #GstVaapiWindow
 * @width: requested new width for the window, in pixels
 *
 * Resizes the @window to match the specified @width.
 */
void
gst_vaapi_window_set_width(GstVaapiWindow *window, guint width)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));

    gst_vaapi_window_set_size(window, width, window->priv->height);
}

/**
 * gst_vaapi_window_set_height:
 * @window: a #GstVaapiWindow
 * @height: requested new height for the window, in pixels
 *
 * Resizes the @window to match the specified @height.
 */
void
gst_vaapi_window_set_height(GstVaapiWindow *window, guint height)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));

    gst_vaapi_window_set_size(window, window->priv->width, height);
}

/**
 * gst_vaapi_window_set_size:
 * @window: a #GstVaapiWindow
 * @width: requested new width for the window, in pixels
 * @height: requested new height for the window, in pixels
 *
 * Resizes the @window to match the specified @width and @height.
 */
void
gst_vaapi_window_set_size(GstVaapiWindow *window, guint width, guint height)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));

    if (width == window->priv->width && height == window->priv->height)
        return;

    window->priv->width  = width;
    window->priv->height = height;

    if (window->priv->is_constructed)
        GST_VAAPI_WINDOW_GET_CLASS(window)->resize(window, width, height);
}

static inline void
get_surface_rect(GstVaapiSurface *surface, GstVaapiRectangle *rect)
{
    guint width, height;

    gst_vaapi_surface_get_size(surface, &width, &height);
    rect->x      = 0;
    rect->y      = 0;
    rect->width  = width;
    rect->height = height;
}

static inline void
get_window_rect(GstVaapiWindow *window, GstVaapiRectangle *rect)
{
    guint width, height;

    gst_vaapi_window_get_size(window, &width, &height);
    rect->x      = 0;
    rect->y      = 0;
    rect->width  = width;
    rect->height = height;
}

/**
 * gst_vaapi_window_put_surface:
 * @window: a #GstVaapiWindow
 * @surface: a #GstVaapiSurface
 * @src_rect: the sub-rectangle of the source surface to
 *   extract and process. If %NULL, the entire surface will be used.
 * @dst_rect: the sub-rectangle of the destination
 *   window into which the surface is rendered. If %NULL, the entire
 *   window will be used.
 * @flags: postprocessing flags. See #GstVaapiSurfaceRenderFlags
 *
 * Renders the @surface region specified by @src_rect into the @window
 * region specified by @dst_rect. The @flags specify how de-interlacing
 * (if needed), color space conversion, scaling and other postprocessing
 * transformations are performed.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_window_put_surface(
    GstVaapiWindow          *window,
    GstVaapiSurface         *surface,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect,
    guint                    flags
)
{
    GstVaapiWindowClass *klass;
    GstVaapiRectangle src_rect_default, dst_rect_default;

    g_return_val_if_fail(GST_VAAPI_IS_WINDOW(window), FALSE);
    g_return_val_if_fail(window->priv->is_constructed, FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);

    klass = GST_VAAPI_WINDOW_GET_CLASS(window);
    if (!klass->render)
        return FALSE;

    if (!src_rect) {
        src_rect = &src_rect_default;
        get_surface_rect(surface, &src_rect_default);
    }

    if (!dst_rect) {
        dst_rect = &dst_rect_default;
        get_window_rect(window, &dst_rect_default);
    }

    return klass->render(window, surface, src_rect, dst_rect, flags);
}
