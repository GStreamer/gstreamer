/*
 *  gstvaapiimage.c - VA image abstraction
 *
 *  gstreamer-vaapi (C) 2010-2011 Splitted-Desktop Systems
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
 * SECTION:gstvaapiimage
 * @short_description: VA image abstraction
 */

#include "config.h"
#include <string.h>
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapiimage.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiImage, gst_vaapi_image, GST_VAAPI_TYPE_OBJECT);

#define GST_VAAPI_IMAGE_GET_PRIVATE(obj)                \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                 \
                                 GST_VAAPI_TYPE_IMAGE,	\
                                 GstVaapiImagePrivate))

struct _GstVaapiImagePrivate {
    VAImage             internal_image;
    VAImage             image;
    guchar             *image_data;
    GstVaapiImageFormat internal_format;
    GstVaapiImageFormat format;
    guint               width;
    guint               height;
    guint               create_image    : 1;
    guint               is_constructed  : 1;
    guint               is_linear       : 1;
};

enum {
    PROP_0,

    PROP_IMAGE,
    PROP_FORMAT,
    PROP_WIDTH,
    PROP_HEIGHT
};

#define SWAP_UINT(a, b) do { \
        guint v = a;         \
        a = b;               \
        b = v;               \
    } while (0)

static gboolean
_gst_vaapi_image_map(GstVaapiImage *image);

static gboolean
_gst_vaapi_image_unmap(GstVaapiImage *image);

static gboolean
_gst_vaapi_image_set_image(GstVaapiImage *image, const VAImage *va_image);

/*
 * VAImage wrapper
 */

#define VAAPI_TYPE_IMAGE vaapi_image_get_type()

static gpointer
vaapi_image_copy(gpointer va_image)
{
    return g_slice_dup(VAImage, va_image);
}

static void
vaapi_image_free(gpointer va_image)
{
    if (G_LIKELY(va_image))
        g_slice_free(VAImage, va_image);
}

static GType
vaapi_image_get_type(void)
{
    static GType type = 0;

    if (G_UNLIKELY(type == 0))
        type = g_boxed_type_register_static(
            "VAImage",
            vaapi_image_copy,
            vaapi_image_free
        );
    return type;
}

static gboolean
vaapi_image_is_linear(const VAImage *va_image)
{
    guint i, width, height, width2, height2, data_size;

    for (i = 1; i < va_image->num_planes; i++)
        if (va_image->offsets[i] < va_image->offsets[i - 1])
            return FALSE;

    width   = va_image->width;
    height  = va_image->height;
    width2  = (width  + 1) / 2;
    height2 = (height + 1) / 2;

    switch (va_image->format.fourcc) {
    case VA_FOURCC('N','V','1','2'):
    case VA_FOURCC('Y','V','1','2'):
    case VA_FOURCC('I','4','2','0'):
        data_size = width * height + 2 * width2 * height2;
        break;
    case VA_FOURCC('A','Y','U','V'):
    case VA_FOURCC('A','R','G','B'):
    case VA_FOURCC('R','G','B','A'):
    case VA_FOURCC('A','B','G','R'):
    case VA_FOURCC('B','G','R','A'):
        data_size = 4 * width * height;
        break;
    default:
        g_error("FIXME: incomplete formats");
        break;
    }
    return va_image->data_size == data_size;
}

static void
gst_vaapi_image_destroy(GstVaapiImage *image)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(image);
    VAImageID image_id;
    VAStatus status;

    _gst_vaapi_image_unmap(image);

    image_id = GST_VAAPI_OBJECT_ID(image);
    GST_DEBUG("image %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(image_id));

    if (image_id != VA_INVALID_ID) {
        GST_VAAPI_DISPLAY_LOCK(display);
        status = vaDestroyImage(GST_VAAPI_DISPLAY_VADISPLAY(display), image_id);
        GST_VAAPI_DISPLAY_UNLOCK(display);
        if (!vaapi_check_status(status, "vaDestroyImage()"))
            g_warning("failed to destroy image %" GST_VAAPI_ID_FORMAT,
                      GST_VAAPI_ID_ARGS(image_id));
        GST_VAAPI_OBJECT_ID(image) = VA_INVALID_ID;
    }
}

