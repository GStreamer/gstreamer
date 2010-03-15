/*
 *  gstvaapiimage.c - VA image abstraction
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
#include <string.h>
#include "vaapi_utils.h"
#include "gstvaapiimage.h"
#include <va/va_backend.h>

#define DEBUG 1
#include "vaapi_debug.h"

G_DEFINE_TYPE(GstVaapiImage, gst_vaapi_image, G_TYPE_OBJECT);

#define GST_VAAPI_IMAGE_GET_PRIVATE(obj)                \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                 \
                                 GST_VAAPI_TYPE_IMAGE,	\
                                 GstVaapiImagePrivate))

struct _GstVaapiImagePrivate {
    GstVaapiDisplay    *display;
    gboolean            is_constructed;
    VAImage             image;
    guchar             *image_data;
    GstVaapiImageFormat format;
    guint               width;
    guint               height;
};

enum {
    PROP_0,

    PROP_DISPLAY,
    PROP_IMAGE_ID,
    PROP_FORMAT,
    PROP_WIDTH,
    PROP_HEIGHT
};

static void
gst_vaapi_image_destroy(GstVaapiImage *image)
{
    GstVaapiImagePrivate * const priv = image->priv;
    VADisplay dpy = gst_vaapi_display_get_display(priv->display);
    VAStatus status;

    gst_vaapi_image_unmap(image);

    if (priv->image.image_id != VA_INVALID_ID) {
        status = vaDestroyImage(dpy, priv->image.image_id);
        if (!vaapi_check_status(status, "vaDestroyImage()"))
            g_warning("failed to destroy image 0x%08x\n", priv->image.image_id);
        priv->image.image_id = VA_INVALID_ID;
    }

    if (priv->display) {
        g_object_unref(priv->display);
        priv->display = NULL;
    }
}

static gboolean
gst_vaapi_image_create(GstVaapiImage *image)
{
    GstVaapiImagePrivate * const priv = image->priv;
    const VAImageFormat *format;
    VAStatus status;

    if (!gst_vaapi_display_has_image_format(priv->display, priv->format))
        return FALSE;

    format = gst_vaapi_image_format_get_va_format(priv->format);

    g_return_val_if_fail(format, FALSE);

    status = vaCreateImage(
        gst_vaapi_display_get_display(priv->display),
        (VAImageFormat *)format,
        priv->width,
        priv->height,
        &priv->image
    );
    if (!vaapi_check_status(status, "vaCreateImage()"))
        return FALSE;

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
    case PROP_DISPLAY:
        priv->display = g_object_ref(g_value_get_object(value));
        break;
    case PROP_FORMAT:
        priv->format = g_value_get_uint(value);
        break;
    case PROP_WIDTH:
        priv->width = g_value_get_uint(value);
        break;
    case PROP_HEIGHT:
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
    case PROP_DISPLAY:
        g_value_set_pointer(value, gst_vaapi_image_get_display(image));
        break;
    case PROP_IMAGE_ID:
        g_value_set_uint(value, gst_vaapi_image_get_id(image));
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
         PROP_DISPLAY,
         g_param_spec_object("display",
                             "display",
                             "GStreamer Va display",
                             GST_VAAPI_TYPE_DISPLAY,
                             G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_IMAGE_ID,
         g_param_spec_uint("id",
                           "VA image id",
                           "VA image id",
                           0, G_MAXUINT32, VA_INVALID_ID,
                           G_PARAM_READABLE));

    g_object_class_install_property
        (object_class,
         PROP_WIDTH,
         g_param_spec_uint("width",
                           "width",
                           "Image width",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_HEIGHT,
         g_param_spec_uint("height",
                           "height",
                           "Image height",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));

    g_object_class_install_property
        (object_class,
         PROP_FORMAT,
         g_param_spec_uint("format",
                           "format",
                           "Image format",
                           0, G_MAXUINT32, 0,
                           G_PARAM_READWRITE|G_PARAM_CONSTRUCT_ONLY));
}

static void
gst_vaapi_image_init(GstVaapiImage *image)
{
    GstVaapiImagePrivate *priv = GST_VAAPI_IMAGE_GET_PRIVATE(image);

    image->priv          = priv;
    priv->display        = NULL;
    priv->image_data     = NULL;
    priv->width          = 0;
    priv->height         = 0;
    priv->format         = 0;

    memset(&priv->image, 0, sizeof(priv->image));
    priv->image.image_id = VA_INVALID_ID;
    priv->image.buf      = VA_INVALID_ID;
}

GstVaapiImage *
gst_vaapi_image_new(
    GstVaapiDisplay    *display,
    GstVaapiImageFormat format,
    guint               width,
    guint               height
)
{
    g_return_val_if_fail(GST_VAAPI_IS_DISPLAY(display), NULL);
    g_return_val_if_fail(width > 0, NULL);
    g_return_val_if_fail(height > 0, NULL);

    GST_DEBUG("format %" GST_FOURCC_FORMAT ", size %ux%u",
              GST_FOURCC_ARGS(format), width, height);

    return g_object_new(GST_VAAPI_TYPE_IMAGE,
                        "display", display,
                        "format",  format,
                        "width",   width,
                        "height",  height,
                        NULL);
}

VAImageID
gst_vaapi_image_get_id(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), VA_INVALID_ID);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    return image->priv->image.image_id;
}

GstVaapiDisplay *
gst_vaapi_image_get_display(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), NULL);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    return image->priv->display;
}

GstVaapiImageFormat
gst_vaapi_image_get_format(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    return image->priv->format;
}

guint
gst_vaapi_image_get_width(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    return image->priv->width;
}

guint
gst_vaapi_image_get_height(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    return image->priv->height;
}

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

gboolean
gst_vaapi_image_map(GstVaapiImage *image)
{
    void *image_data;
    VAStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    if (_gst_vaapi_image_is_mapped(image))
        return TRUE;

    status = vaMapBuffer(
        gst_vaapi_display_get_display(image->priv->display),
        image->priv->image.buf,
        &image_data
    );
    if (!vaapi_check_status(status, "vaMapBuffer()"))
        return FALSE;

    image->priv->image_data = image_data;
    return TRUE;
}

gboolean
gst_vaapi_image_unmap(GstVaapiImage *image)
{
    VAStatus status;

    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), FALSE);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);

    if (!_gst_vaapi_image_is_mapped(image))
        return FALSE;

    status = vaUnmapBuffer(
        gst_vaapi_display_get_display(image->priv->display),
        image->priv->image.buf
    );
    if (!vaapi_check_status(status, "vaUnmapBuffer()"))
        return FALSE;

    image->priv->image_data = NULL;
    return TRUE;
}

guint
gst_vaapi_image_get_plane_count(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);
    g_return_val_if_fail(_gst_vaapi_image_is_mapped(image), 0);

    return image->priv->image.num_planes;
}

guchar *
gst_vaapi_image_get_plane(GstVaapiImage *image, guint plane)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), NULL);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);
    g_return_val_if_fail(_gst_vaapi_image_is_mapped(image), NULL);
    g_return_val_if_fail(plane < image->priv->image.num_planes, NULL);

    return image->priv->image_data + image->priv->image.offsets[plane];
}

guint
gst_vaapi_image_get_pitch(GstVaapiImage *image, guint plane)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), 0);
    g_return_val_if_fail(image->priv->is_constructed, FALSE);
    g_return_val_if_fail(_gst_vaapi_image_is_mapped(image), 0);
    g_return_val_if_fail(plane < image->priv->image.num_planes, 0);

    return image->priv->image.pitches[plane];
}

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
    gboolean swap_YUV;

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
    swap_YUV = ((format == GST_VAAPI_IMAGE_I420 &&
                 priv->format == GST_VAAPI_IMAGE_YV12) ||
                (format == GST_VAAPI_IMAGE_YV12 &&
                 priv->format == GST_VAAPI_IMAGE_I420));
    if (format != priv->format && !swap_YUV)
        return FALSE;

    structure = gst_caps_get_structure(caps, 0);
    gst_structure_get_int(structure, "width",  &width);
    gst_structure_get_int(structure, "height", &height);
    if (width != priv->width || height != priv->height)
        return FALSE;

    if (!gst_vaapi_image_map(image))
        return FALSE;

    if (format == priv->format && data_size == priv->image.data_size)
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
        if (swap_YUV) {
            guint offset = offsets[1];
            guint stride = pitches[1];
            guint width  = widths [1];
            guint height = heights[1];
            offsets[1]   = offsets[2];
            pitches[1]   = pitches[2];
            widths [1]   = widths [2];
            heights[1]   = heights[2];
            offsets[2]   = offset;
            pitches[2]   = stride;
            widths [2]   = width;
            heights[2]   = height;
        }
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
