/*
 *  gstvaapisurface.c - VA surface abstraction
 *
 *  Copyright (C) 2010-2011 Splitted-Desktop Systems
 *  Copyright (C) 2011-2013 Intel Corporation
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
 * SECTION:gstvaapisurface
 * @short_description: VA surface abstraction
 */

#include "sysdeps.h"
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapisurface.h"
#include "gstvaapisurface_priv.h"
#include "gstvaapicontext.h"
#include "gstvaapiimage.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiSurface, gst_vaapi_surface, GST_VAAPI_TYPE_OBJECT)

#define GST_VAAPI_SURFACE_GET_PRIVATE(obj)                      \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_SURFACE,	\
                                 GstVaapiSurfacePrivate))

struct _GstVaapiSurfacePrivate {
    guint               width;
    guint               height;
    GstVaapiChromaType  chroma_type;
    GPtrArray          *subpictures;
    GstVaapiContext    *parent_context;
};

enum {
    PROP_0,

    PROP_WIDTH,
    PROP_HEIGHT,
    PROP_CHROMA_TYPE,
    PROP_PARENT_CONTEXT
};

static gboolean
_gst_vaapi_surface_associate_subpicture(
    GstVaapiSurface         *surface,
    GstVaapiSubpicture      *subpicture,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect
);

static gboolean
_gst_vaapi_surface_deassociate_subpicture(
    GstVaapiSurface    *surface,
    GstVaapiSubpicture *subpicture
);

static void
destroy_subpicture_cb(gpointer subpicture, gpointer surface)
{
    _gst_vaapi_surface_deassociate_subpicture(surface, subpicture);
    g_object_unref(subpicture);
}

static void
gst_vaapi_surface_destroy_subpictures(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate * const priv = surface->priv;

    if (priv->subpictures) {
        g_ptr_array_foreach(priv->subpictures, destroy_subpicture_cb, surface);
        g_ptr_array_free(priv->subpictures, TRUE);
        priv->subpictures = NULL;
    }
}

static void
gst_vaapi_surface_destroy(GstVaapiSurface *surface)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(surface);
    VASurfaceID surface_id;
    VAStatus status;

    surface_id = GST_VAAPI_OBJECT_ID(surface);
    GST_DEBUG("surface %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(surface_id));

    gst_vaapi_surface_destroy_subpictures(surface);
    gst_vaapi_surface_set_parent_context(surface, NULL);
  
    if (surface_id != VA_INVALID_SURFACE) {
        GST_VAAPI_DISPLAY_LOCK(display);
        status = vaDestroySurfaces(
            GST_VAAPI_DISPLAY_VADISPLAY(display),
            &surface_id, 1
        );
        GST_VAAPI_DISPLAY_UNLOCK(display);
        if (!vaapi_check_status(status, "vaDestroySurfaces()"))
            g_warning("failed to destroy surface %" GST_VAAPI_ID_FORMAT,
                      GST_VAAPI_ID_ARGS(surface_id));
        GST_VAAPI_OBJECT_ID(surface) = VA_INVALID_SURFACE;
    }
}