static gboolean
_gst_vaapi_image_create(GstVaapiImage *image, GstVaapiImageFormat format)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(image);
    GstVaapiImagePrivate * const priv = image->priv;
    const VAImageFormat *va_format;
    VAStatus status;

    if (!gst_vaapi_display_has_image_format(display, format))
        return FALSE;

    va_format = gst_vaapi_image_format_get_va_format(format);
    if (!va_format)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaCreateImage(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        (VAImageFormat *)va_format,
        priv->width,
        priv->height,
        &priv->internal_image
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (status != VA_STATUS_SUCCESS ||
        priv->internal_image.format.fourcc != va_format->fourcc)
        return FALSE;

    priv->internal_format = format;
    return TRUE;
}

static gboolean
gst_vaapi_image_create(GstVaapiImage *image)
{
    GstVaapiImagePrivate * const priv = image->priv;
    GstVaapiImageFormat format = priv->format;
    const VAImageFormat *va_format;
    VAImageID image_id;

    if (!priv->create_image)
        return (priv->image.image_id != VA_INVALID_ID &&
                priv->image.buf      != VA_INVALID_ID);

    if (!_gst_vaapi_image_create(image, format)) {
        switch (format) {
        case GST_VAAPI_IMAGE_I420:
            format = GST_VAAPI_IMAGE_YV12;
            break;
        case GST_VAAPI_IMAGE_YV12:
            format = GST_VAAPI_IMAGE_I420;
            break;
        default:
            format = 0;
            break;
        }
        if (!format || !_gst_vaapi_image_create(image, format))
            return FALSE;
    }
    priv->image = priv->internal_image;
    image_id    = priv->image.image_id;

    if (priv->format != priv->internal_format) {
        switch (priv->format) {
        case GST_VAAPI_IMAGE_YV12:
        case GST_VAAPI_IMAGE_I420:
            va_format = gst_vaapi_image_format_get_va_format(priv->format);
            if (!va_format)
                return FALSE;
            priv->image.format = *va_format;
            SWAP_UINT(priv->image.offsets[1], priv->image.offsets[2]);
            SWAP_UINT(priv->image.pitches[1], priv->image.pitches[2]);
            break;
        default:
            break;
        }
    }
    priv->is_linear = vaapi_image_is_linear(&priv->image);

    GST_DEBUG("image %" GST_VAAPI_ID_FORMAT, GST_VAAPI_ID_ARGS(image_id));
    GST_VAAPI_OBJECT_ID(image) = image_id;
    return TRUE;
}

static void
gst_vaapi_image_finalize(GObject *object)
{
    gst_vaapi_image_destroy(GST_VAAPI_IMAGE(object));

    G_OBJECT_CLASS(gst_vaapi_image_parent_class)->finalize(object);
}

