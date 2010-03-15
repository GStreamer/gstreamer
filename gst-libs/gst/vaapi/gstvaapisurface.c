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
    GstVaapiChromaType  chroma_type;
};

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_SURFACE_ID,
    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_CHROMA_TYPE
};

static void
gst_vaapi_surface_destroy(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate * const priv = surface->priv;
    VADisplay dpy = GST_VAAPI_DISPLAY_VADISPLAY(priv->display);
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
    guint format;

    switch (priv->chroma_type) {
    case GST_VAAPI_CHROMA_TYPE_YUV420:
        format = VA_RT_FORMAT_YUV420;
        break;
    case GST_VAAPI_CHROMA_TYPE_YUV422:
        format = VA_RT_FORMAT_YUV422;
        break;
    case GST_VAAPI_CHROMA_TYPE_YUV444:
        format = VA_RT_FORMAT_YUV444;
        break;
    default:
        GST_DEBUG("unsupported chroma-type %u\n", priv->chroma_type);
        return FALSE;
    }

    status = vaCreateSurfaces(
        GST_VAAPI_DISPLAY_VADISPLAY(priv->display),
        priv->width,
        priv->height,
        format,
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
gst_vaapi_surface_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
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
    case PROP_CHROMA_TYPE:
        priv->chroma_type = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiSurface * const surface = GST_VAAPI_SURFACE(object);

    switch (prop_id) {
    case PROP_DISPLAY:
        g_value_set_object(value, gst_vaapi_surface_get_display(surface));
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
    case PROP_CHROMA_TYPE:
        g_value_set_uint(value, gst_vaapi_surface_get_chroma_type(surface));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_surface_constructed(GObject *object)
{
    GstVaapiSurface * const surface = GST_VAAPI_SURFACE(object);
    GObjectClass *parent_class;

    gst_vaapi_surface_create(surface);

    parent_class = G_OBJECT_CLASS(gst_vaapi_surface_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
gst_vaapi_surface_class_init(GstVaapiSurfaceClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiSurfacePrivate));

    object_class->finalize     = gst_vaapi_surface_finalize;
    object_class->set_property = gst_vaapi_surface_set_property;
    object_class->get_property = gst_vaapi_surface_get_property;
    object_class->constructed  = gst_vaapi_surface_constructed;

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
         PROP_CHROMA_TYPE,
         g_param_spec_uint("chroma-type",
                           "chroma-type",
                           "VA surface chroma type",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_surface_init(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate *priv = GST_VAAPI_SURFACE_GET_PRIVATE(surface);

    surface->priv       = priv;
    priv->display       = NULL;
    priv->surface_id    = VA_INVALID_SURFACE;
    priv->width         = 0;
    priv->height        = 0;
    priv->chroma_type        = 0;
}

GstVaapiSurface *
gst_vaapi_surface_new(
    GstVaapiDisplay    *display,
    GstVaapiChromaType  chroma_type,
    guint               width,
    guint               height
)
{
    GST_DEBUG("size %ux%u, chroma type 0x%x", width, height, chroma_type);

    return g_object_new(GST_VAAPI_TYPE_SURFACE,
                        "display",      display,
                        "width",        width,
                        "height",       height,
                        "chroma-type",  chroma_type,
                        NULL);
}

VASurfaceID
gst_vaapi_surface_get_id(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), VA_INVALID_SURFACE);

    return surface->priv->surface_id;
}

GstVaapiDisplay *
gst_vaapi_surface_get_display(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    return surface->priv->display;
}

GstVaapiChromaType
gst_vaapi_surface_get_chroma_type(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return surface->priv->chroma_type;
}

guint
gst_vaapi_surface_get_width(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return surface->priv->width;
}

guint
gst_vaapi_surface_get_height(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return surface->priv->height;
}

void
gst_vaapi_surface_get_size(
    GstVaapiSurface *surface,
    guint           *pwidth,
    guint           *pheight
)
{
    g_return_if_fail(GST_VAAPI_IS_SURFACE(surface));

    if (pwidth)
        *pwidth = gst_vaapi_surface_get_width(surface);

    if (pheight)
        *pheight = gst_vaapi_surface_get_height(surface);
}

gboolean
gst_vaapi_surface_get_image(GstVaapiSurface *surface, GstVaapiImage *image)
{
    VAImageID image_id;
    VAStatus status;
    guint width, height;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);

    gst_vaapi_image_get_size(image, &width, &height);
    if (width != surface->priv->width || height != surface->priv->height)
        return FALSE;

    image_id = gst_vaapi_image_get_id(image);
    if (image_id == VA_INVALID_ID)
        return FALSE;

    status = vaGetImage(
        GST_VAAPI_DISPLAY_VADISPLAY(surface->priv->display),
        surface->priv->surface_id,
        0, 0, width, height,
        image_id
    );
    if (!vaapi_check_status(status, "vaGetImage()"))
        return FALSE;

    return TRUE;
}

gboolean
gst_vaapi_surface_put_image(GstVaapiSurface *surface, GstVaapiImage *image)
{
    VAImageID image_id;
    VAStatus status;
    guint width, height;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);

    gst_vaapi_image_get_size(image, &width, &height);
    if (width != surface->priv->width || height != surface->priv->height)
        return FALSE;

    image_id = gst_vaapi_image_get_id(image);
    if (image_id == VA_INVALID_ID)
        return FALSE;

    status = vaPutImage(
        GST_VAAPI_DISPLAY_VADISPLAY(surface->priv->display),
        surface->priv->surface_id,
        image_id,
        0, 0, width, height,
        0, 0, width, height
    );
    if (!vaapi_check_status(status, "vaPutImage()"))
        return FALSE;

    return TRUE;
}