static gboolean
gst_vaapi_surface_create(GstVaapiSurface *surface)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(surface);
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

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaCreateSurfaces(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        priv->width,
        priv->height,
        format,
        1, &surface_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaCreateSurfaces()"))
        return FALSE;

    GST_DEBUG("surface %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(surface_id));
    GST_VAAPI_OBJECT_ID(surface) = surface_id;
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
    case PROP_WIDTH:
        priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        priv->height = g_value_get_uint(value);
        break;
    case PROP_CHROMA_TYPE:
        priv->chroma_type = g_value_get_uint(value);
        break;
    case PROP_PARENT_CONTEXT:
        gst_vaapi_surface_set_parent_context(surface, g_value_get_object(value));
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
    case PROP_WIDTH:
        g_value_set_uint(value, gst_vaapi_surface_get_width(surface));
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, gst_vaapi_surface_get_height(surface));
        break;
    case PROP_CHROMA_TYPE:
        g_value_set_uint(value, gst_vaapi_surface_get_chroma_type(surface));
        break;
    case PROP_PARENT_CONTEXT:
        g_value_set_object(value, gst_vaapi_surface_get_parent_context(surface));
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
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "Width",
                           "The width of the surface",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "Height",
                           "The height of the surface",
                           0, G_MAXINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_CHROMA_TYPE,
         g_param_spec_uint("chroma-type",
                           "Chroma type",
                           "The chroma type of the surface",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_PARENT_CONTEXT,
         g_param_spec_object("parent-context",
                             "Parent Context",
                             "The parent context, if any",
                             GST_VAAPI_TYPE_CONTEXT,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_surface_init(GstVaapiSurface *surface)
{
    GstVaapiSurfacePrivate *priv = GST_VAAPI_SURFACE_GET_PRIVATE(surface);

    surface->priv        = priv;
    priv->width          = 0;
    priv->height         = 0;
    priv->chroma_type    = 0;
    priv->subpictures    = NULL;
    priv->parent_context = NULL;
}

/**
 * gst_vaapi_surface_new:
 * @display: a #GstVaapiDisplay
 * @chroma_type: the surface chroma format
 * @width: the requested surface width
 * @height: the requested surface height
 *
 * Creates a new #GstVaapiSurface with the specified chroma format and
 * dimensions.
 *
 * Return value: the newly allocated #GstVaapiSurface object
 */
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
                        "id",           GST_VAAPI_ID(VA_INVALID_ID),
                        "width",        width,
                        "height",       height,
                        "chroma-type",  chroma_type,
                        NULL);
}

/**
 * gst_vaapi_surface_get_id:
 * @surface: a #GstVaapiSurface
 *
 * Returns the underlying VASurfaceID of the @surface.
 *
 * Return value: the underlying VA surface id
 */
GstVaapiID
gst_vaapi_surface_get_id(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), VA_INVALID_SURFACE);

    return GST_VAAPI_OBJECT_ID(surface);
}

/**
 * gst_vaapi_surface_get_chroma_type:
 * @surface: a #GstVaapiSurface
 *
 * Returns the #GstVaapiChromaType the @surface was created with.
 *
 * Return value: the #GstVaapiChromaType
 */
GstVaapiChromaType
gst_vaapi_surface_get_chroma_type(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return surface->priv->chroma_type;
}

/**
 * gst_vaapi_surface_get_width:
 * @surface: a #GstVaapiSurface
 *
 * Returns the @surface width.
 *
 * Return value: the surface width, in pixels
 */
guint
gst_vaapi_surface_get_width(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return surface->priv->width;
}

/**
 * gst_vaapi_surface_get_height:
 * @surface: a #GstVaapiSurface
 *
 * Returns the @surface height.
 *
 * Return value: the surface height, in pixels.
 */
guint
gst_vaapi_surface_get_height(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), 0);

    return surface->priv->height;
}

/**
 * gst_vaapi_surface_get_size:
 * @surface: a #GstVaapiSurface
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiSurface.
 */
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

/**
 * gst_vaapi_surface_set_parent_context:
 * @surface: a #GstVaapiSurface
 * @context: a #GstVaapiContext
 *
 * Sets new parent context, or clears any parent context if @context
 * is %NULL. This function owns an extra reference to the context,
 * which will be released when the surface is destroyed.
 */
void
gst_vaapi_surface_set_parent_context(
    GstVaapiSurface *surface,
    GstVaapiContext *context
)
{
    GstVaapiSurfacePrivate *priv;

    g_return_if_fail(GST_VAAPI_IS_SURFACE(surface));

    priv = surface->priv;

    g_clear_object(&priv->parent_context);

    if (context)
        priv->parent_context = g_object_ref(context);
}

/**
 * gst_vaapi_surface_get_parent_context:
 * @surface: a #GstVaapiSurface
 *
 * Retrieves the parent #GstVaapiContext, or %NULL if there is
 * none. The surface shall still own a reference to the context.
 * i.e. the caller shall not unreference the returned context object.
 *
 * Return value: the parent context, if any.
 */
GstVaapiContext *
gst_vaapi_surface_get_parent_context(GstVaapiSurface *surface)
{
    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    return surface->priv->parent_context;
}