static void
gst_vaapi_image_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiImage        * const image = GST_VAAPI_IMAGE(object);
    GstVaapiImagePrivate * const priv  = image->priv;

    switch (prop_id) {
    case PROP_IMAGE: {
        const VAImage * const va_image = g_value_get_boxed(value);
        if (va_image)
            _gst_vaapi_image_set_image(image, va_image);
        break;
    }
    case PROP_FORMAT:
        if (priv->create_image)
            priv->format = g_value_get_uint(value);
        break;
    case PROP_WIDTH:
        if (priv->create_image)
            priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
        if (priv->create_image)
            priv->height = g_value_get_uint(value);
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_image_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiImage * const image = GST_VAAPI_IMAGE(object);

    switch (prop_id) {
    case PROP_IMAGE:
        g_value_set_boxed(value, &image->priv->image);
        break;
    case PROP_FORMAT:
        g_value_set_uint(value, gst_vaapi_image_get_format(image));
        break;
    case PROP_WIDTH:
        g_value_set_uint(value, gst_vaapi_image_get_width(image));
        break;
    case PROP_HEIGHT:
        g_value_set_uint(value, gst_vaapi_image_get_height(image));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_image_constructed(GObject *object)
{
    GstVaapiImage * const image = GST_VAAPI_IMAGE(object);
    GObjectClass *parent_class;

    image->priv->is_constructed = gst_vaapi_image_create(image);

    parent_class = G_OBJECT_CLASS(gst_vaapi_image_parent_class);
    if (parent_class->constructed)
        parent_class->constructed(object);
}

static void
gst_vaapi_image_class_init(GstVaapiImageClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiImagePrivate));

    object_class->finalize     = gst_vaapi_image_finalize;
    object_class->set_property = gst_vaapi_image_set_property;
    object_class->get_property = gst_vaapi_image_get_property;
    object_class->constructed  = gst_vaapi_image_constructed;

    g_object_class_install_property
        (object_class,
         PROP_IMAGE,
         g_param_spec_boxed("image",
                            "Image",
                            "The underlying VA image",
                            VAAPI_TYPE_IMAGE,
                            G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "width",
                           "The image width",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "heighr",
                           "The image height",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    /**
     * GstVaapiImage:format:
     *
     * The #GstVaapiImageFormat of the image
     */
    g_object_class_install_property
        (object_class,
         PROP_FORMAT,
         g_param_spec_uint("format",
                           "Format",
                           "The underlying image format",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_image_init(GstVaapiImage *image)
{
    GstVaapiImagePrivate *priv = GST_VAAPI_IMAGE_GET_PRIVATE(image);

    image->priv                   = priv;
    priv->image_data              = NULL;
    priv->width                   = 0;
    priv->height                  = 0;
    priv->internal_format         = 0;
    priv->format                  = 0;
    priv->create_image            = TRUE;
    priv->is_constructed          = FALSE;
    priv->is_linear               = FALSE;

    memset(&priv->internal_image, 0, sizeof(priv->internal_image));
    priv->internal_image.image_id = VA_INVALID_ID;
    priv->internal_image.buf      = VA_INVALID_ID;

    memset(&priv->image, 0, sizeof(priv->image));
    priv->image.image_id          = VA_INVALID_ID;
    priv->image.buf               = VA_INVALID_ID;
}

/**
 * gst_vaapi_image_new:
 * @display: a #GstVaapiDisplay
 * @format: a #GstVaapiImageFormat
 * @width: the requested image width
 * @height: the requested image height
 *
 * Creates a new #GstVaapiImage with the specified format and
 * dimensions.
 *
 * Return value: the newly allocated #GstVaapiImage object
 */
GstVaapiImage *
gst_vaapi_image_new(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format,
    guint               width,
    guint               height
)
{
    GstVaapiImage *image;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(width > 0, NULL);
    g_return_val_if_fail(height > 0, NULL);

    GST_DEBUG("format %" GST_FOURCC_FORMAT ", size %ux%u",
              GST_FOURCC_ARGS(format), width, height);

    image = g_object_new(
        GST_VAAPI_TYPE_IMAGE,
        "display", display,
        "id",      GST_VAAPI_ID(VA_INVALID_ID),
        "format",  format,
        "width",   width,
        "height",  height,
        NULL
    );
    if (!image)
        return NULL;

    if (!image->priv->is_constructed) {
        g_object_unref(image);
        return NULL;
    }
    return image;
}

/**
 * gst_vaapi_image_new_with_image:
 * @display: a #GstVaapiDisplay
 * @va_image: a VA image
 *
 * Creates a new #GstVaapiImage from a foreign VA image. The image
 * format and dimensions will be extracted from @va_image. This
 * function is mainly used by gst_vaapi_surface_derive_image() to bind
 * a VA image to a #GstVaapiImage object.
 *
 * Return value: the newly allocated #GstVaapiImage object
 */
GstVaapiImage *
gst_vaapi_image_new_with_image(GstVaapiDisplay *display, VAImage *va_image)
{
    GstVaapiImage *image;

    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(va_image, NULL);
    g_return_val_if_fail(va_image->image_id != VA_INVALID_ID, NULL);
    g_return_val_if_fail(va_image->buf != VA_INVALID_ID, NULL);

    GST_DEBUG("VA image 0x%08x, format %" GST_FOURCC_FORMAT ", size %ux%u",
              va_image->image_id,
              GST_FOURCC_ARGS(va_image->format.fourcc),
              va_image->width, va_image->height);

    image = g_object_new(
        GST_VAAPI_TYPE_IMAGE,
        "display", display,
        "id",      GST_VAAPI_ID(va_image->image_id),
        "image",   va_image,
        NULL
    );
    if (!image)
        return NULL;

    if (!image->priv->is_constructed) {
        g_object_unref(image);
        return NULL;
    }
    return image;
}

/**
 * gst_vaapi_image_get_id:
 * @image: a #GstVaapiImage
 *
 * Returns the underlying VAImageID of the @image.
 *
 * Return value: the underlying VA image id
 */
GstVaapiID
gst_vaapi_image_get_id(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), VA_INVALID_ID);
    g_return_val_if_fail(image->priv->is_constructed, VA_INVALID_ID);

    return GST_VAAPI_OBJECT_ID(image);
}

/**
 * gst_vaapi_image_get_image:
 * @image: a #GstVaapiImage
 * @va_image: a VA image
 *
 * Fills @va_image with the VA image used internally.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_get_image(GstVaapiImage *image, VAImage *va_image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    if (va_image)
        *va_image = image->priv->image;

    return TRUE;
}

/*
 * _gst_vaapi_image_set_image:
 * @image: a #GstVaapiImage
 * @va_image: a VA image
 *
 * Initializes #GstVaapiImage with a foreign VA image. This function
 * will try to "linearize" the VA image. i.e. making sure that the VA
 * image offsets into the data buffer are in increasing order with the
 * number of planes available in the image.
 *
 * This is an internal function used by gst_vaapi_image_new_with_image().
 *
 * Return value: %TRUE on success
 */
gboolean
_gst_vaapi_image_set_image(GstVaapiImage *image, const VAImage *va_image)
{
    GstVaapiImagePrivate * const priv = image->priv;
    GstVaapiImageFormat format;
    VAImage alt_va_image;
    const VAImageFormat *alt_va_format;

    if (!va_image)
        return FALSE;

    format = gst_vaapi_image_format(&va_image->format);
    if (!format)
        return FALSE;

    priv->create_image    = FALSE;
    priv->internal_image  = *va_image;
    priv->internal_format = format;
    priv->is_linear       = vaapi_image_is_linear(va_image);
    priv->image           = *va_image;
    priv->format          = format;
    priv->width           = va_image->width;
    priv->height          = va_image->height;

    /* Try to linearize image */
    if (!priv->is_linear) {
        switch (format) {
        case GST_VAAPI_IMAGE_I420:
            format = GST_VAAPI_IMAGE_YV12;
            break;
        case GST_VAAPI_IMAGE_YV12:
            format = GST_VAAPI_IMAGE_I420;
            break;
        default:
            format = 0;
            break;
        }
        if (format &&
            (alt_va_format = gst_vaapi_image_format_get_va_format(format))) {
            alt_va_image = *va_image;
            alt_va_image.format = *alt_va_format;
            SWAP_UINT(alt_va_image.offsets[1], alt_va_image.offsets[2]);
            SWAP_UINT(alt_va_image.pitches[1], alt_va_image.pitches[2]);
            if (vaapi_image_is_linear(&alt_va_image)) {
                priv->image     = alt_va_image;
                priv->format    = format;
                priv->is_linear = TRUE;
                GST_DEBUG("linearized image to %" GST_FOURCC_FORMAT " format",
                          GST_FOURCC_ARGS(format));
            }
        }
    }
    return TRUE;
}

/**
 * gst_vaapi_image_get_format:
 * @image: a #GstVaapiImage
 *
 * Returns the #GstVaapiImageFormat the @image was created with.
 *
 * Return value: the #GstVaapiImageFormat
 */
GstVaapiImageFormat
gst_vaapi_image_get_format(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, 0);

    return image->priv->format;
}

/**
 * gst_vaapi_image_get_width:
 * @image: a #GstVaapiImage
 *
 * Returns the @image width.
 *
 * Return value: the image width, in pixels
 */
guint
gst_vaapi_image_get_width(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, 0);

    return image->priv->width;
}

/**
 * gst_vaapi_image_get_height:
 * @image: a #GstVaapiImage
 *
 * Returns the @image height.
 *
 * Return value: the image height, in pixels.
 */
guint
gst_vaapi_image_get_height(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, 0);

    return image->priv->height;
}

