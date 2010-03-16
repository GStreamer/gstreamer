/*
 *  gstvaapiwindow.c - VA window abstraction
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
#include "gstvaapiwindow.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiWindow, gst_vaapi_window, G_TYPE_OBJECT);

#define GST_VAAPI_WINDOW_GET_PRIVATE(obj)                       \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_WINDOW,         \
                                 GstVaapiWindowPrivate))

struct _GstVaapiWindowPrivate {
    gboolean    is_constructed;
    guint       width;
    guint       height;
};

enum {
    PROP_0,

    PROP_WIDTH,
    PROP_HEIGHT
};

static void
gst_vaapi_window_destroy(GstVaapiWindow *window)
{
    GST_VAAPI_WINDOW_GET_CLASS(window)->destroy(window);
}

static gboolean
gst_vaapi_window_create(GstVaapiWindow *window, guint width, guint height)
{
    if (width == 0 || height == 0)
        return FALSE;

    return GST_VAAPI_WINDOW_GET_CLASS(window)->create(window, width, height);
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

    window->priv->is_constructed = gst_vaapi_window_create(
        window,
        window->priv->width,
        window->priv->height
    );

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
                           "width",
                           "Width",
                           1, G_MAXUINT32, 1,
                           G_PARAM_READWRITE));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "height",
                           "Height",
                           1, G_MAXUINT32, 1,
                           G_PARAM_READWRITE));
}

static void
gst_vaapi_window_init(GstVaapiWindow *window)
{
    GstVaapiWindowPrivate *priv = GST_VAAPI_WINDOW_GET_PRIVATE(window);

    window->priv                = priv;
    priv->is_constructed        = FALSE;
    priv->width                 = 1;
    priv->height                = 1;
}

void
gst_vaapi_window_show(GstVaapiWindow *window)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));
    g_return_if_fail(window->priv->is_constructed);

    GST_VAAPI_WINDOW_GET_CLASS(window)->show(window);
}

void
gst_vaapi_window_hide(GstVaapiWindow *window)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));
    g_return_if_fail(window->priv->is_constructed);

    GST_VAAPI_WINDOW_GET_CLASS(window)->hide(window);
}

guint
gst_vaapi_window_get_width(GstVaapiWindow *window)
{
    g_return_val_if_fail(GST_VAAPI_IS_WINDOW(window), 0);
    g_return_val_if_fail(window->priv->is_constructed, 0);

    return window->priv->width;
}

guint
gst_vaapi_window_get_height(GstVaapiWindow *window)
{
    g_return_val_if_fail(GST_VAAPI_IS_WINDOW(window), 0);
    g_return_val_if_fail(window->priv->is_constructed, 0);

    return window->priv->height;
}

void
gst_vaapi_window_get_size(GstVaapiWindow *window, guint *pwidth, guint *pheight)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));
    g_return_if_fail(window->priv->is_constructed);

    if (pwidth)
        *pwidth = window->priv->width;

    if (pheight)
        *pheight = window->priv->height;
}

void
gst_vaapi_window_set_width(GstVaapiWindow *window, guint width)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));

    gst_vaapi_window_set_size(window, width, window->priv->height);
}

void
gst_vaapi_window_set_height(GstVaapiWindow *window, guint height)
{
    g_return_if_fail(GST_VAAPI_IS_WINDOW(window));

    gst_vaapi_window_set_size(window, window->priv->width, height);
}

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
get_surface_rect(GstVaapiSurface *surface, GstVideoRectangle *rect)
{
    guint width, height;

    gst_vaapi_surface_get_size(surface, &width, &height);
    rect->x = 0;
    rect->y = 0;
    rect->w = width;
    rect->h = height;
}

static inline void
get_window_rect(GstVaapiWindow *window, GstVideoRectangle *rect)
{
    guint width, height;

    gst_vaapi_window_get_size(window, &width, &height);
    rect->x = 0;
    rect->y = 0;
    rect->w = width;
    rect->h = height;
}

gboolean
gst_vaapi_window_put_surface(
    GstVaapiWindow          *window,
    GstVaapiSurface         *surface,
    guint                    flags
)
{
    GstVideoRectangle src_rect, dst_rect;

    g_return_val_if_fail(GST_VAAPI_IS_WINDOW(window), FALSE);
    g_return_val_if_fail(window->priv->is_constructed, FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);

    get_surface_rect(surface, &src_rect);
    get_window_rect(window, &dst_rect);

    return GST_VAAPI_WINDOW_GET_CLASS(window)->render(window,
                                                      surface,
                                                      &src_rect,
                                                      &dst_rect,
                                                      flags);
}

gboolean
gst_vaapi_window_put_surface_full(
    GstVaapiWindow          *window,
    GstVaapiSurface         *surface,
    const GstVideoRectangle *src_rect,
    const GstVideoRectangle *dst_rect,
    guint                    flags
)
{
    GstVideoRectangle src_rect_default, dst_rect_default;

    g_return_val_if_fail(GST_VAAPI_IS_WINDOW(window), FALSE);
    g_return_val_if_fail(window->priv->is_constructed, FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);

    if (!src_rect) {
        src_rect = &src_rect_default;
        get_surface_rect(surface, &src_rect_default);
    }

    if (!dst_rect) {
        dst_rect = &dst_rect_default;
        get_window_rect(window, &dst_rect_default);
    }

    return GST_VAAPI_WINDOW_GET_CLASS(window)->render(window,
                                                      surface,
                                                      src_rect,
                                                      dst_rect,
                                                      flags);
}
