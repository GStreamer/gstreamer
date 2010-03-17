/*
 *  gstvaapisubpicture.c - VA subpicture abstraction
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
#include "gstvaapiutils.h"
#include "gstvaapisubpicture.h"
#include <va/va_backend.h>

#define DEBUG 1
#include "gstvaapidebug.h"

G_DEFINE_TYPE(GstVaapiSubpicture, gst_vaapi_subpicture, G_TYPE_OBJECT);

#define GST_VAAPI_SUBPICTURE_GET_PRIVATE(obj)                   \
    (G_TYPE_INSTANCE_GET_PRIVATE((obj),                         \
                                 GST_VAAPI_TYPE_SUBPICTURE,	\
                                 GstVaapiSubpicturePrivate))

struct _GstVaapiSubpicturePrivate {
    VASubpictureID      subpicture_id;
    GstVaapiImage      *image;
};

enum {
    PROP_0,

    PROP_SUBPICTURE_ID,
    PROP_IMAGE
};

static void
gst_vaapi_subpicture_destroy(GstVaapiSubpicture *subpicture)
{
    GstVaapiSubpicturePrivate * const priv = subpicture->priv;
    GstVaapiDisplay *display;
    VAStatus status;

    if (priv->subpicture_id != VA_INVALID_ID) {
        display = gst_vaapi_image_get_display(priv->image);
        if (display) {
            GST_VAAPI_DISPLAY_LOCK(display);
            status = vaDestroySubpicture(
                GST_VAAPI_DISPLAY_VADISPLAY(display),
                priv->subpicture_id
            );
            GST_VAAPI_DISPLAY_UNLOCK(display);
            if (!vaapi_check_status(status, "vaDestroySubpicture()"))
                g_warning("failed to destroy subpicture 0x%08x\n",
                          priv->subpicture_id);
        }
        priv->subpicture_id = VA_INVALID_ID;
    }

    if (priv->image) {
        g_object_unref(priv->image);
        priv->image = NULL;
    }
}

static gboolean
gst_vaapi_subpicture_create(GstVaapiSubpicture *subpicture)
{
    GstVaapiSubpicturePrivate * const priv = subpicture->priv;
    GstVaapiDisplay *display;
    VASubpictureID subpicture_id;
    VAStatus status;

    if (!priv->image)
        return FALSE;

    display = gst_vaapi_image_get_display(priv->image);
    if (!display)
        return FALSE;

    GST_VAAPI_DISPLAY_LOCK(display);
    status = vaCreateSubpicture(
        GST_VAAPI_DISPLAY_VADISPLAY(display),
        gst_vaapi_image_get_id(priv->image),
        &subpicture_id
    );
    GST_VAAPI_DISPLAY_UNLOCK(display);
    if (!vaapi_check_status(status, "vaCreateSubpicture()"))
        return FALSE;

    priv->subpicture_id = subpicture_id;
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
    case PROP_SUBPICTURE_ID:
        g_value_set_uint(value, gst_vaapi_subpicture_get_id(subpicture));
        break;
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

    g_object_class_install_property
        (object_class,
         PROP_SUBPICTURE_ID,
         g_param_spec_uint("id",
                           "VA subpicture id",
                           "VA subpicture id",
                           0, G_MAXUINT32, VA_INVALID_ID,
                           G_PARAM_READABLE));

    g_object_class_install_property
        (object_class,
         PROP_IMAGE,
         g_param_spec_object("image",
                             "image",
                             "GStreamer VA image",
                             GST_VAAPI_TYPE_IMAGE,
                             G_PARAM_READWRITE));
}

static void
gst_vaapi_subpicture_init(GstVaapiSubpicture *subpicture)
{
    GstVaapiSubpicturePrivate *priv = GST_VAAPI_SUBPICTURE_GET_PRIVATE(subpicture);

    subpicture->priv    = priv;
    priv->subpicture_id = VA_INVALID_ID;
    priv->image         = NULL;
}

GstVaapiSubpicture *
gst_vaapi_subpicture_new(GstVaapiImage *image)
{
    g_return_val_if_fail(GST_VAAPI_IS_IMAGE(image), NULL);

    GST_DEBUG("image 0x%08x", gst_vaapi_image_get_id(image));

    return g_object_new(GST_VAAPI_TYPE_SUBPICTURE,
                        "image", image,
                        NULL);
}

VASubpictureID
gst_vaapi_subpicture_get_id(GstVaapiSubpicture *subpicture)
{
    g_return_val_if_fail(GST_VAAPI_IS_SUBPICTURE(subpicture), VA_INVALID_ID);

    return subpicture->priv->subpicture_id;
}

GstVaapiImage *
gst_vaapi_subpicture_get_image(GstVaapiSubpicture *subpicture)
{
    g_return_val_if_fail(GST_VAAPI_IS_SUBPICTURE(subpicture), NULL);

    return subpicture->priv->image;
}

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