/**
 * gst_vaapi_image_get_size:
 * @image: a #GstVaapiImage
 * @pwidth: return location for the width, or %NULL
 * @pheight: return location for the height, or %NULL
 *
 * Retrieves the dimensions of a #GstVaapiImage.
 */
void
gst_vaapi_image_get_size(GstVaapiImage *image, guint *pwidth, guint *pheight)
{
    g_return_if_fail(GST_VAAPI_IS_IMAGE(image));
    g_return_if_fail(image->priv->is_constructed);

    if (pwidth)
        *pwidth = image->priv->width;

    if (pheight)
        *pheight = image->priv->height;
}

/**
 * gst_vaapi_image_is_linear:
 * @image: a #GstVaapiImage
 *
 * Checks whether the @image has data planes allocated from a single
 * buffer and offsets into that buffer are in increasing order with
 * the number of planes.
 *
 * Return value: %TRUE if image data planes are allocated from a single buffer
 */
gboolean
gst_vaapi_image_is_linear(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    return image->priv->is_linear;
}

/**
 * gst_vaapi_image_is_mapped:
 * @image: a #GstVaapiImage
 *
 * Checks whether the @image is currently mapped or not.
 *
 * Return value: %TRUE if the @image is mapped
 */
static inline gboolean
_gst_vaapi_image_is_mapped(GstVaapiImage *image)
{
    return image->priv->image_data != NULL;
}