/**
 * gst_vaapi_surface_derive_image:
 * @surface: a #GstVaapiSurface
 *
 * Derives a #GstVaapiImage from the @surface. This image buffer can
 * then be mapped/unmapped for direct CPU access. This operation is
 * only possible if the underlying implementation supports direct
 * rendering capabilities and internal surface formats that can be
 * represented with a #GstVaapiImage.
 *
 * When the operation is not possible, the function returns %NULL and
 * the user should then fallback to using gst_vaapi_surface_get_image()
 * or gst_vaapi_surface_put_image() to accomplish the same task in an
 * indirect manner (additional copy).
 *
 * An image created with gst_vaapi_surface_derive_image() should be
 * unreferenced when it's no longer needed. The image and image buffer
 * data structures will be destroyed. However, the surface contents
 * will remain unchanged until destroyed through the last call to
 * g_object_unref().
 *
 * Return value: the newly allocated #GstVaapiImage object, or %NULL
 *   on failure
 */
GstVaapiImage *
gst_vaapi_surface_derive_image(GstVaapiSurface *surface)
{
    GstVaapiDisplay *display;
    VAImage va_image;
    VAStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), NULL);

    display           = GST_VAAPI_OBJECT_DISPLAY(surface);
    va_image.image_id = VA_INVALID_ID;
    va_image.buf      = VA_INVALID_ID;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaDeriveImage(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        GST_VAAPI_OBJECT_ID(surface),
        &va_image
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaDeriveImage()"))
        return NULL;
    if (va_image.image_id == VA_INVALID_ID || va_image.buf == VA_INVALID_ID)
        return NULL;

    return gst_vaapi_image_new_with_image(display, &va_image);
}

