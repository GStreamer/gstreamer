/*
 *  gstvaapisubpicture.c - VA subpicture abstraction
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
 * SECTION:gstvaapisubpicture
 * @short_description: VA subpicture abstraction
 */

#include "config.h"
#include <string.h>
#include "gstvaapicompat.h"
#include "gstvaapiutils.h"
#include "gstvaapisubpicture.h"
#include "gstvaapi_priv.h"

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiSubpicture, gst_vaapi_subpicture, GST_VAAPI_TYPE_OBJECT);

#define GST_VAAPI_SUBPICTURE_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_SUBPICTURE,	\
                                 GstVaapiSubpicturePrivate))

struct _GstVaapiSubpicturePrivate {
    GstVaapiImage      *image;
};

enum {
    PROP_0,

    PROP_IMAGE
};

static void
gst_vaapi_subpicture_destroy(GstVaapiSubpicture *subpicture)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(subpicture);
    GstVaapiSubpicturePrivate * const priv = subpicture->priv;
    VASubpictureID subpicture_id;
    VAStatus status;

    subpicture_id = GST_VAAPI_OBJECT_ID(subpicture);
    GST_DEBUG("subpicture %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(subpicture_id));

    if (subpicture_id != VA_INVALID_ID) {
        if (display) {
            GST_VAAPI_DISPLAY_LOCK(display);
            status = vaDestroySubpicture(
                GST_VAAPI_DISPLAY_VADISPLAY(display),
                subpicture_id
            );
            GST_VAAPI_DISPLAY_UNLOCK(display);
            if (!vaapi_check_status(status, "vaDestroySubpicture()"))
                g_warning("failed to destroy subpicture %" GST_VAAPI_ID_FORMAT,
                          GST_VAAPI_ID_ARGS(subpicture_id));
        }
        GST_VAAPI_OBJECT_ID(subpicture) = VA_INVALID_ID;
    }

    if (priv->image) {
        g_object_unref(priv->image);
        priv->image = NULL;
    }
}

static gboolean
gst_vaapi_subpicture_create(GstVaapiSubpicture *subpicture)
{
    GstVaapiDisplay * const display = GST_VAAPI_OBJECT_DISPLAY(subpicture);
    GstVaapiSubpicturePrivate * const priv = subpicture->priv;
    VASubpictureID subpicture_id;
    VAStatus status;

    if (!priv->image)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaCreateSubpicture(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        GST_VAAPI_OBJECT_ID(priv->image),
        &subpicture_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaCreateSubpicture()"))
        return FALSE;

    GST_DEBUG("subpicture %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(subpicture_id));
    GST_VAAPI_OBJECT_ID(subpicture) = subpicture_id;
    return TRUE;
}

static void
gst_vaapi_subpicture_finalize(GObject *object)
{
    gst_vaapi_subpicture_destroy(GST_VAAPI_SUBPICTURE(object));

    G_OBJECT_CLASS(gst_vaapi_subpicture_parent_class)->finalize(object);
}

static void
gst_vaapi_subpicture_set_property(
    GObject      *object,
    guint         prop_id,
    const GValue *value,
    GParamSpec   *pspec
)
{
    GstVaapiSubpicture * const subpicture = GST_VAAPI_SUBPICTURE(object);

    switch (prop_id) {
    case PROP_IMAGE:
        gst_vaapi_subpicture_set_image(subpicture, g_value_get_object(value));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_subpicture_get_property(
    GObject    *object,
    guint       prop_id,
    GValue     *value,
    GParamSpec *pspec
)
{
    GstVaapiSubpicture * const subpicture = GST_VAAPI_SUBPICTURE(object);

    switch (prop_id) {
    case PROP_IMAGE:
        g_value_set_object(value, gst_vaapi_subpicture_get_image(subpicture));
        break;
    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void
gst_vaapi_subpicture_class_init(GstVaapiSubpictureClass *klass)
{
    GObjectClass * const object_class = G_OBJECT_CLASS(klass);

    g_type_class_add_private(klass, sizeof(GstVaapiSubpicturePrivate));

    object_class->finalize     = gst_vaapi_subpicture_finalize;
    object_class->set_property = gst_vaapi_subpicture_set_property;
    object_class->get_property = gst_vaapi_subpicture_get_property;

    /**
     * GstVaapiSubpicture:image:
     *
     * The #GstVaapiImage this subpicture is bound to.
     */
    g_object_class_install_property
        (object_class,
         PROP_IMAGE,
         g_param_spec_object("image",
                             "Image",
                             "The GstVaapiImage this subpicture is bound to",
                             GST_VAAPI_TYPE_IMAGE,
                             G_PARAM_READWRITE));
}

static void
gst_vaapi_subpicture_init(GstVaapiSubpicture *subpicture)
{
    GstVaapiSubpicturePrivate *priv = GST_VAAPI_SUBPICTURE_GET_PRIVATE(subpicture);

    subpicture->priv    = priv;
    priv->image         = NULL;
}

/**
 * gst_vaapi_subpicture_new:
 * @image: a #GstVaapiImage
 *
 * Creates a new #GstVaapiSubpicture with @image as source pixels. The
 * newly created object holds a reference on @image.
 *
 * Return value: the newly allocated #GstVaapiSubpicture object
 */
GstVaapiSubpicture *
gst_vaapi_subpicture_new(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), NULL);

    GST_DEBUG("create from image %" GST_VAAPI_ID_FORMAT,
              GST_VAAPI_ID_ARGS(GST_VAAPI_OBJECT_ID(image)));

    return g_object_new(GST_VAAPI_TYPE_SUBPICTURE,
                        "display", GST_VAAPI_OBJECT_DISPLAY(image),
                        "id",      GST_VAAPI_ID(VA_INVALID_ID),
                        "image",   image,
                        NULL);
}

/**
 * gst_vaapi_subpicture_get_id:
 * @subpicture: a #GstVaapiSubpicture
 *
 * Returns the underlying VASubpictureID of the @subpicture.
 *
 * Return value: the underlying VA subpicture id
 */
GstVaapiID
gst_vaapi_subpicture_get_id(GstVaapiSubpicture *subpicture)
{
    g_return_val_if_fail(GST_VAAPI_IS_SUBPICTURE(subpicture), VA_INVALID_ID);

    return GST_VAAPI_OBJECT_ID(subpicture);
}

/**
 * gst_vaapi_subpicture_get_image:
 * @subpicture: a #GstVaapiSubpicture
 *
 * Returns the #GstVaapiImage this @subpicture is bound to.
 *
 * Return value: the #GstVaapiImage this @subpicture is bound to
 */
GstVaapiImage *
gst_vaapi_subpicture_get_image(GstVaapiSubpicture *subpicture)
{
    g_return_val_if_fail(GST_VAAPI_IS_SUBPICTURE(subpicture), NULL);

    return subpicture->priv->image;
}

/**
 * gst_vaapi_subpicture_set_image:
 * @subpicture: a #GstVaapiSubpicture
 * @image: a #GstVaapiImage
 *
 * Binds a new #GstVaapiImage to the @subpicture. The reference to the
 * previous image is released a new one acquired on @image.
 */
void
gst_vaapi_subpicture_set_image(
    GstVaapiSubpicture *subpicture,
    GstVaapiImage      *image
)
{
    g_return_if_fail(GST_VAAPI_IS_SUBPICTURE(subpicture));
    g_return_if_fail(GST_VAAPI_IS_IMAGE(image));

    gst_vaapi_subpicture_destroy(subpicture);

    subpicture->priv->image = g_object_ref(image);
    gst_vaapi_subpicture_create(subpicture);
}