gboolean
gst_vaapi_image_is_mapped(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    return _gst_vaapi_image_is_mapped(image);
}

/**
 * gst_vaapi_image_map:
 * @image: a #GstVaapiImage
 *
 * Maps the image data buffer. The actual pixels are returned by the
 * gst_vaapi_image_get_plane() function.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_map(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    return _gst_vaapi_image_map(image);
}

gboolean
_gst_vaapi_image_map(GstVaapiImage *image)
{
    GstVaapiDisplay *display;
    void *image_data;
    VAStatus status;

    if (_gst_vaapi_image_is_mapped(image))
        return TRUE;

    display = GST_VAAPI_OBJECT_DISPLAY(image);
    if (!display)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaMapBuffer(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        image->priv->image.buf,
        &image_data
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaMapBuffer()"))
        return FALSE;

    image->priv->image_data = image_data;
    return TRUE;
}

/**
 * gst_vaapi_image_unmap:
 * @image: a #GstVaapiImage
 *
 * Unmaps the image data buffer. Pointers to pixels returned by
 * gst_vaapi_image_get_plane() are then no longer valid.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_unmap(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    return _gst_vaapi_image_unmap(image);
}

gboolean
_gst_vaapi_image_unmap(GstVaapiImage *image)
{
    GstVaapiDisplay *display;
    VAStatus status;

    if (!_gst_vaapi_image_is_mapped(image))
        return FALSE;

    display = GST_VAAPI_OBJECT_DISPLAY(image);
    if (!display)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaUnmapBuffer(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        image->priv->image.buf
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaUnmapBuffer()"))
        return FALSE;

    image->priv->image_data = NULL;
    return TRUE;
}

/**
 * gst_vaapi_image_get_plane_count:
 * @image: a #GstVaapiImage
 *
 * Retrieves the number of planes available in the @image. The @image
 * must be mapped for this function to work properly.
 *
 * Return value: the number of planes available in the @image
 */
guint
gst_vaapi_image_get_plane_count(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);
    g_return_val_if_fail(_gst_vaapi_image_is_mapped(image), 0);

    return image->priv->image.num_planes;
}

/**
 * gst_vaapi_image_get_plane:
 * @image: a #GstVaapiImage
 * @plane: the requested plane number
 *
 * Retrieves the pixels data to the specified @plane. The @image must
 * be mapped for this function to work properly.
 *
 * Return value: the pixels data of the specified @plane
 */
guchar *
gst_vaapi_image_get_plane(GstVaapiImage *image, guint plane)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), NULL);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);
    g_return_val_if_fail(_gst_vaapi_image_is_mapped(image), NULL);
    g_return_val_if_fail(plane < image->priv->image.num_planes, NULL);

    return image->priv->image_data + image->priv->image.offsets[plane];
}

/**
 * gst_vaapi_image_get_pitch:
 * @image: a #GstVaapiImage
 * @plane: the requested plane number
 *
 * Retrieves the line size (stride) of the specified @plane. The
 * @image must be mapped for this function to work properly.
 *
 * Return value: the line size (stride) of the specified plane
 */
guint
gst_vaapi_image_get_pitch(GstVaapiImage *image, guint plane)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);
    g_return_val_if_fail(_gst_vaapi_image_is_mapped(image), 0);
    g_return_val_if_fail(plane < image->priv->image.num_planes, 0);

    return image->priv->image.pitches[plane];
}

