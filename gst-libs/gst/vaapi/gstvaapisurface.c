/*
 *  gstvaapisurface.c - VA surface abstraction
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
#include "vaapi_utils.h"
#include "gstvaapisurface.h"
#include <va/va_backend.h>

#define DEBUG 1
#include "vaapi_debug.h"

G_DEFINE_TYPE(GstVaapiSurface, gst_vaapi_surface, G_TYPE_OBJECT);

#define GST_VAAPI_SURFACE_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_SURFACE,	\
                                 GstVaapiSurfacePrivate))

struct _GstVaapiSurfacePrivate {
    GstVaapiDisplay    *display;
    VASurfaceID         surface_id;
    guint               width;
    guint               height;
    guint               format;
};

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_SURFACE_ID,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_FORMAT
};

static void
gst_vaapi_surface_destroy(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate * const priv = surface->priv;
    VADisplay dpy = gst_vaapi_display_get_display(priv->display);
    VAStatus status;

    if (priv->surface_id != VA_INVALID_SURFACE) {
        status = vaDestroySurfaces(dpy, &priv->surface_id, 1);
        if (!vaapi_check_status(status, "vaDestroySurfaces()"))
            g_warning("failed to destroy surface 0x%08x\n", priv->surface_id);
        priv->surface_id = VA_INVALID_SURFACE;
    }

    if (priv->display) {
        g_object_unref(priv->display);
        priv->display = NULL;
    }
}

static gboolean
gst_vaapi_surface_create(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate * const priv = surface->priv;
    VASurfaceID surface_id;
    VAStatus status;

    status = vaCreateSurfaces(
        gst_vaapi_display_get_display(priv->display),
        priv->width,
        priv->height,
        priv->format,
        1, &surface_id
    );
    if (!vaapi_check_status(status, "vaCreateSurfaces()"))
        return FALSE;

    priv->surface_id = surface_id;
    return TRUE;
}

static void
gst_vaapi_surface_finalize(GObject *object)
{
    gst_vaapi_surface_destroy(GST_VAAPI_SURFACE(object));

    G_OBJECT_CLASS(gst_vaapi_surface_parent_class)->finalize(object);
}

static void
gst_vaapi_surface_set_property(GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
    GstVaapiSurface        * const surface = GST_VAAPI_SURFACE(object);
    GstVaapiSurfacePrivate * const priv    = surface->priv;

    switch (prop_id) {
    case PROP_DISPLAY:
        priv->display = g_object_ref(g_value_get_object(value));
        break;
    case PROP_WIDTH:
        priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_uint(value);
        break;
    case PROP_FORMAT:
        priv->format = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_get_property(GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
    GstVaapiSurface        * const surface = GST_VAAPI_SURFACE(object);
    GstVaapiSurfacePrivate * const priv    = surface->priv;

    switch (prop_id) {
    case PROP_DISPLAY:
        /* gst_vaapi_surface_get_display() already refs the object */
        g_value_take_object(value, gst_vaapi_surface_get_display(surface));
        break;
    case PROP_SURFACE_ID:
        g_value_set_uint(value, gst_vaapi_surface_get_id(surface));
        break;
    case PROP_WIDTH:
        g_value_set_uint(value, gst_vaapi_surface_get_width(surface));
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, gst_vaapi_surface_get_height(surface));
        break;
    case PROP_FORMAT:
        g_value_set_uint(value, gst_vaapi_surface_get_format(surface));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static GObject *
gst_vaapi_surface_constructor(GType                  type,
                              guint                  n_params,
                              GObjectConstructParam *params)
{
    GstVaapiSurface *surface;
    GObjectClass *parent_class;
    GObject *object;

    D(bug("gst_vaapi_surface_constructor()\n"));

    parent_class = G_OBJECT_CLASS(gst_vaapi_surface_parent_class);
    object = parent_class->constructor (type, n_params, params);

    if (object) {
        surface = GST_VAAPI_SURFACE(object);
        if (!gst_vaapi_surface_create(surface)) {
            gst_vaapi_surface_destroy(surface);
            object = NULL;
        }
    }
    return object;
}

static void
gst_vaapi_surface_class_init(GstVaapiSurfaceClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiSurfacePrivate));

    object_class->finalize     = gst_vaapi_surface_finalize;
    object_class->set_property = gst_vaapi_surface_set_property;
    object_class->get_property = gst_vaapi_surface_get_property;
    object_class->constructor  = gst_vaapi_surface_constructor;

    g_object_class_install_property
        (object_class,
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "display",
                             "GStreamer Va display",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_SURFACE_ID,
         g_param_spec_uint("id",
                           "VA surface id",
                           "VA surface id",
                           0, G_MAXUINT32, VA_INVALID_SURFACE,
                           G_PARAM_READABLE));

    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "width",
                           "VA surface width",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "height",
                           "VA surface height",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_FORMAT,
         g_param_spec_uint("format",
                           "format",
                           "VA surface format",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_surface_init(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate *priv = GST_VAAPI_SURFACE_GET_PRIVATE(surface);

    D(bug("gst_vaapi_surface_init()\n"));

    surface->priv       = priv;
    priv->display       = NULL;
    priv->surface_id    = VA_INVALID_SURFACE;
    priv->width         = 0;
    priv->height        = 0;
    priv->format        = 0;
}

GstVaapiSurface *
gst_vaapi_surface_new(GstVaapiDisplay *display,
                      guint            width,
                      guint            height,
                      guint            format)
{
    D(bug("gst_vaapi_surface_new(): size %ux%u, format 0x%x\n",
          width, height, format));

    return g_object_new(GST_VAAPI_TYPE_SURFACE,
                        "display", display,
                        "width", width,
                        "height", height,
                        "format", format,
                        NULL);
}

VASurfaceID
gst_vaapi_surface_get_id(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate * const priv = surface->priv;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), VA_INVALID_SURFACE);

    return priv->surface_id;
}

GstVaapiDisplay *
gst_vaapi_surface_get_display(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    return g_object_ref(surface->priv->display);
}

guint
gst_vaapi_surface_get_width(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate * const priv = surface->priv;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return priv->width;
}

guint
gst_vaapi_surface_get_height(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate * const priv = surface->priv;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return priv->height;
}

guint
gst_vaapi_surface_get_format(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate *priv = surface->priv;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return priv->format;
}