/**
 * gst_vaapi_surface_get_image
 * @surface: a #GstVaapiSurface
 * @image: a #GstVaapiImage
 *
 * Retrieves surface data into a #GstVaapiImage. The @image must have
 * a format supported by the @surface.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_get_image(GstVaapiSurface *surface, GstVaapiImage *image)
{
    GstVaapiDisplay *display;
    VAImageID image_id;
    VAStatus status;
    guint width, height;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);

    display = GST_VAAPI_OBJECT_DISPLAY(surface);
    if (!display)
        return FALSE;

    gst_vaapi_image_get_size(image, &width, &height);
    if (width != surface->priv->width || height != surface->priv->height)
        return FALSE;

    image_id = GST_VAAPI_OBJECT_ID(image);
    if (image_id == VA_INVALID_ID)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaGetImage(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        GST_VAAPI_OBJECT_ID(surface),
        0, 0, width, height,
        image_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaGetImage()"))
        return FALSE;

    return TRUE;
}

/**
 * gst_vaapi_surface_put_image:
 * @surface: a #GstVaapiSurface
 * @image: a #GstVaapiImage
 *
 * Copies data from a #GstVaapiImage into a @surface. The @image must
 * have a format supported by the @surface.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_put_image(GstVaapiSurface *surface, GstVaapiImage *image)
{
    GstVaapiDisplay *display;
    VAImageID image_id;
    VAStatus status;
    guint width, height;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);

    display = GST_VAAPI_OBJECT_DISPLAY(surface);
    if (!display)
        return FALSE;

    gst_vaapi_image_get_size(image, &width, &height);
    if (width != surface->priv->width || height != surface->priv->height)
        return FALSE;

    image_id = GST_VAAPI_OBJECT_ID(image);
    if (image_id == VA_INVALID_ID)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaPutImage(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        GST_VAAPI_OBJECT_ID(surface),
        image_id,
        0, 0, width, height,
        0, 0, width, height
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaPutImage()"))
        return FALSE;

    return TRUE;
}

/**
 * gst_vaapi_surface_associate_subpicture:
 * @surface: a #GstVaapiSurface
 * @subpicture: a #GstVaapiSubpicture
 * @src_rect: the sub-rectangle of the source subpicture
 *   image to extract and process. If %NULL, the entire image will be used.
 * @dst_rect: the sub-rectangle of the destination
 *   surface into which the image is rendered. If %NULL, the entire
 *   surface will be used.
 *
 * Associates the @subpicture with the @surface. The @src_rect
 * coordinates and size are relative to the source image bound to
 * @subpicture. The @dst_rect coordinates and size are relative to the
 * target @surface. Note that the @surface holds an additional
 * reference to the @subpicture.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_associate_subpicture(
    GstVaapiSurface         *surface,
    GstVaapiSubpicture      *subpicture,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect
)
{
    gboolean success;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SUBPICTURE(subpicture), FALSE);

    if (!surface->priv->subpictures) {
        surface->priv->subpictures = g_ptr_array_new();
        if (!surface->priv->subpictures)
            return FALSE;
    }

    if (g_ptr_array_remove_fast(surface->priv->subpictures, subpicture)) {
        success = _gst_vaapi_surface_deassociate_subpicture(surface, subpicture);
        g_object_unref(subpicture);
        if (!success)
            return FALSE;
    }

    success = _gst_vaapi_surface_associate_subpicture(
        surface,
        subpicture,
        src_rect,
        dst_rect
    );
    if (!success)
        return FALSE;

    g_ptr_array_add(surface->priv->subpictures, g_object_ref(subpicture));
    return TRUE;
}

gboolean
_gst_vaapi_surface_associate_subpicture(
    GstVaapiSurface         *surface,
    GstVaapiSubpicture      *subpicture,
    const GstVaapiRectangle *src_rect,
    const GstVaapiRectangle *dst_rect
)
{
    GstVaapiDisplay *display;
    GstVaapiRectangle src_rect_default, dst_rect_default;
    GstVaapiImage *image;
    VASurfaceID surface_id;
    VAStatus status;

    display = GST_VAAPI_OBJECT_DISPLAY(surface);
    if (!display)
        return FALSE;

    surface_id = GST_VAAPI_OBJECT_ID(surface);
    if (surface_id == VA_INVALID_SURFACE)
        return FALSE;

    if (!src_rect) {
        image = gst_vaapi_subpicture_get_image(subpicture);
        if (!image)
            return FALSE;
        src_rect                = &src_rect_default;
        src_rect_default.x      = 0;
        src_rect_default.y      = 0;
        gst_vaapi_image_get_size(
            image,
            &src_rect_default.width,
            &src_rect_default.height
        );
    }

    if (!dst_rect) {
        dst_rect                = &dst_rect_default;
        dst_rect_default.x      = 0;
        dst_rect_default.y      = 0;
        dst_rect_default.width  = surface->priv->width;
        dst_rect_default.height = surface->priv->height;
    }

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaAssociateSubpicture(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        GST_VAAPI_OBJECT_ID(subpicture),
        &surface_id, 1,
        src_rect->x, src_rect->y, src_rect->width, src_rect->height,
        dst_rect->x, dst_rect->y, dst_rect->width, dst_rect->height,
        from_GstVaapiSubpictureFlags(gst_vaapi_subpicture_get_flags(subpicture))
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaAssociateSubpicture()"))
        return FALSE;

    return TRUE;
}

/**
 * gst_vaapi_surface_deassociate_subpicture:
 * @surface: a #GstVaapiSurface
 * @subpicture: a #GstVaapiSubpicture
 *
 * Deassociates @subpicture from @surface. Other associations are kept.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_deassociate_subpicture(
    GstVaapiSurface         *surface,
    GstVaapiSubpicture      *subpicture
)
{
    gboolean success;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);
    g_return_val_if_fail(GST_VAAPI_IS_SUBPICTURE(subpicture), FALSE);

    if (!surface->priv->subpictures)
        return TRUE;

    /* First, check subpicture was really associated with this surface */
    if (!g_ptr_array_remove_fast(surface->priv->subpictures, subpicture)) {
        GST_DEBUG("subpicture %" GST_VAAPI_ID_FORMAT " was not bound to "
                  "surface %" GST_VAAPI_ID_FORMAT,
                  GST_VAAPI_ID_ARGS(GST_VAAPI_OBJECT_ID(subpicture)),
                  GST_VAAPI_ID_ARGS(GST_VAAPI_OBJECT_ID(surface)));
        return TRUE;
    }

    success = _gst_vaapi_surface_deassociate_subpicture(surface, subpicture);
    g_object_unref(subpicture);
    return success;
}