/**
 * gst_vaapi_image_get_data_size:
 * @image: a #GstVaapiImage
 *
 * Retrieves the underlying image data size. This function could be
 * used to determine whether the image has a compatible layout with
 * another image structure.
 *
 * Return value: the whole image data size of the @image
 */
guint
gst_vaapi_image_get_data_size(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    return image->priv->image.data_size;
}

/**
 * gst_vaapi_image_update_from_buffer:
 * @image: a #GstVaapiImage
 * @buffer: a #GstBuffer
 *
 * Transfers pixels data contained in the #GstBuffer into the
 * @image. Both image structures shall have the same format.
 *
 * Return value: %TRUE on success
 */
gboolean
gst_vaapi_image_update_from_buffer(GstVaapiImage *image, GstBuffer *buffer)
{
    GstVaapiImagePrivate *priv;
    GstStructure *structure;
    GstCaps *caps;
    GstVaapiImageFormat format;
    gint width, height;
    guint offsets[3], pitches[3], widths[3], heights[3];
    guint i, j;
    guchar *data;
    guint32 data_size;

    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);
    g_return_val_if_fail(GST_IS_BUFFER(buffer), FALSE);

    priv      = image->priv;
    data      = GST_BUFFER_DATA(buffer);
    data_size = GST_BUFFER_SIZE(buffer);
    caps      = GST_BUFFER_CAPS(buffer);

    if (!caps)
        return FALSE;

    format = gst_vaapi_image_format_from_caps(caps);
    if (format != priv->format)
        return FALSE;

    structure = gst_caps_get_structure(caps, 0);
    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);
    if (width != priv->width || height != priv->height)
        return FALSE;

    if (!gst_vaapi_image_map(image))
        return FALSE;

    if (priv->is_linear && data_size == priv->image.data_size)
        memcpy(priv->image_data, data, data_size);
    else {
        /* XXX: copied from gst_video_format_get_row_stride() -- no NV12? */
        const guint width2  = (width  + 1) / 2;
        const guint height2 = (height + 1) / 2;
        guint size2;
        switch (format) {
        case GST_VAAPI_IMAGE_NV12:
            offsets[0] = 0;
            pitches[0] = GST_ROUND_UP_4(width);
            widths [0] = width;
            heights[0] = height;
            offsets[1] = offsets[0] + height * pitches[0];
            pitches[1] = pitches[0];
            widths [1] = width2 * 2;
            heights[1] = height2;
            size2      = offsets[1] + height2 * pitches[1];
            break;
        case GST_VAAPI_IMAGE_YV12:
        case GST_VAAPI_IMAGE_I420:
            offsets[0] = 0;
            pitches[0] = GST_ROUND_UP_4(width);
            widths [0] = width;
            heights[0] = height;
            offsets[1] = offsets[0] + height * pitches[0];
            pitches[1] = GST_ROUND_UP_4(GST_ROUND_UP_2(width) / 2);
            widths [1] = width2;
            heights[1] = height2;
            offsets[2] = offsets[1] + height2 * pitches[1];
            pitches[2] = pitches[1];
            widths [2] = width2;
            heights[2] = height2;
            size2      = offsets[2] + height2 * pitches[2];
            break;
        case GST_VAAPI_IMAGE_ARGB:
        case GST_VAAPI_IMAGE_RGBA:
        case GST_VAAPI_IMAGE_ABGR:
        case GST_VAAPI_IMAGE_BGRA:
            offsets[0] = 0;
            pitches[0] = width * 4;
            widths [0] = width * 4;
            heights[0] = height;
            size2      = offsets[0] + height * pitches[0];
            break;
        default:
            g_error("could not compute row-stride for %" GST_FOURCC_FORMAT,
                    GST_FOURCC_ARGS(format));
            break;
        }
        if (size2 != data_size)
            g_error("data_size mismatch %d / %u", size2, data_size);
        for (i = 0; i < priv->image.num_planes; i++) {
            guchar *src = data + offsets[i];
            guchar *dst = priv->image_data + priv->image.offsets[i];
            for (j = 0; j < heights[i]; j++) {
                memcpy(dst, src, widths[i]);
                src += pitches[i];
                dst += priv->image.pitches[i];
            }
        }
    }

    if (!gst_vaapi_image_unmap(image))
        return FALSE;

    return TRUE;
}