gboolean
_gst_vaapi_surface_deassociate_subpicture(
    GstVaapiSurface         *surface,
    GstVaapiSubpicture      *subpicture
)
{
    GstVaapiDisplay *display;
    VASurfaceID surface_id;
    VAStatus status;

    display = GST_VAAPI_OBJECT_DISPLAY(surface);
    if (!display)
        return FALSE;

    surface_id = GST_VAAPI_OBJECT_ID(surface);
    if (surface_id == VA_INVALID_SURFACE)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaDeassociateSubpicture(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        GST_VAAPI_OBJECT_ID(subpicture),
        &surface_id, 1
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaDeassociateSubpicture()"))
        return FALSE;

    return TRUE;
}

/**
 * gst_vaapi_surface_sync:
 * @surface: a #GstVaapiSurface
 *
 * Blocks until all pending operations on the @surface have been
 * completed.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_sync(GstVaapiSurface *surface)
{
    GstVaapiDisplay *display;
    VAStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);

    display = GST_VAAPI_OBJECT_DISPLAY(surface);
    if (!display)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaSyncSurface(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        GST_VAAPI_OBJECT_ID(surface)
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaSyncSurface()"))
        return FALSE;

    return TRUE;
}

/**
 * gst_vaapi_surface_query_status:
 * @surface: a #GstVaapiSurface
 * @pstatus: return location for the #GstVaapiSurfaceStatus
 *
 * Finds out any pending operations on the @surface. The
 * #GstVaapiSurfaceStatus flags are returned into @pstatus.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_query_status(
    GstVaapiSurface       *surface,
    GstVaapiSurfaceStatus *pstatus
)
{
    VASurfaceStatus surface_status;
    VAStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);

    GST_VAAPI_OBJECT_LOCK_DISPLAY(surface);
    status = vaQuerySurfaceStatus(
        GST_VAAPI_OBJECT_VADISPLAY(surface),
        GST_VAAPI_OBJECT_ID(surface),
        &surface_status
    );
    GST_VAAPI_OBJECT_UNLOCK_DISPLAY(surface);
    if (!vaapi_check_status(status, "vaQuerySurfaceStatus()"))
        return FALSE;

    if (pstatus)
        *pstatus = to_GstVaapiSurfaceStatus(surface_status);
    return TRUE;
}

/**
 * gst_vaapi_surface_set_subpictures_from_composition:
 * @surface: a #GstVaapiSurface
 * @compostion: a #GstVideoOverlayCompositon
 * @propagate_context: a flag specifying whether to apply composition
 *     to the parent context, if any
 *
 * Helper to update the subpictures from #GstVideoOverlayCompositon. Sending
 * a NULL composition will clear all the current subpictures. Note that this
 * method will clear existing subpictures.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_surface_set_subpictures_from_composition(
    GstVaapiSurface            *surface,
    GstVideoOverlayComposition *composition,
    gboolean                    propagate_context
)
{
    GstVaapiDisplay *display;
    guint n, nb_rectangles;

    g_return_val_if_fail(GST_VAAPI_IS_SURFACE(surface), FALSE);

    if (propagate_context) {
        GstVaapiContext * const context = surface->priv->parent_context;
        if (context)
            return gst_vaapi_context_apply_composition(context, composition);
    }

    display = GST_VAAPI_OBJECT_DISPLAY(surface);
    if (!display)
        return FALSE;

    /* Clear current subpictures */
    gst_vaapi_surface_destroy_subpictures(surface);

    if (!composition)
        return TRUE;

    nb_rectangles = gst_video_overlay_composition_n_rectangles (composition);

    /* Overlay all the rectangles cantained in the overlay composition */
    for (n = 0; n < nb_rectangles; ++n) {
        GstVideoOverlayRectangle *rect;
        GstVaapiRectangle sub_rect;
        GstVaapiSubpicture *subpicture;

        rect = gst_video_overlay_composition_get_rectangle (composition, n);
        subpicture = gst_vaapi_subpicture_new_from_overlay_rectangle (display,
                rect);

        gst_video_overlay_rectangle_get_render_rectangle (rect,
                (gint *)&sub_rect.x, (gint *)&sub_rect.y,
                &sub_rect.width, &sub_rect.height);

        if (!gst_vaapi_surface_associate_subpicture (surface, subpicture,
                    NULL, &sub_rect)) {
            GST_WARNING ("could not render overlay rectangle %p", rect);
            g_object_unref (subpicture);
            return FALSE;
        }
        g_object_unref (subpicture);
    }
    return TRUE;
}
